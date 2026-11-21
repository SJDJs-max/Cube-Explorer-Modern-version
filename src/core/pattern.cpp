#include "cube/pattern.hpp"
#include "cube/runtime_config.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace cube {
namespace {
using Clock = std::chrono::steady_clock;

struct Abort { PatternStop reason; };

struct Control {
    const PatternRequest& request;
    PatternReport& report;
    PatternProgressCallback progress;
    Clock::time_point start = Clock::now(), last_progress = start;
    int depth = 0;
    bool preparing = true;
    void check(bool force_progress = false, bool depth_started = false) {
        const auto now = Clock::now();
        if (request.cancelled && request.cancelled()) throw Abort{PatternStop::Cancelled};
        if (request.time_limit.count() > 0 && now - start >= request.time_limit) throw Abort{PatternStop::TimeLimit};
        if (progress && (force_progress || now - last_progress >= std::chrono::milliseconds(200))) {
            progress({report.nodes, depth, report.results,
                std::chrono::duration_cast<std::chrono::milliseconds>(now - start), preparing, depth_started});
            last_progress = now;
        }
    }
};

bool matches(const PatternState& actual, const PatternState& wanted) {
    // Used with one concrete side. A wildcard on either side is unobserved.
    auto equal = [](const auto& a, const auto& b) {
        for (std::size_t i = 0; i < a.size(); ++i) if (a[i] >= 0 && b[i] >= 0 && a[i] != b[i]) return false;
        return true;
    };
    return equal(actual.cp, wanted.cp) && equal(actual.co, wanted.co) &&
        equal(actual.ep, wanted.ep) && equal(actual.eo, wanted.eo) && equal(actual.center_ori, wanted.center_ori);
}

PatternState apply_logical_u(const PatternState& p, int power) {
    if (power == 0) return p;
    auto q = p; q.frame = 0;
    q = apply_pattern(q, Move{'U', static_cast<std::uint8_t>(power)});
    q.frame = p.frame;
    return q;
}

std::vector<PatternState> make_goals(const PatternRequest& r) {
    PatternState target = r.direction == PatternDirection::Solve ? pattern_from_cube(CubeState{}, true) : r.pattern;
    std::vector<PatternState> goals;
    for (int k = 0; k < (r.modulo_u ? 4 : 1); ++k) goals.push_back(apply_logical_u(target, k));
    return goals;
}

PatternState initial_state(const PatternRequest& r) {
    if (r.direction == PatternDirection::Solve) return r.pattern;
    auto start = pattern_from_cube(CubeState{}, true);
    start.frame = r.pattern.frame;
    return start;
}

int goal_index(const PatternState& state, const std::vector<PatternState>& goals,
               bool require_d_on_d = false, Face target_d = Face::D) {
    if (require_d_on_d && logical_face(state.frame, Face::D) != target_d) return -1;
    for (std::size_t i = 0; i < goals.size(); ++i) if (matches(state, goals[i])) return static_cast<int>(i);
    return -1;
}

bool u_conjugation_invariant(const PatternState& pattern, int power) {
    // U changes no cubie orientations, so its left multiplication is defined
    // even for an anonymous orientation-only cubie. Identity labels are merely
    // permuted. This exact comparison is not a test for a monochrome U face.
    auto p = apply_logical_u(pattern, power);
    const auto& left = outer_move(Face::U, 4 - power);
    for (auto& id : p.cp) if (id >= 0) id = static_cast<std::int8_t>(left.cp[id]);
    for (auto& id : p.ep) if (id >= 0) id = static_cast<std::int8_t>(left.ep[id]);
    if (p.center_ori[0] >= 0) p.center_ori[0] = static_cast<std::int8_t>((p.center_ori[0] + 4 - power) % 4);
    return p == pattern;
}

struct AbstractMove {
    std::array<std::uint8_t, 8> corner_dest{}, corner_ori{};
    std::array<std::uint8_t, 12> edge_dest{}, edge_ori{};
    std::array<std::uint8_t, 6> centers{};
    std::uint8_t from_orbit = 0;
};

AbstractMove abstract_move(const CubieState& delta, const std::array<std::uint8_t, 6>& centers,
                           std::uint8_t from_orbit = 0) {
    AbstractMove result; result.centers = centers; result.from_orbit = from_orbit;
    for (int dest = 0; dest < 8; ++dest) {
        result.corner_dest[delta.cp[dest]] = static_cast<std::uint8_t>(dest);
        result.corner_ori[delta.cp[dest]] = delta.co[dest];
    }
    for (int dest = 0; dest < 12; ++dest) {
        result.edge_dest[delta.ep[dest]] = static_cast<std::uint8_t>(dest);
        result.edge_ori[delta.ep[dest]] = delta.eo[dest];
    }
    return result;
}

struct AbstractMoves {
    std::uint8_t orbit_count = 1;
    std::array<std::int8_t, 24> orbit_of_frame{};
    std::uint32_t goal_orbit_mask = 0xFFFFFFu;
    std::vector<std::vector<AbstractMove>> reverse_from_orbit;
    std::string key;
};

std::string transform_key(const MoveTransform& t) {
    std::string key;
    key.reserve(46);
    for (const auto& a : {std::span<const std::uint8_t>(t.delta.cp), std::span<const std::uint8_t>(t.delta.co),
         std::span<const std::uint8_t>(t.delta.ep), std::span<const std::uint8_t>(t.delta.eo), std::span<const std::uint8_t>(t.center_delta)})
        for (auto v : a) key += static_cast<char>(v);
    return key;
}

PatternPreRotation effective_pre_rotation(const PatternRequest& r) noexcept {
    if (r.pre_rotation != PatternPreRotation::None) return r.pre_rotation;
    return r.pre_rotate_y ? PatternPreRotation::YOnly : PatternPreRotation::None;
}

AbstractMoves project_moves(std::span<const Move> moves, std::uint8_t initial_frame, bool track_orbits = true,
                            PatternPreRotation pre_rot = PatternPreRotation::None, bool require_d_on_d = false) {
    // Track free-rotation equivalence classes (orbits) of reachable holding
    // frames whenever track_orbits is true. Every free rotation stays inside
    // the same orbit at cost 0, and every paid move transitions from
    // orbit[frame] to orbit[next_frame] at cost 1. Thus distances in the
    // orbit-product graph remain 100% admissible lower bounds while avoiding
    // the severe bound collapse of merging wide-turn frames into all 18 outer moves.
    AbstractMoves result;
    const Face target_d = logical_face(initial_frame, Face::D);
    std::array<bool, 24> reached{};
    std::vector<std::uint8_t> frames{initial_frame}; reached[initial_frame] = true;
    if (pre_rot == PatternPreRotation::YOnly) {
        for (std::uint8_t p = 1; p <= 3; ++p) {
            const auto nf = rotated_frame(initial_frame, Move{'y', p});
            if (!reached[nf]) { reached[nf] = true; frames.push_back(nf); }
        }
    } else if (pre_rot == PatternPreRotation::All24) {
        for (std::uint8_t f = 0; f < 24; ++f)
            if (!reached[f]) { reached[f] = true; frames.push_back(f); }
    }
    for (std::size_t head = 0; head < frames.size(); ++head) for (auto m : moves) {
        const auto next = move_transform(frames[head], m).next_frame;
        if (!reached[next]) { reached[next] = true; frames.push_back(next); }
    }
    result.orbit_of_frame.fill(-1);
    if (!track_orbits) {
        result.orbit_count = 1;
        result.orbit_of_frame.fill(0);
    } else {
        std::uint8_t next_orbit = 0;
        for (int f = 0; f < 24; ++f) {
            if (!reached[f] || result.orbit_of_frame[f] >= 0) continue;
            const auto id = static_cast<std::int8_t>(next_orbit++);
            std::vector<std::uint8_t> comp{static_cast<std::uint8_t>(f)};
            result.orbit_of_frame[f] = id;
            for (std::size_t head = 0; head < comp.size(); ++head)
                for (auto m : moves) if (m.is_rotation()) {
                    const auto nf = rotated_frame(comp[head], m);
                    if (reached[nf] && result.orbit_of_frame[nf] < 0) {
                        result.orbit_of_frame[nf] = id;
                        comp.push_back(nf);
                    }
                }
        }
        result.orbit_count = std::max<std::uint8_t>(1, next_orbit);
        if (result.orbit_count == 1) result.orbit_of_frame.fill(0);
    }
    if (require_d_on_d) {
        result.goal_orbit_mask = 0;
        for (int f = 0; f < 24; ++f) {
            if (reached[f] && logical_face(static_cast<std::uint8_t>(f), Face::D) == target_d) {
                const int o = result.orbit_of_frame[f];
                if (o >= 0) result.goal_orbit_mask |= (1u << o);
            }
        }
    } else {
        result.goal_orbit_mask = (1u << result.orbit_count) - 1u;
    }
    result.reverse_from_orbit.resize(result.orbit_count);
    std::unordered_set<std::string> seen;
    std::vector<std::string> sorted;
    for (auto m : moves) if (!m.is_rotation()) for (auto frame : frames) {
        const auto& t = move_transform(frame, m);
        const auto o_from = static_cast<std::uint8_t>(result.orbit_of_frame[frame]);
        const auto o_to = static_cast<std::uint8_t>(result.orbit_of_frame[t.next_frame]);
        std::string edge_key;
        edge_key.reserve(48);
        edge_key += static_cast<char>(o_from);
        edge_key += static_cast<char>(o_to);
        edge_key += transform_key(t);
        if (!seen.insert(edge_key).second) continue;
        sorted.push_back(std::move(edge_key));
        std::array<std::uint8_t, 6> centers{};
        for (int i = 0; i < 6; ++i) centers[i] = static_cast<std::uint8_t>((4 - t.center_delta[i]) % 4);
        result.reverse_from_orbit[o_to].push_back(abstract_move(cube::inverse(t.delta), centers, o_from));
    }
    std::sort(sorted.begin(), sorted.end());
    result.key += static_cast<char>(result.orbit_count);
    for (int i = 0; i < 3; ++i) result.key += static_cast<char>((result.goal_orbit_mask >> (8 * i)) & 255);
    for (auto o : result.orbit_of_frame) result.key += static_cast<char>(o + 1);
    for (int i = 0; i < 4; ++i) result.key += static_cast<char>((sorted.size() >> (8 * i)) & 255);
    for (auto& key : sorted) result.key += key;
    return result;
}

std::uint32_t permutations(int n, int k) {
    std::uint32_t result = 1;
    for (int i = 0; i < k; ++i) result *= static_cast<unsigned>(n - i);
    return result;
}

std::uint32_t rank_positions(const std::uint8_t* p, int k, int n) {
    std::uint32_t index = 0, used = 0;
    for (int i = 0; i < k; ++i) {
        const unsigned smaller = p[i] - std::popcount(used & ((1u << p[i]) - 1));
        index = index * static_cast<unsigned>(n - i) + smaller;
        used |= 1u << p[i];
    }
    return index;
}

void unrank_positions(std::uint32_t index, std::uint8_t* p, int k, int n) {
    std::array<int, 12> digits{};
    for (int i = k - 1; i >= 0; --i) { digits[i] = static_cast<int>(index % (n - i)); index /= n - i; }
    unsigned available = (1u << n) - 1;
    for (int i = 0; i < k; ++i) {
        unsigned choices = available;
        for (int j = 0; j < digits[i]; ++j) choices &= choices - 1;
        p[i] = static_cast<std::uint8_t>(std::countr_zero(choices));
        available &= ~(1u << p[i]);
    }
}

struct InversePieces {
    std::array<std::uint8_t, 8> cp{};
    std::array<std::uint8_t, 12> ep{};
    bool has_cp = false, has_ep = false;
    const std::array<std::uint8_t, 8>& corners(const PatternState& state) {
        if (!has_cp) {
            for (int i = 0; i < 8; ++i)
                if (state.cp[i] >= 0) cp[state.cp[i]] = static_cast<std::uint8_t>(i);
            has_cp = true;
        }
        return cp;
    }
    const std::array<std::uint8_t, 12>& edges(const PatternState& state) {
        if (!has_ep) {
            for (int i = 0; i < 12; ++i)
                if (state.ep[i] >= 0) ep[state.ep[i]] = static_cast<std::uint8_t>(i);
            has_ep = true;
        }
        return ep;
    }
};

struct Bound {
    enum class Kind { Center, Orientation, Group };
    Kind kind = Kind::Group;
    std::vector<std::uint8_t> distance;
    std::uint8_t orbit_count = 1;
    std::array<std::int8_t, 24> orbit_of_frame{};
    virtual ~Bound() = default;
    virtual std::uint32_t base_index(const PatternState&, InversePieces&) const = 0;
    std::uint32_t seed_index(const PatternState& state) const {
        InversePieces inv;
        return base_index(state, inv);
    }
    int eval(const PatternState& state, InversePieces& inv) const {
        const auto base = base_index(state, inv);
        const auto idx = orbit_count == 1 ? base : base * orbit_count + static_cast<std::uint32_t>(orbit_of_frame[state.frame]);
        return distance[idx];
    }
    int operator()(const PatternState& state) const {
        InversePieces inv;
        return eval(state, inv);
    }
};

struct GroupToken { std::uint8_t id; bool orientation; };

struct GroupBound final : Bound {
    std::vector<GroupToken> corners, edges;
    std::uint32_t edge_count = 1, orientation_count = 1;
    struct Values { std::array<std::uint8_t, 8> cp{}, co{}; std::array<std::uint8_t, 12> ep{}, eo{}; };
    static std::size_t base_size_for(const std::vector<GroupToken>& c, const std::vector<GroupToken>& e) {
        std::size_t sz = permutations(8, static_cast<int>(c.size()));
        sz *= permutations(12, static_cast<int>(e.size()));
        for (auto t : c) if (t.orientation) sz *= 3;
        for (auto t : e) if (t.orientation) sz *= 2;
        return sz;
    }
    GroupBound(std::vector<GroupToken> c, std::vector<GroupToken> e, const AbstractMoves& moves)
        : corners(std::move(c)), edges(std::move(e)) {
        kind = Kind::Group;
        orbit_count = moves.orbit_count;
        orbit_of_frame = moves.orbit_of_frame;
        edge_count = permutations(12, static_cast<int>(edges.size()));
        for (auto t : corners) if (t.orientation) orientation_count *= 3;
        for (auto t : edges) if (t.orientation) orientation_count *= 2;
        distance.assign(
            static_cast<std::size_t>(permutations(8, static_cast<int>(corners.size()))) * edge_count * orientation_count * orbit_count,
            255);
    }
    std::uint32_t rank(const Values& v) const {
        auto index = rank_positions(v.cp.data(), static_cast<int>(corners.size()), 8) * edge_count +
            rank_positions(v.ep.data(), static_cast<int>(edges.size()), 12);
        for (std::size_t i = 0; i < corners.size(); ++i) if (corners[i].orientation) index = index * 3 + v.co[i];
        for (std::size_t i = 0; i < edges.size(); ++i) if (edges[i].orientation) index = index * 2 + v.eo[i];
        return index;
    }
    Values unrank(std::uint32_t index) const {
        Values v;
        for (std::size_t i = edges.size(); i-- > 0;) if (edges[i].orientation) { v.eo[i] = static_cast<std::uint8_t>(index % 2); index /= 2; }
        for (std::size_t i = corners.size(); i-- > 0;) if (corners[i].orientation) { v.co[i] = static_cast<std::uint8_t>(index % 3); index /= 3; }
        unrank_positions(index % edge_count, v.ep.data(), static_cast<int>(edges.size()), 12);
        unrank_positions(index / edge_count, v.cp.data(), static_cast<int>(corners.size()), 8);
        return v;
    }
    std::uint32_t base_index(const PatternState& state, InversePieces& inv) const override {
        Values v;
        if (!corners.empty()) {
            const auto& cp = inv.corners(state);
            for (std::size_t i = 0; i < corners.size(); ++i) {
                v.cp[i] = cp[corners[i].id];
                if (corners[i].orientation) v.co[i] = static_cast<std::uint8_t>(state.co[v.cp[i]]);
            }
        }
        if (!edges.empty()) {
            const auto& ep = inv.edges(state);
            for (std::size_t i = 0; i < edges.size(); ++i) {
                v.ep[i] = ep[edges[i].id];
                if (edges[i].orientation) v.eo[i] = static_cast<std::uint8_t>(state.eo[v.ep[i]]);
            }
        }
        return rank(v);
    }
    template<class ShouldStop>
    bool build(const AbstractMoves& moves, const std::vector<PatternState>& goals, ShouldStop&& should_stop) {
        std::vector<std::uint32_t> queue; queue.reserve(distance.size());
        for (const auto& goal : goals) {
            const auto base = seed_index(goal);
            for (std::uint8_t o = 0; o < orbit_count; ++o) {
                if (!(moves.goal_orbit_mask & (1u << o))) continue;
                const auto seed = base * orbit_count + o;
                if (distance[seed] == 255) { distance[seed] = 0; queue.push_back(seed); }
            }
        }
        for (std::size_t head = 0; head < queue.size(); ++head) {
            if ((head & 1023) == 0 && should_stop()) return false;
            const auto flat = queue[head];
            const auto o_to = orbit_count == 1 ? std::uint32_t{0} : flat % orbit_count;
            const auto old = unrank(orbit_count == 1 ? flat : flat / orbit_count);
            const auto d = static_cast<std::uint8_t>(distance[flat] + 1);
            for (const auto& m : moves.reverse_from_orbit[o_to]) {
                Values next;
                for (std::size_t i = 0; i < corners.size(); ++i) {
                    const auto p = old.cp[i];
                    next.cp[i] = m.corner_dest[p];
                    const auto o = static_cast<std::uint8_t>(old.co[i] + m.corner_ori[p]);
                    next.co[i] = o >= 3 ? static_cast<std::uint8_t>(o - 3) : o;
                }
                for (std::size_t i = 0; i < edges.size(); ++i) {
                    const auto p = old.ep[i];
                    next.ep[i] = m.edge_dest[p];
                    next.eo[i] = static_cast<std::uint8_t>((old.eo[i] + m.edge_ori[p]) & 1u);
                }
                const auto idx = orbit_count == 1 ? rank(next) : rank(next) * orbit_count + m.from_orbit;
                if (distance[idx] == 255) { distance[idx] = d; queue.push_back(idx); }
            }
        }
        return true;
    }
};

struct OrientationBound final : Bound {
    bool corner;
    int count, base, radix, unknown_count;
    static std::size_t base_size_for(bool corners, int unknown) {
        const int count = corners ? 8 : 12;
        const int radix = (corners ? 3 : 2) + (unknown != 0);
        std::size_t size = 1;
        for (int i = 0; i < count; ++i) size *= radix;
        return size;
    }
    OrientationBound(bool corners, int unknown, const AbstractMoves& moves) : corner(corners), count(corners ? 8 : 12),
        base(corners ? 3 : 2), radix(base + (unknown != 0)), unknown_count(unknown) {
        kind = Kind::Orientation;
        orbit_count = moves.orbit_count;
        orbit_of_frame = moves.orbit_of_frame;
        distance.assign(base_size_for(corners, unknown) * orbit_count, 255);
    }
    std::uint32_t base_index(const PatternState& state, InversePieces&) const override {
        std::uint32_t index = 0;
        for (int i = 0; i < count; ++i) {
            const int o = corner ? state.co[i] : state.eo[i];
            index = index * radix + (o < 0 ? base : o);
        }
        return index;
    }
    template<class ShouldStop>
    bool build(const AbstractMoves& moves, const std::vector<PatternState>& goals, bool solve, ShouldStop&& should_stop) {
        std::vector<std::uint32_t> queue; queue.reserve(distance.size());
        auto seed = [&](std::uint32_t base_i) {
            for (std::uint8_t o = 0; o < orbit_count; ++o) {
                if (!(moves.goal_orbit_mask & (1u << o))) continue;
                const auto i = base_i * orbit_count + o;
                if (distance[i] == 255) { distance[i] = 0; queue.push_back(i); }
            }
        };
        if (solve) {
            for (unsigned mask = 0; mask < (1u << count); ++mask) {
                if (std::popcount(mask) != unknown_count) continue;
                std::uint32_t i = 0;
                for (int p = 0; p < count; ++p) i = i * radix + ((mask & (1u << p)) ? base : 0);
                seed(i);
            }
        } else {
            for (const auto& goal : goals) {
                auto add = [&](auto&& self, int p, std::uint32_t i) -> void {
                    if (p == count) { seed(i); return; }
                    const int o = corner ? goal.co[p] : goal.eo[p];
                    if (o >= 0) self(self, p + 1, i * radix + o);
                    else for (int k = 0; k < base; ++k) self(self, p + 1, i * radix + k);
                };
                add(add, 0, 0);
            }
        }
        std::array<std::uint8_t, 12> old{}, next{};
        for (std::size_t head = 0; head < queue.size(); ++head) {
            if ((head & 1023) == 0 && should_stop()) return false;
            const auto flat = queue[head];
            const auto o_to = orbit_count == 1 ? std::uint32_t{0} : flat % orbit_count;
            auto i = orbit_count == 1 ? flat : flat / orbit_count;
            for (int p = count - 1; p >= 0; --p) { old[p] = static_cast<std::uint8_t>(i % radix); i /= radix; }
            const auto d = static_cast<std::uint8_t>(distance[flat] + 1);
            for (const auto& m : moves.reverse_from_orbit[o_to]) {
                for (int p = 0; p < count; ++p) {
                    const auto dest = corner ? m.corner_dest[p] : m.edge_dest[p];
                    const auto ori = corner ? m.corner_ori[p] : m.edge_ori[p];
                    next[dest] = static_cast<std::uint8_t>(old[p] == base ? base : (old[p] + ori) % base);
                }
                std::uint32_t base_idx = 0; for (int p = 0; p < count; ++p) base_idx = base_idx * radix + next[p];
                const auto idx = orbit_count == 1 ? base_idx : base_idx * orbit_count + m.from_orbit;
                if (distance[idx] == 255) { distance[idx] = d; queue.push_back(idx); }
            }
        }
        return true;
    }
};

struct CenterBound final : Bound {
    std::vector<int> faces;
    CenterBound(std::vector<int> f, const AbstractMoves& moves) : faces(std::move(f)) {
        kind = Kind::Center;
        orbit_count = moves.orbit_count;
        orbit_of_frame = moves.orbit_of_frame;
        distance.assign((std::size_t{1} << (2 * faces.size())) * orbit_count, 255);
    }
    std::uint32_t base_index(const PatternState& p, InversePieces&) const override {
        std::uint32_t i = 0; for (int f : faces) i = i * 4 + p.center_ori[f]; return i;
    }
    template<class ShouldStop>
    bool build(const AbstractMoves& moves, const std::vector<PatternState>& goals, ShouldStop&& should_stop) {
        std::vector<std::uint32_t> queue;
        for (const auto& goal : goals) {
            const auto base = seed_index(goal);
            for (std::uint8_t o = 0; o < orbit_count; ++o) {
                if (!(moves.goal_orbit_mask & (1u << o))) continue;
                const auto i = base * orbit_count + o;
                if (distance[i] == 255) { distance[i] = 0; queue.push_back(i); }
            }
        }
        for (std::size_t head = 0; head < queue.size(); ++head) {
            if ((head & 1023) == 0 && should_stop()) return false;
            const auto flat = queue[head];
            const auto o_to = orbit_count == 1 ? std::uint32_t{0} : flat % orbit_count;
            auto idx = orbit_count == 1 ? flat : flat / orbit_count;
            std::array<std::uint8_t, 6> old{};
            for (std::size_t p = faces.size(); p-- > 0;) { old[p] = static_cast<std::uint8_t>(idx % 4); idx /= 4; }
            const auto d = static_cast<std::uint8_t>(distance[flat] + 1);
            for (const auto& m : moves.reverse_from_orbit[o_to]) {
                std::uint32_t next = 0;
                for (std::size_t p = 0; p < faces.size(); ++p) next = next * 4 + (old[p] + m.centers[faces[p]]) % 4;
                const auto nflat = orbit_count == 1 ? next : next * orbit_count + m.from_orbit;
                if (distance[nflat] == 255) { distance[nflat] = d; queue.push_back(nflat); }
            }
        }
        return true;
    }
};

std::mutex cache_mutex;
std::unordered_map<std::string, std::shared_ptr<const Bound>> bound_cache;
std::size_t cache_entries = 0;

void append_number(std::string& key, std::uint32_t n) {
    for (int i = 0; i < 4; ++i) key += static_cast<char>((n >> (8 * i)) & 255);
}

std::vector<std::shared_ptr<const Bound>> make_bounds(const PatternRequest& r,
        const std::vector<PatternState>& goals, Control& control) {
    std::vector<std::shared_ptr<const Bound>> bounds;
    if (r.heuristic == PatternHeuristic::None) return bounds;
    const auto pre_rot = effective_pre_rotation(r);
    const auto orbit_moves = project_moves(r.allowed_moves, r.pattern.frame, true, pre_rot, r.require_d_on_d);
    const auto collapsed_moves =
        orbit_moves.orbit_count > 1 ? project_moves(r.allowed_moves, r.pattern.frame, false, pre_rot, r.require_d_on_d) : orbit_moves;
    static constexpr std::size_t max_orbit_entries = 3'500'000;
    const std::size_t high_gen_limit = pattern_high_gen_entry_limit();
    std::array<bool, 18> paid_sym_seen{};
    int paid_symbol_count = 0;
    for (auto m : r.allowed_moves) {
        if (m.is_rotation()) continue;
        const auto pos = move_names.find(m.symbol);
        if (pos != std::string_view::npos && !paid_sym_seen[pos]) {
            paid_sym_seen[pos] = true;
            ++paid_symbol_count;
        }
    }
    const bool high_gen = r.heuristic == PatternHeuristic::Strong && paid_symbol_count >= 5 &&
                          (r.max_turns < 0 || r.max_turns >= 9) && high_gen_limit >= 16'000'000;
    auto select_moves = [&](std::size_t base_size, std::size_t limit = 0) -> const AbstractMoves& {
        const std::size_t cap = limit ? limit : max_orbit_entries;
        return (orbit_moves.orbit_count > 1 && base_size * orbit_moves.orbit_count <= cap)
                   ? orbit_moves
                   : collapsed_moves;
    };
    const auto& p = r.pattern;
    std::array<int, 8> corner_pos; corner_pos.fill(-1);
    std::array<int, 12> edge_pos; edge_pos.fill(-1);
    for (int i = 0; i < 8; ++i) if (p.cp[i] >= 0) corner_pos[p.cp[i]] = i;
    for (int i = 0; i < 12; ++i) if (p.ep[i] >= 0) edge_pos[p.ep[i]] = i;
    std::vector<GroupToken> corners, edges;
    for (int i = 0; i < 8; ++i) if (corner_pos[i] >= 0) corners.push_back({static_cast<std::uint8_t>(i), p.co[corner_pos[i]] >= 0});
    for (int i = 0; i < 12; ++i) if (edge_pos[i] >= 0) edges.push_back({static_cast<std::uint8_t>(i), p.eo[edge_pos[i]] >= 0});

    struct BoundJob {
        std::string key;
        std::shared_ptr<const Bound> ready;
        std::function<std::shared_ptr<const Bound>(const std::function<bool()>&)> build;
    };
    std::vector<BoundJob> jobs;
    std::unordered_set<std::string> scheduled;
    auto schedule_job = [&](std::string key,
                            std::function<std::shared_ptr<const Bound>(const std::function<bool()>&)> builder) {
        if (!scheduled.insert(key).second) return;
        jobs.push_back({std::move(key), nullptr, std::move(builder)});
    };

    // Place lightweight O(1) bounds (centers & orientations) first for faster early cutoff.
    std::vector<int> centers;
    for (int i = 0; i < 6; ++i) if (p.center_ori[i] >= 0) centers.push_back(i);
    if (!centers.empty()) {
        const auto& moves = select_moves(std::size_t{1} << (2 * centers.size()));
        CenterBound proto(centers, moves);
        std::string key = "centers_v2" + moves.key;
        key += static_cast<char>(centers.size()); for (int f : centers) key += static_cast<char>(f);
        for (const auto& g : goals) append_number(key, proto.seed_index(g));
        schedule_job(std::move(key), [centers, &moves, &goals](const std::function<bool()>& stop) -> std::shared_ptr<const Bound> {
            auto b = std::make_shared<CenterBound>(centers, moves);
            return b->build(moves, goals, stop) ? b : nullptr;
        });
    }
    for (bool corner : {false, true}) {
        int specified = 0; bool anonymous = false;
        const int count = corner ? 8 : 12;
        for (int i = 0; i < count; ++i) {
            const int id = corner ? p.cp[i] : p.ep[i], o = corner ? p.co[i] : p.eo[i];
            specified += o >= 0; anonymous |= o >= 0 && id < 0;
        }
        if (!anonymous && specified < (corner ? 5 : 6)) continue;
        const int unknown = r.direction == PatternDirection::Solve ? count - specified : 0;
        const auto& moves = select_moves(OrientationBound::base_size_for(corner, unknown));
        std::string key = (corner ? "cornerori_v2" : "edgeori_v2") + moves.key;
        key += static_cast<char>(r.direction == PatternDirection::Solve);
        key += static_cast<char>(unknown);
        if (r.direction == PatternDirection::Generate) for (const auto& g : goals)
            for (int i = 0; i < count; ++i) key += static_cast<char>((corner ? g.co[i] : g.eo[i]) + 1);
        const bool solve_dir = r.direction == PatternDirection::Solve;
        schedule_job(std::move(key), [corner, unknown, solve_dir, &moves, &goals](const std::function<bool()>& stop) -> std::shared_ptr<const Bound> {
            auto b = std::make_shared<OrientationBound>(corner, unknown, moves);
            return b->build(moves, goals, solve_dir, stop) ? b : nullptr;
        });
    }
    auto group = [&](std::vector<GroupToken> c, std::vector<GroupToken> e, std::size_t orbit_limit = 0) {
        if (c.empty() && e.empty()) return;
        const auto& moves = select_moves(GroupBound::base_size_for(c, e), orbit_limit);
        // Compute seeds cheaply without allocating the full distance table yet.
        std::string key = "group_v2" + moves.key;
        key += static_cast<char>(c.size()); for (auto t : c) { key += static_cast<char>(t.id); key += static_cast<char>(t.orientation); }
        key += static_cast<char>(e.size()); for (auto t : e) { key += static_cast<char>(t.id); key += static_cast<char>(t.orientation); }
        const std::uint32_t edge_cnt = permutations(12, static_cast<int>(e.size()));
        auto seed_of = [&](const PatternState& goal) {
            InversePieces inv;
            std::array<std::uint8_t, 8> cp{}, co{};
            std::array<std::uint8_t, 12> ep{}, eo{};
            if (!c.empty()) {
                const auto& ic = inv.corners(goal);
                for (std::size_t i = 0; i < c.size(); ++i) {
                    cp[i] = ic[c[i].id];
                    if (c[i].orientation) co[i] = static_cast<std::uint8_t>(goal.co[cp[i]]);
                }
            }
            if (!e.empty()) {
                const auto& ie = inv.edges(goal);
                for (std::size_t i = 0; i < e.size(); ++i) {
                    ep[i] = ie[e[i].id];
                    if (e[i].orientation) eo[i] = static_cast<std::uint8_t>(goal.eo[ep[i]]);
                }
            }
            auto index = rank_positions(cp.data(), static_cast<int>(c.size()), 8) * edge_cnt +
                rank_positions(ep.data(), static_cast<int>(e.size()), 12);
            for (std::size_t i = 0; i < c.size(); ++i) if (c[i].orientation) index = index * 3 + co[i];
            for (std::size_t i = 0; i < e.size(); ++i) if (e[i].orientation) index = index * 2 + eo[i];
            return index;
        };
        std::vector<std::uint32_t> seeds;
        seeds.reserve(goals.size());
        for (const auto& goal : goals) seeds.push_back(seed_of(goal));
        std::sort(seeds.begin(), seeds.end());
        for (auto seed : seeds) append_number(key, seed);
        schedule_job(std::move(key), [c = std::move(c), e = std::move(e), &moves, &goals](const std::function<bool()>& stop) -> std::shared_ptr<const Bound> {
            auto b = std::make_shared<GroupBound>(c, e, moves);
            return b->build(moves, goals, stop) ? b : nullptr;
        });
    };

    const bool deep_budget = r.max_turns < 0 || r.max_turns >= 6;
    auto cperm = corners; for (auto& t : cperm) t.orientation = false;
    group(cperm, {});
    for (std::size_t start = 0; start < corners.size(); start += 4)
        group({corners.begin() + start, corners.begin() + std::min(start + 4, corners.size())}, {});
    for (std::size_t start = 0; start < edges.size(); start += 4)
        group({}, {edges.begin() + start, edges.begin() + std::min(start + 4, edges.size())});

    const bool use_7edge = high_gen && edges.size() >= 7 &&
        static_cast<std::size_t>(permutations(12, 7)) * orbit_moves.orbit_count <= high_gen_limit;
    const bool use_6edge = !use_7edge && deep_budget && edges.size() >= 6 &&
        static_cast<std::size_t>(permutations(12, 6)) * orbit_moves.orbit_count <= max_orbit_entries;
    if (use_7edge) {
        std::vector<std::size_t> edge_starts{0};
        if (edges.size() > 7) edge_starts.push_back(edges.size() - 7);
        for (auto start : edge_starts) {
            std::vector<GroupToken> e(edges.begin() + start, edges.begin() + start + 7);
            for (auto& t : e) t.orientation = false;
            group({}, std::move(e), high_gen_limit);
        }
    } else if (use_6edge) {
        std::vector<std::size_t> edge_starts{0};
        if (edges.size() > 6) edge_starts.push_back(edges.size() - 6);
        for (auto start : edge_starts) {
            std::vector<GroupToken> e(edges.begin() + start, edges.begin() + start + 6);
            for (auto& t : e) t.orientation = false;
            group({}, std::move(e));
        }
    } else {
        std::vector<std::size_t> edge_starts;
        if (!edges.empty()) edge_starts.push_back(0);
        if (edges.size() > 10) edge_starts.push_back(5);
        if (edges.size() > 5) edge_starts.push_back(edges.size() - 5);
        for (auto start : edge_starts) {
            std::vector<GroupToken> e(edges.begin() + start, edges.begin() + std::min(start + 5, edges.size()));
            for (auto& t : e) t.orientation = false;
            group({}, std::move(e));
        }
    }
    const bool use_3c4e = high_gen && corners.size() >= 4 && edges.size() >= 4 &&
        static_cast<std::size_t>(permutations(8, 3)) * permutations(12, 4) * orbit_moves.orbit_count <= high_gen_limit;
    const bool use_2c4e = !use_3c4e && deep_budget && corners.size() >= 4 && edges.size() >= 4 &&
        static_cast<std::size_t>(permutations(8, 2)) * permutations(12, 4) * orbit_moves.orbit_count <= max_orbit_entries;
    if (use_3c4e) {
        std::vector<GroupToken> c0{{corners[0].id, false}, {corners[1].id, false}, {corners[2].id, false}};
        std::vector<GroupToken> e0(edges.begin(), edges.begin() + 4);
        for (auto& t : e0) t.orientation = false;
        group(std::move(c0), std::move(e0), high_gen_limit);
        std::vector<GroupToken> c1{{corners[1].id, false}, {corners[2].id, false}, {corners[3].id, false}};
        std::vector<GroupToken> e1(edges.end() - 4, edges.end());
        for (auto& t : e1) t.orientation = false;
        group(std::move(c1), std::move(e1), high_gen_limit);
    } else if (use_2c4e) {
        std::vector<GroupToken> c0{{corners[0].id, false}, {corners[1].id, false}};
        std::vector<GroupToken> e0(edges.begin(), edges.begin() + 4);
        for (auto& t : e0) t.orientation = false;
        group(std::move(c0), std::move(e0));
        std::vector<GroupToken> c1{{corners[2].id, false}, {corners[3].id, false}};
        std::vector<GroupToken> e1(edges.end() - 4, edges.end());
        for (auto& t : e1) t.orientation = false;
        group(std::move(c1), std::move(e1));
    }
    if (r.heuristic == PatternHeuristic::Strong) {
        bool down = true;
        for (int i = 4; i < 8; ++i) down &= corner_pos[i] >= 0 && edge_pos[i] >= 0;
        if (down) for (int first : {4, 6}) {
            std::vector<GroupToken> c{{static_cast<std::uint8_t>(first), false}, {static_cast<std::uint8_t>(first + 1), false}};
            std::vector<GroupToken> e{{4,false},{5,false},{6,false},{7,false}};
            group(std::move(c), std::move(e));
        }
    }

    // Check cache once for all scheduled bounds, then build misses in parallel.
    std::vector<std::size_t> misses;
    {
        std::lock_guard lock(cache_mutex);
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            if (auto found = bound_cache.find(jobs[i].key); found != bound_cache.end())
                jobs[i].ready = found->second;
            else
                misses.push_back(i);
        }
    }
    if (!misses.empty()) {
        std::atomic<int> abort_code{0};
        auto should_stop = [&]() -> bool {
            if (abort_code.load(std::memory_order_relaxed) != 0) return true;
            if (control.request.cancelled && control.request.cancelled()) {
                int expected = 0;
                abort_code.compare_exchange_strong(expected, 1, std::memory_order_relaxed);
                return true;
            }
            if (control.request.time_limit.count() > 0 && Clock::now() - control.start >= control.request.time_limit) {
                int expected = 0;
                abort_code.compare_exchange_strong(expected, 2, std::memory_order_relaxed);
                return true;
            }
            return false;
        };
        if (misses.size() == 1) {
            jobs[misses[0]].ready = jobs[misses[0]].build(should_stop);
        } else {
            const std::size_t workers = std::min<std::size_t>(misses.size(), std::min<std::size_t>(recommended_worker_count(false), 12));
            std::atomic<std::size_t> next_miss{0};
            std::vector<std::jthread> pool;
            pool.reserve(workers);
            for (std::size_t w = 0; w < workers; ++w) {
                pool.emplace_back([&]() {
                    apply_worker_thread_priority();
                    for (;;) {
                        if (abort_code.load(std::memory_order_relaxed) != 0) return;
                        const auto pos = next_miss.fetch_add(1, std::memory_order_relaxed);
                        if (pos >= misses.size()) return;
                        const auto idx = misses[pos];
                        jobs[idx].ready = jobs[idx].build(should_stop);
                    }
                });
            }
        }
        const int aborted = abort_code.load(std::memory_order_relaxed);
        if (aborted == 1) throw Abort{PatternStop::Cancelled};
        if (aborted == 2) throw Abort{PatternStop::TimeLimit};
        std::lock_guard lock(cache_mutex);
        const std::size_t budget = pattern_cache_budget_bytes();
        for (auto idx : misses) {
            if (!jobs[idx].ready) continue;
            if (auto found = bound_cache.find(jobs[idx].key); found != bound_cache.end()) {
                jobs[idx].ready = found->second;
                continue;
            }
            if (cache_entries + jobs[idx].ready->distance.size() > budget) { bound_cache.clear(); cache_entries = 0; }
            cache_entries += jobs[idx].ready->distance.size();
            bound_cache.emplace(jobs[idx].key, jobs[idx].ready);
        }
    }
    bounds.reserve(jobs.size());
    for (auto& job : jobs) {
        if (job.ready) {
            control.report.pruning_entries += job.ready->distance.size();
            bounds.push_back(std::move(job.ready));
        }
    }
    return bounds;
}

struct RotationPath { std::uint8_t frame; std::vector<Move> moves; };

std::array<std::vector<RotationPath>, 24> rotation_closures(std::span<const Move> allowed) {
    std::array<std::vector<RotationPath>, 24> closures;
    for (int start = 0; start < 24; ++start) {
        std::array<bool, 24> seen{}; seen[start] = true;
        auto& closure = closures[start]; closure.push_back({static_cast<std::uint8_t>(start), {}});
        for (std::size_t head = 0; head < closure.size(); ++head) {
            const auto here = closure[head];
            for (auto m : allowed) if (m.is_rotation()) {
                const auto next = rotated_frame(here.frame, m);
                if (seen[next]) continue;
                seen[next] = true;
                auto path = here.moves; path.push_back(m);
                closure.push_back({next, std::move(path)});
            }
        }
    }
    return closures;
}

std::vector<std::uint8_t> reachable_frames_from(std::span<const Move> allowed, std::uint8_t initial_frame) {
    std::array<bool, 24> seen{};
    std::vector<std::uint8_t> frames{initial_frame};
    seen[initial_frame] = true;
    for (std::size_t head = 0; head < frames.size(); ++head)
        for (auto m : allowed) {
            const auto next = move_transform(frames[head], m).next_frame;
            if (!seen[next]) { seen[next] = true; frames.push_back(next); }
        }
    return frames;
}

bool can_quotient_free_rotations(std::span<const Move> allowed, std::uint8_t initial_frame,
        const std::array<std::vector<RotationPath>, 24>& closures) {
    // Proves equivalence only of FUTURE EXISTENCE, not of formula words.
    // Checks over the actual reachable frame set from initial_frame (covering
    // both 24-frame multi-axis rotations and 4-frame single-axis rotations like RUF+y).
    // Goals ignore frame and there is no limit on the number of rotations.
    std::uint64_t profile = 0;
    for (auto m : allowed) profile |= std::uint64_t{1} << move_index(m);
    const std::uint64_t cache_key = profile ^ (static_cast<std::uint64_t>(initial_frame + 1) << 56);
    static std::mutex mutex;
    static std::unordered_map<std::uint64_t, bool> proved;
    { std::lock_guard lock(mutex); if (auto it = proved.find(cache_key); it != proved.end()) return it->second; }
    const auto frames = reachable_frames_from(allowed, initial_frame);
    std::unordered_set<std::string> effects;
    for (auto frame : frames) for (auto m : allowed) if (!m.is_rotation())
        effects.insert(transform_key(move_transform(frame, m)));
    bool result = !effects.empty();
    for (auto start : frames) {
        if (!result) break;
        std::unordered_set<std::string> reachable;
        for (const auto& rotation : closures[start]) if (!rotation.moves.empty())
            for (auto m : allowed) if (!m.is_rotation()) reachable.insert(transform_key(move_transform(rotation.frame, m)));
        result = reachable.size() == effects.size();
    }
    { std::lock_guard lock(mutex); proved.emplace(cache_key, result); }
    return result;
}

const std::array<std::array<bool, 54>, 54>& commute_table() {
    static const auto table = [] {
        std::array<std::array<bool, 54>, 54> t{};
        for (int a = 0; a < 54; ++a)
            for (int b = 0; b < 54; ++b)
                t[a][b] = cube::apply(cube::apply(CubeState{}, move_from_index(a)), move_from_index(b)) ==
                          cube::apply(cube::apply(CubeState{}, move_from_index(b)), move_from_index(a));
        return t;
    }();
    return table;
}

bool moves_commute(Move a, Move b) noexcept {
    return commute_table()[move_index(a)][move_index(b)];
}

struct WorkerHalt {};
struct TaskLocalCap {};

struct CursorState {
    std::array<std::uint16_t, 8> d{};
    std::array<std::uint16_t, 6> f_cp{};
    std::array<std::uint16_t, 6> f_ep{};
};

struct FastCursorTable {
    struct Direct {
        const Bound* bound = nullptr;
        const std::uint8_t* dist = nullptr;
        std::uint8_t orbit_count = 1;
        std::array<std::int8_t, 24> orbit_of_frame{};
        std::vector<std::uint16_t> next_base;
    };
    struct Factorized {
        const Bound* bound = nullptr;
        const std::uint8_t* dist = nullptr;
        std::uint8_t orbit_count = 1;
        std::array<std::int8_t, 24> orbit_of_frame{};
        std::uint32_t edge_count = 1;
        std::vector<std::uint16_t> next_cp;
        std::vector<std::uint16_t> next_ep;
    };
    std::uint32_t num_effects = 0;
    std::vector<Direct> direct;
    std::vector<Factorized> factorized;
    std::vector<std::shared_ptr<const Bound>> slow_bounds;

    CursorState make_cursor(const PatternState& state) const {
        CursorState cur;
        InversePieces inv;
        for (std::size_t i = 0; i < direct.size(); ++i)
            cur.d[i] = static_cast<std::uint16_t>(direct[i].bound->base_index(state, inv));
        for (std::size_t i = 0; i < factorized.size(); ++i) {
            const auto base = factorized[i].bound->base_index(state, inv);
            cur.f_cp[i] = static_cast<std::uint16_t>(base / factorized[i].edge_count);
            cur.f_ep[i] = static_cast<std::uint16_t>(base % factorized[i].edge_count);
        }
        return cur;
    }

    void build(const std::vector<std::shared_ptr<const Bound>>& bounds,
               const std::vector<MoveTransform>& forward_transforms,
               bool enable_cursors) {
        direct.clear();
        factorized.clear();
        slow_bounds.clear();
        num_effects = static_cast<std::uint32_t>(forward_transforms.size());
        if (!enable_cursors || num_effects == 0) {
            slow_bounds = bounds;
            return;
        }
        std::vector<AbstractMove> fwd_abs;
        fwd_abs.reserve(num_effects);
        for (const auto& t : forward_transforms) fwd_abs.push_back(abstract_move(t.delta, t.center_delta));

        for (const auto& b : bounds) {
            const std::size_t base_size = b->distance.size() / b->orbit_count;
            if (b->kind == Bound::Kind::Center && base_size <= 45000 && direct.size() < 8) {
                const auto* cb = static_cast<const CenterBound*>(b.get());
                Direct dc{cb, cb->distance.data(), cb->orbit_count, cb->orbit_of_frame, {}};
                dc.next_base.resize(base_size * num_effects);
                const std::size_t nf = cb->faces.size();
                std::array<std::uint8_t, 6> old{};
                for (std::size_t s = 0; s < base_size; ++s) {
                    auto tmp = static_cast<std::uint32_t>(s);
                    for (std::size_t p = nf; p-- > 0;) { old[p] = static_cast<std::uint8_t>(tmp & 3u); tmp >>= 2; }
                    for (std::uint32_t e = 0; e < num_effects; ++e) {
                        const auto& t = forward_transforms[e];
                        std::uint32_t nxt = 0;
                        for (std::size_t p = 0; p < nf; ++p)
                            nxt = (nxt << 2) | ((old[p] + t.center_delta[cb->faces[p]]) & 3u);
                        dc.next_base[s * num_effects + e] = static_cast<std::uint16_t>(nxt);
                    }
                }
                direct.push_back(std::move(dc));
                continue;
            }
            if (b->kind == Bound::Kind::Orientation && base_size <= 45000 && direct.size() < 8) {
                const auto* ob = static_cast<const OrientationBound*>(b.get());
                Direct dc{ob, ob->distance.data(), ob->orbit_count, ob->orbit_of_frame, {}};
                dc.next_base.resize(base_size * num_effects);
                std::array<std::uint8_t, 12> old{};
                for (std::size_t s = 0; s < base_size; ++s) {
                    auto tmp = static_cast<std::uint32_t>(s);
                    for (int p = ob->count - 1; p >= 0; --p) {
                        old[p] = static_cast<std::uint8_t>(tmp % ob->radix);
                        tmp /= ob->radix;
                    }
                    for (std::uint32_t e = 0; e < num_effects; ++e) {
                        const auto& t = forward_transforms[e];
                        std::uint32_t nxt = 0;
                        for (int i = 0; i < ob->count; ++i) {
                            const int src = ob->corner ? t.delta.cp[i] : t.delta.ep[i];
                            const int delta_o = ob->corner ? t.delta.co[i] : t.delta.eo[i];
                            const int v = old[src] == ob->base ? ob->base : (old[src] + delta_o) % ob->base;
                            nxt = nxt * ob->radix + static_cast<std::uint32_t>(v);
                        }
                        dc.next_base[s * num_effects + e] = static_cast<std::uint16_t>(nxt);
                    }
                }
                direct.push_back(std::move(dc));
                continue;
            }
            if (b->kind == Bound::Kind::Group) {
                const auto* gb = static_cast<const GroupBound*>(b.get());
                if (base_size <= 45000 && direct.size() < 8) {
                    Direct dc{gb, gb->distance.data(), gb->orbit_count, gb->orbit_of_frame, {}};
                    dc.next_base.resize(base_size * num_effects);
                    for (std::size_t s = 0; s < base_size; ++s) {
                        const auto old = gb->unrank(static_cast<std::uint32_t>(s));
                        for (std::uint32_t e = 0; e < num_effects; ++e) {
                            const auto& fm = fwd_abs[e];
                            GroupBound::Values nxt;
                            for (std::size_t i = 0; i < gb->corners.size(); ++i) {
                                const auto p = old.cp[i];
                                nxt.cp[i] = fm.corner_dest[p];
                                if (gb->corners[i].orientation) {
                                    const auto o = static_cast<std::uint8_t>(old.co[i] + fm.corner_ori[p]);
                                    nxt.co[i] = o >= 3 ? static_cast<std::uint8_t>(o - 3) : o;
                                }
                            }
                            for (std::size_t i = 0; i < gb->edges.size(); ++i) {
                                const auto p = old.ep[i];
                                nxt.ep[i] = fm.edge_dest[p];
                                if (gb->edges[i].orientation)
                                    nxt.eo[i] = static_cast<std::uint8_t>((old.eo[i] + fm.edge_ori[p]) & 1u);
                            }
                            dc.next_base[s * num_effects + e] = static_cast<std::uint16_t>(gb->rank(nxt));
                        }
                    }
                    direct.push_back(std::move(dc));
                    continue;
                }
                const std::size_t c_cnt = permutations(8, static_cast<int>(gb->corners.size()));
                const std::size_t e_cnt = gb->edge_count;
                if (gb->orientation_count == 1 && !gb->corners.empty() && !gb->edges.empty() &&
                    c_cnt <= 45000 && e_cnt <= 45000 && factorized.size() < 6) {
                    Factorized fc{gb, gb->distance.data(), gb->orbit_count, gb->orbit_of_frame, gb->edge_count, {}, {}};
                    fc.next_cp.resize(c_cnt * num_effects);
                    fc.next_ep.resize(e_cnt * num_effects);
                    const int clen = static_cast<int>(gb->corners.size());
                    const int elen = static_cast<int>(gb->edges.size());
                    std::array<std::uint8_t, 8> cp_old{}, cp_nxt{};
                    for (std::size_t s = 0; s < c_cnt; ++s) {
                        unrank_positions(static_cast<std::uint32_t>(s), cp_old.data(), clen, 8);
                        for (std::uint32_t e = 0; e < num_effects; ++e) {
                            const auto& fm = fwd_abs[e];
                            for (int i = 0; i < clen; ++i) cp_nxt[i] = fm.corner_dest[cp_old[i]];
                            fc.next_cp[s * num_effects + e] = static_cast<std::uint16_t>(rank_positions(cp_nxt.data(), clen, 8));
                        }
                    }
                    std::array<std::uint8_t, 12> ep_old{}, ep_nxt{};
                    for (std::size_t s = 0; s < e_cnt; ++s) {
                        unrank_positions(static_cast<std::uint32_t>(s), ep_old.data(), elen, 12);
                        for (std::uint32_t e = 0; e < num_effects; ++e) {
                            const auto& fm = fwd_abs[e];
                            for (int i = 0; i < elen; ++i) ep_nxt[i] = fm.edge_dest[ep_old[i]];
                            fc.next_ep[s * num_effects + e] = static_cast<std::uint16_t>(rank_positions(ep_nxt.data(), elen, 12));
                        }
                    }
                    factorized.push_back(std::move(fc));
                    continue;
                }
            }
            slow_bounds.push_back(b);
        }
    }
};

struct Search {
    struct WorkerContext {
        std::atomic<std::uint64_t>& global_nodes;
        std::atomic<int>& stop_code;
        std::function<void()> maybe_progress;
        std::vector<PatternSolution> local_solutions;
        std::uint64_t local_nodes = 0;
        std::uint64_t flushed_nodes = 0;
    };
    const PatternRequest& request;
    PatternReport& report;
    Control& control;
    const std::vector<PatternState>& goals;
    std::vector<std::shared_ptr<const Bound>> bounds;
    std::shared_ptr<FastCursorTable> fast_table = std::make_shared<FastCursorTable>();
    std::array<std::vector<RotationPath>, 24> rotations;
    std::vector<RotationPath> root_rotations;
    struct PaidTransition {
        Move move;
        const MoveTransform* transform;
        std::uint32_t effect_id = 0;
        std::uint8_t symbol_idx = 0;
    };
    std::array<std::vector<PaidTransition>, 24> paid_transitions;
    PatternSolutionCallback callback;
    std::vector<Move> path, paid_moves;
    std::array<bool, 4> redundant_root_u{};
    std::array<bool, 54> allowed{};
    struct DeadEntry {
        std::uint64_t hash = 0;
        std::array<std::uint8_t, 64> key{};
        bool occupied = false;
    };
    std::vector<DeadEntry> dead_ends;
    bool quotient_rotations = false;
    std::vector<MoveTransform> canonical_effects;
    bool prove_minimum = false;
    int first_solution_depth = -1;
    std::vector<std::uint8_t> constrained_symbols;
    int used_rotations = 0;
    std::array<int, 18> used_symbols{};
    bool can_memoize = false;
    Face target_d_ = Face::D;
    WorkerContext* worker_ = nullptr;

    Search(const PatternRequest& r, PatternReport& result, Control& ctl, const std::vector<PatternState>& targets)
        : request(r), report(result), control(ctl), goals(targets),
          target_d_(logical_face(r.pattern.frame, Face::D)) {
        for (std::uint8_t s = 0; s < 18; ++s)
            if (r.max_symbol_uses[s] >= 0) constrained_symbols.push_back(s);
        can_memoize = r.memoize_dead_ends && constrained_symbols.size() <= 8;
        if (can_memoize) dead_ends.resize(1u << 18);
    }

    // Worker clone: shares read-only tables/bounds, owns thread-local path & dead_ends.
    Search(const Search& proto, WorkerContext& worker)
        : request(proto.request), report(proto.report), control(proto.control), goals(proto.goals),
          bounds(proto.bounds), fast_table(proto.fast_table), rotations(proto.rotations),
          root_rotations(proto.root_rotations), paid_transitions(proto.paid_transitions),
          paid_moves(proto.paid_moves), redundant_root_u(proto.redundant_root_u), allowed(proto.allowed),
          quotient_rotations(proto.quotient_rotations), canonical_effects(proto.canonical_effects),
          prove_minimum(proto.prove_minimum), constrained_symbols(proto.constrained_symbols),
          can_memoize(proto.can_memoize), target_d_(proto.target_d_), worker_(&worker) {
        if (can_memoize) dead_ends.resize(1u << 16);
    }

    int match_goal(const PatternState& state) const {
        return goal_index(state, goals, request.require_d_on_d, target_d_);
    }

    DeadEntry dead_key(const PatternState& state, int remaining, bool root) const {
        DeadEntry entry; entry.occupied = true;
        std::size_t pos = 0;
        for (auto a : {std::span<const std::int8_t>(state.cp), std::span<const std::int8_t>(state.co),
            std::span<const std::int8_t>(state.ep), std::span<const std::int8_t>(state.eo), std::span<const std::int8_t>(state.center_ori)})
            for (auto v : a) entry.key[pos++] = static_cast<std::uint8_t>(v + 1);
        const bool quotient = quotient_rotations && !root;
        entry.key[pos++] = quotient ? 0 : state.frame;
        for (int byte = 0; byte < 4; ++byte)
            entry.key[pos++] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(remaining) >> (byte * 8)) & 255);
        if (!quotient && !path.empty() && !path.back().is_rotation()) {
            const auto last = path.back();
            entry.key[pos] = static_cast<std::uint8_t>(move_names.find(last.symbol) + 1);
            for (int k = 1; k <= 3; ++k) if (redundant_same_symbol(Move{last.symbol, static_cast<std::uint8_t>(k)}))
                entry.key[pos + 1] |= static_cast<std::uint8_t>(1u << k);
            if (path.size() >= 2) {
                const auto previous = path[path.size() - 2];
                const bool alt = request.reject_alternating_triples && violates_alternating_triple(path, previous);
                const bool comm_ban = request.forbid_consecutive_same_symbol && !previous.is_rotation() &&
                                      previous.symbol != last.symbol && moves_commute(previous, last);
                if (alt || comm_ban) entry.key[pos + 2] = static_cast<std::uint8_t>(move_index(previous) + 1);
            }
        }
        pos += 3;
        entry.key[pos++] = static_cast<std::uint8_t>(root);
        // Pack remaining rotation & constrained symbol budgets so dead-end memoization stays 100% exact.
        const int rem_rot = request.max_rotations < 0 ? 255 : std::min(254, std::max(0, request.max_rotations - used_rotations));
        entry.key[pos++] = static_cast<std::uint8_t>(rem_rot);
        for (auto s : constrained_symbols) {
            const int rem_sym = std::min(255, std::max(0, request.max_symbol_uses[s] - used_symbols[s]));
            entry.key[pos++] = static_cast<std::uint8_t>(rem_sym);
        }
        entry.hash = 1469598103934665603ull;
        for (auto v : entry.key) { entry.hash ^= v; entry.hash *= 1099511628211ull; }
        return entry;
    }

    int lower_bound(const PatternState& state, int cutoff = std::numeric_limits<int>::max()) const {
        int result = 0;
        InversePieces inv;
        for (const auto& bound : bounds) {
            const int distance = bound->eval(state, inv);
            if (distance == 255) return std::numeric_limits<int>::max();
            result = std::max(result, distance);
            if (result > cutoff) return result;
        }
        return result;
    }

    int root_lower_bound(const PatternState& state, int cutoff = std::numeric_limits<int>::max()) const {
        if (root_rotations.size() <= 1) return lower_bound(state, cutoff);
        int best = std::numeric_limits<int>::max();
        for (const auto& rotation : root_rotations) {
            if (!rotation_within_limits(rotation)) continue;
            auto oriented = state;
            oriented.frame = rotation.frame;
            best = std::min(best, lower_bound(oriented, cutoff));
            if (best == 0) break;
        }
        return best;
    }

    void check_control() {
        if (!worker_) { control.check(); return; }
        if (worker_->stop_code.load(std::memory_order_relaxed) != 0) throw WorkerHalt{};
        if (request.cancelled && request.cancelled()) {
            int expected = 0;
            worker_->stop_code.compare_exchange_strong(expected, 1, std::memory_order_relaxed);
            throw WorkerHalt{};
        }
        if (request.time_limit.count() > 0 && Clock::now() - control.start >= request.time_limit) {
            int expected = 0;
            worker_->stop_code.compare_exchange_strong(expected, 2, std::memory_order_relaxed);
            throw WorkerHalt{};
        }
    }

    void count_node() {
        if (!worker_) {
            if (request.max_nodes && report.nodes >= request.max_nodes) throw Abort{PatternStop::NodeLimit};
            ++report.nodes;
            if ((report.nodes & 1023) == 0 || (request.max_nodes && report.nodes >= request.max_nodes)) control.check();
            return;
        }
        ++worker_->local_nodes;
        if ((worker_->local_nodes & 1023) == 0) {
            const auto added = worker_->local_nodes - worker_->flushed_nodes;
            worker_->flushed_nodes = worker_->local_nodes;
            const auto total = worker_->global_nodes.fetch_add(added, std::memory_order_relaxed) + added;
            if (worker_->stop_code.load(std::memory_order_relaxed) != 0) throw WorkerHalt{};
            if (request.max_nodes && total >= request.max_nodes) {
                int expected = 0;
                worker_->stop_code.compare_exchange_strong(expected, 3, std::memory_order_relaxed);
                throw WorkerHalt{};
            }
            check_control();
            if (worker_->maybe_progress) worker_->maybe_progress();
        }
    }

    static PatternState apply_effect(const PatternState& state, const MoveTransform& t) {
        PatternState next;
        for (int i = 0; i < 8; ++i) {
            next.cp[i] = state.cp[t.delta.cp[i]];
            const int o = state.co[t.delta.cp[i]];
            if (o < 0) next.co[i] = -1;
            else {
                const int s = o + t.delta.co[i];
                next.co[i] = static_cast<std::int8_t>(s >= 3 ? s - 3 : s);
            }
        }
        for (int i = 0; i < 12; ++i) {
            next.ep[i] = state.ep[t.delta.ep[i]];
            const int o = state.eo[t.delta.ep[i]];
            next.eo[i] = static_cast<std::int8_t>(o < 0 ? -1 : ((o + t.delta.eo[i]) & 1));
        }
        for (int i = 0; i < 6; ++i) {
            const int o = state.center_ori[i];
            next.center_ori[i] = static_cast<std::int8_t>(o < 0 ? -1 : ((o + t.center_delta[i]) & 3));
        }
        return next;
    }

    static void apply_effect_with_inv(const PatternState& state, const MoveTransform& t,
                                      PatternState& next, InversePieces& inv) noexcept {
        inv.has_cp = true;
        inv.has_ep = true;
        for (int i = 0; i < 8; ++i) {
            const auto id = state.cp[t.delta.cp[i]];
            next.cp[i] = id;
            if (id >= 0) inv.cp[id] = static_cast<std::uint8_t>(i);
            const int o = state.co[t.delta.cp[i]];
            if (o < 0) next.co[i] = -1;
            else {
                const int s = o + t.delta.co[i];
                next.co[i] = static_cast<std::int8_t>(s >= 3 ? s - 3 : s);
            }
        }
        for (int i = 0; i < 12; ++i) {
            const auto id = state.ep[t.delta.ep[i]];
            next.ep[i] = id;
            if (id >= 0) inv.ep[id] = static_cast<std::uint8_t>(i);
            const int o = state.eo[t.delta.ep[i]];
            next.eo[i] = static_cast<std::int8_t>(o < 0 ? -1 : ((o + t.delta.eo[i]) & 1));
        }
        for (int i = 0; i < 6; ++i) {
            const int o = state.center_ori[i];
            next.center_ori[i] = static_cast<std::int8_t>(o < 0 ? -1 : ((o + t.center_delta[i]) & 3));
        }
    }

    bool rotation_within_limits(const RotationPath& rotation) const noexcept {
        if (request.max_rotations >= 0 && used_rotations + static_cast<int>(rotation.moves.size()) > request.max_rotations)
            return false;
        if (!constrained_symbols.empty() && !rotation.moves.empty()) {
            std::array<int, 3> extra{};
            for (auto rm : rotation.moves) {
                const int idx = static_cast<int>(move_names.find(rm.symbol));
                if (idx >= 15 && idx < 18) ++extra[idx - 15];
            }
            for (int k = 0; k < 3; ++k)
                if (extra[k] > 0 && request.max_symbol_uses[15 + k] >= 0 &&
                    used_symbols[15 + k] + extra[k] > request.max_symbol_uses[15 + k])
                    return false;
        }
        return true;
    }

    void push_rotation_usage(const RotationPath& rotation) noexcept {
        used_rotations += static_cast<int>(rotation.moves.size());
        for (auto rm : rotation.moves) {
            const int idx = static_cast<int>(move_names.find(rm.symbol));
            if (idx >= 15 && idx < 18) ++used_symbols[idx];
        }
    }

    void pop_rotation_usage(const RotationPath& rotation) noexcept {
        used_rotations -= static_cast<int>(rotation.moves.size());
        for (auto rm : rotation.moves) {
            const int idx = static_cast<int>(move_names.find(rm.symbol));
            if (idx >= 15 && idx < 18) --used_symbols[idx];
        }
    }

    // Advance O(1) incremental cursors BEFORE apply_effect; returns false if pruned!
    bool advance_fast_cursors(const CursorState& cur, std::uint32_t eff, std::uint8_t next_frame,
                              int next_rem, CursorState& next_cur) const noexcept {
        const auto& ft = *fast_table;
        const std::size_t ne = ft.num_effects;
        for (std::size_t i = 0; i < ft.direct.size(); ++i) {
            const auto& dc = ft.direct[i];
            const auto nb = dc.next_base[static_cast<std::size_t>(cur.d[i]) * ne + eff];
            const int dist = dc.dist[dc.orbit_count == 1 ? nb
                                                         : static_cast<std::size_t>(nb) * dc.orbit_count +
                                                               static_cast<std::uint32_t>(dc.orbit_of_frame[next_frame])];
            if (dist > next_rem) return false;
            next_cur.d[i] = nb;
        }
        for (std::size_t i = 0; i < ft.factorized.size(); ++i) {
            const auto& fc = ft.factorized[i];
            const auto ncp = fc.next_cp[static_cast<std::size_t>(cur.f_cp[i]) * ne + eff];
            const auto nep = fc.next_ep[static_cast<std::size_t>(cur.f_ep[i]) * ne + eff];
            const auto nb = static_cast<std::uint32_t>(ncp) * fc.edge_count + nep;
            const int dist = fc.dist[fc.orbit_count == 1 ? nb
                                                         : static_cast<std::size_t>(nb) * fc.orbit_count +
                                                               static_cast<std::uint32_t>(fc.orbit_of_frame[next_frame])];
            if (dist > next_rem) return false;
            next_cur.f_cp[i] = ncp;
            next_cur.f_ep[i] = nep;
        }
        return true;
    }

    bool slow_bounds_pass(const PatternState& child, InversePieces& inv, int next_rem) const {
        for (const auto& sb : fast_table->slow_bounds) {
            if (sb->eval(child, inv) > next_rem) return false;
        }
        return true;
    }

    bool canonical_completion_exists(const PatternState& state, int remaining) {
        count_node();
        if (goal_index(state, goals) >= 0) return remaining == 0;
        if (remaining == 0 || lower_bound(state, remaining) > remaining) return false;
        const auto key = dead_key(state, remaining, false);
        const auto& slot = dead_ends[key.hash & (dead_ends.size() - 1)];
        if (slot.occupied && slot.hash == key.hash && slot.key == key.key) return false;
        for (const auto& effect : canonical_effects)
            if (canonical_completion_exists(apply_effect(state, effect), remaining - 1)) return true;
        // The mechanical free-rotation proof makes this an exact existence
        // search, rather than an optimistic heuristic. A failed complete probe
        // is a failed concrete subtree. Positive probes never emit or collapse
        // formulas; the original notation search still enumerates them.
        dead_ends[key.hash & (dead_ends.size() - 1)] = key;
        return false;
    }

    bool redundant_same_symbol(std::span<const Move> prefix, Move next) const {
        if (prefix.empty() || prefix.back().symbol != next.symbol) return false;
        if (request.forbid_consecutive_same_symbol) return true;
        const auto combined = static_cast<std::uint8_t>((prefix.back().power + next.power) % 4);
        return combined == 0 || allowed[move_index(Move{next.symbol, combined})];
    }
    bool redundant_same_symbol(Move next) const { return redundant_same_symbol(path, next); }

    bool violates_banned_commuting_triple(std::span<const Move> prefix, Move next) const {
        if (!request.forbid_consecutive_same_symbol || prefix.size() < 2 || next.is_rotation()) return false;
        const auto a = prefix[prefix.size() - 2];
        const auto b = prefix.back();
        if (a.is_rotation() || b.is_rotation() || a.symbol != next.symbol || b.symbol == next.symbol) return false;
        if (!moves_commute(a, b)) return false;
        const auto combined = static_cast<std::uint8_t>((a.power + next.power) % 4);
        return combined == 0 || !allowed[move_index(Move{next.symbol, combined})];
    }

    bool canonical_trace() {
        if (!request.deduplicate_commuting || path.empty()) return true;
        const auto& commutes = commute_table();
        // A history set grows forever in an unbounded stream. Instead choose
        // exactly one admissible topological ordering of this trace's
        // non-commutation graph. Grammar and first-goal checks are essential:
        // the unconstrained lexical normal form might contain R r R, mergeable
        // neighbours, or reach the modulo-U goal before its last action.
        const auto size = path.size();
        std::vector<int> ids; ids.reserve(size);
        for (auto m : path) ids.push_back(move_index(m));
        std::vector<std::size_t> order(size);
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return ids[a] < ids[b]; });
        std::vector<std::vector<std::size_t>> predecessors(size);
        for (std::size_t i = 0; i < size; ++i) for (std::size_t j = 0; j < i; ++j)
            if (!commutes[ids[j]][ids[i]]) predecessors[i].push_back(j);
        std::vector<bool> used(size);
        std::vector<Move> candidate; candidate.reserve(size);
        std::uint64_t probes = 0;
        const auto start = initial_state(request);
        auto first = [&](auto&& self, const PatternState& state, int paid,
                std::size_t rotation_begin, std::uint8_t rotation_frame) -> bool {
            if ((++probes & 1023) == 0) check_control();
            if (candidate.size() == size) return !candidate.back().is_rotation() && match_goal(state) >= 0;
            int previous_id = -1;
            for (auto i : order) {
                if (used[i] || ids[i] == previous_id) continue;
                previous_id = ids[i]; // Equal occurrences are interchangeable.
                if (std::any_of(predecessors[i].begin(), predecessors[i].end(), [&](auto p) { return !used[p]; })) continue;
                const auto move = path[i];
                if (!move.is_rotation()) {
                    const auto segment = std::span<const Move>(candidate).subspan(rotation_begin);
                    bool valid_segment = false;
                    const auto& rot_list = rotation_begin == 0 ? root_rotations : rotations[rotation_frame];
                    for (const auto& rotation : rot_list)
                        if (std::ranges::equal(segment, rotation.moves)) { valid_segment = true; break; }
                    if (!valid_segment || redundant_same_symbol(candidate, move)) continue;
                    if (violates_banned_commuting_triple(candidate, move)) continue;
                    if (request.reject_alternating_triples && violates_alternating_triple(candidate, move)) continue;
                    if (paid == 0 && move.symbol == 'U' && redundant_root_u[move.power] && logical_face(state.frame, Face::U) == Face::U) continue;
                }
                candidate.push_back(move);
                bool valid = true;
                if (move.is_rotation()) {
                    const auto segment = std::span<const Move>(candidate).subspan(rotation_begin);
                    valid = false;
                    const auto& rot_list = rotation_begin == 0 ? root_rotations : rotations[rotation_frame];
                    for (const auto& rotation : rot_list)
                        if (rotation.moves.size() >= segment.size() &&
                            std::equal(segment.begin(), segment.end(), rotation.moves.begin())) { valid = true; break; }
                }
                const auto next = apply_pattern(state, move);
                if (!move.is_rotation() && candidate.size() < size && match_goal(next) >= 0) valid = false;
                if (valid) {
                    used[i] = true;
                    if (self(self, next, paid + move.turn_cost(), move.is_rotation() ? rotation_begin : candidate.size(),
                            move.is_rotation() ? rotation_frame : next.frame)) return true;
                    used[i] = false;
                }
                candidate.pop_back();
            }
            return false;
        };
        if (!first(first, start, 0, 0, start.frame))
            throw std::logic_error("An emitted trace has no admissible representative.");
        return candidate == path;
    }

    void accept(const PatternState& state, int turns, int residual) {
        if (!canonical_trace()) return;
        PatternSolution solution{path, turns, cube::rotation_count(path), residual, state, false};
        const auto verification = verify_pattern_solution(request, solution);
        if (!verification) throw std::logic_error("Independent pattern result verification failed: " + verification.message);
        solution.verified = true;
        if (worker_) {
            worker_->local_solutions.push_back(std::move(solution));
            if (request.max_results && worker_->local_solutions.size() >= request.max_results)
                throw TaskLocalCap{};
            return;
        }
        if (first_solution_depth < 0) first_solution_depth = turns;
        solution.optimality_proven = prove_minimum && turns == first_solution_depth;
        ++report.results;
        if (request.collect_solutions) report.solutions.push_back(solution);
        if (callback) callback(solution);
        control.check();
        if (request.max_results && report.results >= request.max_results) throw Abort{PatternStop::ResultLimit};
    }

    bool visit(const PatternState& state, const CursorState& cursor, int turns, int bound) {
        count_node();
        const int goal = match_goal(state);
        if (goal >= 0) {
            // A goal is terminal even below min_turns: never leave a solved
            // (or modulo-U solved) state just to pad an enumerated formula.
            if (turns == bound && turns >= request.min_turns) { accept(state, turns, goal); return true; }
            return false;
        }
        const int remaining = bound - turns;
        if (turns >= bound) return false;
        if (turns == 0 && root_lower_bound(state, remaining) > remaining) return false;
        const bool use_dead = !dead_ends.empty() && remaining >= 2;
        DeadEntry key;
        if (use_dead) {
            key = dead_key(state, remaining, turns == 0);
            const auto& slot = dead_ends[key.hash & (dead_ends.size() - 1)];
            if (slot.occupied && slot.hash == key.hash && slot.key == key.key) return false;
        }
        if (quotient_rotations && turns > 0 && !canonical_completion_exists(state, remaining)) return false;
        const int next_rem = remaining - 1;
        bool has_completion = false;
        const auto& active_rotations = turns == 0 ? root_rotations : rotations[state.frame];
        for (const auto& rotation : active_rotations) {
            if (!rotation_within_limits(rotation)) continue;
            const auto old_size = path.size();
            path.insert(path.end(), rotation.moves.begin(), rotation.moves.end());
            push_rotation_usage(rotation);
            auto oriented = state;
            // Pure whole-cube rotations change only the holding frame in the
            // logical-center model. Base coordinates in cursor are unchanged!
            oriented.frame = rotation.frame;
            for (const auto& transition : paid_transitions[oriented.frame]) {
                const auto sym = transition.symbol_idx;
                if (request.max_symbol_uses[sym] >= 0 && used_symbols[sym] >= request.max_symbol_uses[sym]) continue;
                const auto m = transition.move;
                if (redundant_same_symbol(m)) continue;
                if (violates_banned_commuting_triple(path, m)) continue;
                if (request.reject_alternating_triples && violates_alternating_triple(path, m)) continue;
                if (turns == 0 && redundant_root_u[m.power] && m.symbol == 'U' && logical_face(oriented.frame, Face::U) == Face::U) continue;
                CursorState next_cur;
                if (!advance_fast_cursors(cursor, transition.effect_id, transition.transform->next_frame, next_rem, next_cur))
                    continue;
                PatternState child;
                InversePieces inv;
                apply_effect_with_inv(oriented, *transition.transform, child, inv);
                child.frame = transition.transform->next_frame;
                if (!slow_bounds_pass(child, inv, next_rem)) continue;
                path.push_back(m);
                ++used_symbols[sym];
                has_completion |= visit(child, next_cur, turns + 1, bound);
                --used_symbols[sym];
                path.pop_back();
            }
            pop_rotation_usage(rotation);
            path.resize(old_size);
        }
        // A positive subtree is never memoized: two different formula prefixes
        // may lead to the same state and must retain their different results.
        // Aborted subtrees never reach this line. Hash collisions are checked
        // against the complete state/context key before any pruning occurs.
        if (!has_completion && use_dead) dead_ends[key.hash & (dead_ends.size() - 1)] = key;
        return has_completion;
    }

    void run_depth_parallel(const PatternState& start, const CursorState& root_cursor, int depth) {
        struct FrontierTask {
            std::vector<Move> prefix;
            PatternState state;
            CursorState cursor;
            int turns = 0;
            int used_rot = 0;
            std::array<int, 18> used_sym{};
        };
        const int frontier_depth = (root_rotations.size() > 1) ? 1 : 2;
        std::vector<FrontierTask> tasks;
        count_node();
        for (const auto& rotation : root_rotations) {
            if (!rotation_within_limits(rotation)) continue;
            push_rotation_usage(rotation);
            auto oriented = start;
            oriented.frame = rotation.frame;
            for (const auto& transition : paid_transitions[oriented.frame]) {
                const auto sym = transition.symbol_idx;
                if (request.max_symbol_uses[sym] >= 0 && used_symbols[sym] >= request.max_symbol_uses[sym]) continue;
                const auto m = transition.move;
                if (redundant_same_symbol(rotation.moves, m)) continue;
                if (request.reject_alternating_triples && violates_alternating_triple(rotation.moves, m)) continue;
                if (redundant_root_u[m.power] && m.symbol == 'U' && logical_face(oriented.frame, Face::U) == Face::U) continue;
                CursorState cur1;
                if (!advance_fast_cursors(root_cursor, transition.effect_id, transition.transform->next_frame, depth - 1, cur1))
                    continue;
                PatternState child1;
                InversePieces inv1;
                apply_effect_with_inv(oriented, *transition.transform, child1, inv1);
                child1.frame = transition.transform->next_frame;
                if (!slow_bounds_pass(child1, inv1, depth - 1)) continue;
                auto prefix1 = rotation.moves;
                prefix1.push_back(m);
                ++used_symbols[sym];
                if (frontier_depth == 1) {
                    tasks.push_back({std::move(prefix1), child1, cur1, 1, used_rotations, used_symbols});
                    --used_symbols[sym];
                    continue;
                }
                count_node();
                if (match_goal(child1) < 0) {
                    for (const auto& rot1 : rotations[child1.frame]) {
                        if (!rotation_within_limits(rot1)) continue;
                        push_rotation_usage(rot1);
                        auto oriented1 = child1;
                        oriented1.frame = rot1.frame;
                        auto prefix1r = prefix1;
                        prefix1r.insert(prefix1r.end(), rot1.moves.begin(), rot1.moves.end());
                        for (const auto& t2 : paid_transitions[oriented1.frame]) {
                            const auto sym2 = t2.symbol_idx;
                            if (request.max_symbol_uses[sym2] >= 0 && used_symbols[sym2] >= request.max_symbol_uses[sym2]) continue;
                            const auto m2 = t2.move;
                            if (redundant_same_symbol(prefix1r, m2)) continue;
                            if (violates_banned_commuting_triple(prefix1r, m2)) continue;
                            if (request.reject_alternating_triples && violates_alternating_triple(prefix1r, m2)) continue;
                            CursorState cur2;
                            if (!advance_fast_cursors(cur1, t2.effect_id, t2.transform->next_frame, depth - 2, cur2))
                                continue;
                            PatternState child2;
                            InversePieces inv2;
                            apply_effect_with_inv(oriented1, *t2.transform, child2, inv2);
                            child2.frame = t2.transform->next_frame;
                            if (!slow_bounds_pass(child2, inv2, depth - 2)) continue;
                            auto prefix2 = prefix1r;
                            prefix2.push_back(m2);
                            ++used_symbols[sym2];
                            tasks.push_back({std::move(prefix2), child2, cur2, 2, used_rotations, used_symbols});
                            --used_symbols[sym2];
                        }
                        pop_rotation_usage(rot1);
                    }
                }
                --used_symbols[sym];
            }
            pop_rotation_usage(rotation);
        }
        if (tasks.empty()) return;
        if (tasks.size() == 1) {
            path = std::move(tasks[0].prefix);
            used_rotations = tasks[0].used_rot;
            used_symbols = tasks[0].used_sym;
            visit(tasks[0].state, tasks[0].cursor, tasks[0].turns, depth);
            path.clear();
            used_rotations = 0;
            used_symbols.fill(0);
            return;
        }
        struct TaskSlot {
            std::vector<PatternSolution> solutions;
            bool done = false;
        };
        std::vector<TaskSlot> slots(tasks.size());
        std::atomic<std::size_t> next_task{0};
        std::atomic<std::uint64_t> global_nodes{report.nodes};
        std::atomic<int> stop_code{0}; // 1=Cancel, 2=Time, 3=Node, 4=Result, 5=Error
        std::mutex flush_mutex;
        std::size_t next_flush = 0;
        std::string worker_error;

        auto flush_ready = [&]() {
            while (next_flush < slots.size() && slots[next_flush].done &&
                   stop_code.load(std::memory_order_relaxed) == 0) {
                auto& sols = slots[next_flush].solutions;
                for (auto& sol : sols) {
                    if (first_solution_depth < 0) first_solution_depth = sol.turn_cost;
                    sol.optimality_proven = prove_minimum && sol.turn_cost == first_solution_depth;
                    ++report.results;
                    if (request.collect_solutions) report.solutions.push_back(sol);
                    if (callback) callback(sol);
                    if (request.max_results && report.results >= request.max_results) {
                        int expected = 0;
                        stop_code.compare_exchange_strong(expected, 4, std::memory_order_relaxed);
                        break;
                    }
                    if (request.cancelled && request.cancelled()) {
                        int expected = 0;
                        stop_code.compare_exchange_strong(expected, 1, std::memory_order_relaxed);
                        break;
                    }
                    if (request.time_limit.count() > 0 && Clock::now() - control.start >= request.time_limit) {
                        int expected = 0;
                        stop_code.compare_exchange_strong(expected, 2, std::memory_order_relaxed);
                        break;
                    }
                }
                sols.clear();
                ++next_flush;
            }
        };
        auto maybe_progress = [&]() {
            if (!control.progress) return;
            const auto now = Clock::now();
            if (now - control.last_progress < std::chrono::milliseconds(200)) return;
            std::unique_lock lock(flush_mutex, std::try_to_lock);
            if (!lock.owns_lock()) return;
            const auto now2 = Clock::now();
            if (now2 - control.last_progress >= std::chrono::milliseconds(200)) {
                control.last_progress = now2;
                control.progress({global_nodes.load(std::memory_order_relaxed), depth, report.results,
                    std::chrono::duration_cast<std::chrono::milliseconds>(now2 - control.start), false, false});
            }
        };

        const std::size_t max_workers = recommended_worker_count(false);
        const std::size_t worker_count = std::min<std::size_t>(tasks.size(), max_workers);
        {
            std::vector<std::jthread> pool;
            pool.reserve(worker_count);
            for (std::size_t w = 0; w < worker_count; ++w) {
                pool.emplace_back([&]() {
                    apply_worker_thread_priority();
                    WorkerContext wctx{global_nodes, stop_code, maybe_progress, {}, 0, 0};
                    Search worker(*this, wctx);
                    for (;;) {
                        if (stop_code.load(std::memory_order_relaxed) != 0) break;
                        const auto idx = next_task.fetch_add(1, std::memory_order_relaxed);
                        if (idx >= tasks.size()) break;
                        wctx.local_solutions.clear();
                        worker.path = tasks[idx].prefix;
                        worker.used_rotations = tasks[idx].used_rot;
                        worker.used_symbols = tasks[idx].used_sym;
                        bool completed = true;
                        try {
                            worker.visit(tasks[idx].state, tasks[idx].cursor, tasks[idx].turns, depth);
                        } catch (const TaskLocalCap&) {
                            // This single task alone already reached request.max_results.
                        } catch (const WorkerHalt&) {
                            completed = false;
                        } catch (const std::exception& e) {
                            completed = false;
                            std::lock_guard lock(flush_mutex);
                            if (worker_error.empty()) worker_error = e.what();
                            stop_code.store(5, std::memory_order_relaxed);
                        }
                        const auto rem = wctx.local_nodes - wctx.flushed_nodes;
                        if (rem > 0) {
                            wctx.flushed_nodes = wctx.local_nodes;
                            global_nodes.fetch_add(rem, std::memory_order_relaxed);
                        }
                        if (!completed) break;
                        std::lock_guard lock(flush_mutex);
                        slots[idx].solutions = std::move(wctx.local_solutions);
                        slots[idx].done = true;
                        flush_ready();
                    }
                });
            }
        }
        report.nodes = global_nodes.load(std::memory_order_relaxed);
        const int stopped = stop_code.load(std::memory_order_relaxed);
        if (stopped == 1) throw Abort{PatternStop::Cancelled};
        if (stopped == 2) throw Abort{PatternStop::TimeLimit};
        if (stopped == 3) throw Abort{PatternStop::NodeLimit};
        if (stopped == 4) throw Abort{PatternStop::ResultLimit};
        if (stopped == 5) throw std::logic_error(worker_error);
    }
};

// Independent verifier data: every sticker/arrow permutation below is built
// by the physical geometry simulator, never by the search's cubie move table.
// Precomputing these fixed maps retains per-result/per-prefix verification
// without repeating 3D rotations and a 24-frame scan for every output action.
struct VerificationGeometry {
    struct Piece { std::int8_t id = -1, orientation = 0; };
    struct Transform {
        std::array<std::uint8_t, 54> source{};
        std::array<std::uint8_t, 6> center_source{}, center_delta{};
    };
    struct Frame {
        Transform normalize;
        FaceletState identity;
    };
    std::array<Transform, 54> moves;
    std::array<Frame, 24> frames;
    std::array<std::int8_t, 46656> frame_by_centers{};
    std::array<Piece, 216> corners;
    std::array<Piece, 36> edges;

    static unsigned center_key(const FaceletState& state) {
        unsigned key = 0;
        for (int face = 0; face < 6; ++face) key = 6 * key + state.colors[face * 9 + 4];
        return key;
    }
    VerificationGeometry() {
        FaceletState labels;
        for (int i = 0; i < 54; ++i) labels.colors[i] = static_cast<std::int8_t>(i);
        for (int index = 0; index < 54; ++index) {
            const auto moved = apply_facelets(labels, move_from_index(index));
            auto& t = moves[index];
            std::copy(moved.colors.begin(), moved.colors.end(), t.source.begin());
            for (int face = 0; face < 6; ++face) {
                t.center_source[face] = t.source[face * 9 + 4] / 9;
                t.center_delta[face] = moved.center_ori[face];
            }
        }
        frame_by_centers.fill(-1);
        std::deque<FaceletState> queue{labels};
        int count = 0;
        while (!queue.empty()) {
            const auto rotated = queue.front(); queue.pop_front();
            auto colored = rotated;
            for (auto& color : colored.colors) color /= 9;
            const auto key = center_key(colored);
            if (frame_by_centers[key] >= 0) continue;
            const auto frame = from_facelets(colored).frame;
            frame_by_centers[key] = static_cast<std::int8_t>(frame);
            frames[frame].identity = colored;
            auto& inverse = frames[frame].normalize;
            for (int physical = 0; physical < 54; ++physical)
                inverse.source[rotated.colors[physical]] = static_cast<std::uint8_t>(physical);
            for (int face = 0; face < 6; ++face) {
                const auto physical = inverse.source[face * 9 + 4] / 9;
                inverse.center_source[face] = static_cast<std::uint8_t>(physical);
                inverse.center_delta[face] = static_cast<std::uint8_t>((4 - rotated.center_ori[physical]) % 4);
            }
            ++count;
            for (char axis : std::string_view("xyz")) queue.push_back(apply_facelets(rotated, Move{axis, 1}));
        }
        if (count != 24) throw std::logic_error("Independent geometry did not enumerate 24 proper frames.");
        for (int piece = 0; piece < 8; ++piece) for (int orientation = 0; orientation < 3; ++orientation) {
            std::array<int, 3> colors{};
            for (int i = 0; i < 3; ++i) colors[(i + orientation) % 3] = corner_facelets[piece][i] / 9;
            corners[36 * colors[0] + 6 * colors[1] + colors[2]] =
                {static_cast<std::int8_t>(piece), static_cast<std::int8_t>(orientation)};
        }
        for (int piece = 0; piece < 12; ++piece) for (int orientation = 0; orientation < 2; ++orientation) {
            std::array<int, 2> colors{};
            for (int i = 0; i < 2; ++i) colors[(i + orientation) % 2] = edge_facelets[piece][i] / 9;
            edges[6 * colors[0] + colors[1]] =
                {static_cast<std::int8_t>(piece), static_cast<std::int8_t>(orientation)};
        }
    }
    FaceletState apply(const FaceletState& state, Move move) const {
        const auto& t = moves[move_index(move)];
        FaceletState next;
        for (int i = 0; i < 54; ++i) next.colors[i] = state.colors[t.source[i]];
        for (int i = 0; i < 6; ++i)
            next.center_ori[i] = static_cast<std::uint8_t>((state.center_ori[t.center_source[i]] + t.center_delta[i]) % 4);
        return next;
    }
    PatternState decode(const FaceletState& state, const PatternState& start) const {
        const auto frame = frame_by_centers[center_key(state)];
        if (frame < 0) throw std::logic_error("Independent geometry lost its center frame.");
        const auto& t = frames[frame].normalize;
        PatternState replay; replay.frame = static_cast<std::uint8_t>(frame);
        for (int i = 0; i < 8; ++i) {
            const auto& facelets = corner_facelets[i];
            const auto piece = corners[36 * state.colors[t.source[facelets[0]]] +
                6 * state.colors[t.source[facelets[1]]] + state.colors[t.source[facelets[2]]]];
            if (piece.id < 0) throw std::logic_error("Independent geometry produced an invalid corner.");
            replay.cp[i] = start.cp[piece.id];
            replay.co[i] = static_cast<std::int8_t>(start.co[piece.id] < 0 ? -1 : (start.co[piece.id] + piece.orientation) % 3);
        }
        for (int i = 0; i < 12; ++i) {
            const auto& facelets = edge_facelets[i];
            const auto piece = edges[6 * state.colors[t.source[facelets[0]]] + state.colors[t.source[facelets[1]]]];
            if (piece.id < 0) throw std::logic_error("Independent geometry produced an invalid edge.");
            replay.ep[i] = start.ep[piece.id];
            replay.eo[i] = static_cast<std::int8_t>(start.eo[piece.id] < 0 ? -1 : (start.eo[piece.id] + piece.orientation) % 2);
        }
        for (int i = 0; i < 6; ++i) replay.center_ori[i] = static_cast<std::int8_t>(start.center_ori[i] < 0 ? -1 :
            (start.center_ori[i] + state.center_ori[t.center_source[i]] + t.center_delta[i]) % 4);
        return replay;
    }
};
} // namespace

Validation verify_pattern_solution(const PatternRequest& request, const PatternSolution& solution) {
    if (auto v = validate_pattern(request.pattern); !v) return v;
    if (solution.turn_cost < request.min_turns || (request.max_turns >= 0 && solution.turn_cost > request.max_turns))
        return {false, "Result cost is outside the requested interval."};
    if (solution.turn_cost != cube::turn_count(solution.moves) || solution.rotations != cube::rotation_count(solution.moves))
        return {false, "Algorithm counters disagree with structured actions."};
    if (request.max_rotations >= 0 && solution.rotations > request.max_rotations)
        return {false, "Result exceeds the requested maximum rotation count."};
    std::array<int, 18> sym_counts{};
    for (auto m : solution.moves) {
        const auto pos = move_names.find(m.symbol);
        if (pos != std::string_view::npos) ++sym_counts[pos];
    }
    for (std::size_t s = 0; s < 18; ++s)
        if (request.max_symbol_uses[s] >= 0 && sym_counts[s] > request.max_symbol_uses[s])
            return {false, "Result exceeds a per-symbol maximum move count."};
    std::array<bool, 54> allowed{};
    for (auto m : request.allowed_moves) allowed[move_index(m)] = true;
    const auto pre_rot = effective_pre_rotation(request);
    std::size_t leading_rotations = 0;
    while (leading_rotations < solution.moves.size() && solution.moves[leading_rotations].is_rotation())
        ++leading_rotations;
    for (std::size_t i = 0; i < solution.moves.size(); ++i) {
        const auto m = solution.moves[i];
        const bool legal_pre_rot =
            (pre_rot == PatternPreRotation::YOnly && i == 0 && leading_rotations == 1 && m.symbol == 'y') ||
            (pre_rot == PatternPreRotation::All24 && i < leading_rotations && leading_rotations <= 2 && m.is_rotation());
        if (!allowed[move_index(m)] && !legal_pre_rot) return {false, "Result contains a disabled action."};
    }
    for (std::size_t i = 0; i < solution.moves.size(); ++i) {
        const auto m = solution.moves[i];
        const auto prefix = std::span<const Move>(solution.moves.data(), i);
        if (request.forbid_consecutive_same_symbol && !m.is_rotation() && i >= 1 &&
            solution.moves[i - 1].symbol == m.symbol)
            return {false, "Result contains consecutive moves on the same layer."};
        if (request.forbid_consecutive_same_symbol && !m.is_rotation() && i >= 2) {
            const auto a = solution.moves[i - 2];
            const auto b = solution.moves[i - 1];
            if (!a.is_rotation() && !b.is_rotation() && a.symbol == m.symbol && b.symbol != m.symbol &&
                moves_commute(a, b)) {
                const auto combined = static_cast<std::uint8_t>((a.power + m.power) % 4);
                if (combined == 0 || !allowed[move_index(Move{m.symbol, combined})])
                    return {false, "Result synthesizes a disabled or cancelling move across a commuting opposite layer."};
            }
        }
        if (request.reject_alternating_triples && violates_alternating_triple(prefix, m))
            return {false, "Result contains a forbidden alternating triple."};
    }
    const auto start = initial_state(request);
    const auto goals = make_goals(request);
    const Face target_d = logical_face(request.pattern.frame, Face::D);
    static const VerificationGeometry independent;
    auto geometry = independent.frames[start.frame].identity;
    PatternState replay = start;
    bool paid = false;
    for (auto move : solution.moves) {
        if (goal_index(replay, goals, request.require_d_on_d, target_d) >= 0) return {false, "Result continues after its first modulo-U goal."};
        if (!move.is_rotation()) {
            if (!paid && move.symbol == 'U' && request.modulo_u && request.remove_u_symmetric_prefix &&
                logical_face(replay.frame, Face::U) == Face::U && u_conjugation_invariant(request.pattern, move.power))
                return {false, "Result has a redundant U-symmetric prefix."};
            paid = true;
        }
        geometry = independent.apply(geometry, move);
        replay = independent.decode(geometry, start);
    }
    if (replay != solution.terminal) return {false, "Geometric replay disagrees with the search terminal state."};
    if (request.require_d_on_d && logical_face(replay.frame, Face::D) != target_d)
        return {false, "Geometric replay does not place the bottom center on physical D."};
    if (solution.residual_u < 0 || solution.residual_u >= static_cast<int>(goals.size()) || !matches(replay, goals[solution.residual_u]))
        return {false, "Geometric replay does not meet the requested target."};
    return {};
}

PatternReport enumerate_pattern(const PatternRequest& request, PatternSolutionCallback on_solution, PatternProgressCallback on_progress) {
    PatternReport report;
    Control control{request, report, std::move(on_progress)};
    try {
        if (auto v = validate_pattern(request.pattern); !v) { report.stop = PatternStop::InvalidInput; report.message = v.message; return report; }
        if (request.min_turns < 0 || request.max_turns < -1 || (request.max_turns >= 0 && request.max_turns < request.min_turns)) {
            report.stop = PatternStop::InvalidInput; report.message = "Require min_turns >= 0 and max_turns >= min_turns, or max_turns = -1 for unlimited."; return report;
        }
        if (request.time_limit.count() < 0) {
            report.stop = PatternStop::InvalidInput; report.message = "time_limit must be nonnegative; zero means unlimited."; return report;
        }
        if (request.max_rotations < -1) {
            report.stop = PatternStop::InvalidInput; report.message = "max_rotations must be >= -1 (-1 means unlimited)."; return report;
        }
        for (int lim : request.max_symbol_uses)
            if (lim < -1) {
                report.stop = PatternStop::InvalidInput; report.message = "max_symbol_uses entries must be >= -1 (-1 means unlimited)."; return report;
            }
        for (auto m : request.allowed_moves) (void)move_index(m);
        const auto goals = make_goals(request);
        const auto start = initial_state(request);
        Search search{request, report, control, goals};
        search.callback = std::move(on_solution);
        std::vector<Move> effective_allowed;
        effective_allowed.reserve(request.allowed_moves.size());
        for (auto m : request.allowed_moves) {
            const auto sym_pos = move_names.find(m.symbol);
            if (sym_pos != std::string_view::npos && request.max_symbol_uses[sym_pos] == 0) continue;
            if (m.is_rotation() && request.max_rotations == 0) continue;
            auto index = move_index(m);
            if (!search.allowed[index]) {
                effective_allowed.push_back(m);
                if (!m.is_rotation()) search.paid_moves.push_back(m);
                search.allowed[index] = true;
            }
        }
        search.rotations = rotation_closures(effective_allowed);
        search.root_rotations = search.rotations[start.frame];
        const auto pre_rot = effective_pre_rotation(request);
        if (pre_rot == PatternPreRotation::YOnly) {
            std::array<bool, 24> seen_frames{};
            for (const auto& r : search.root_rotations) seen_frames[r.frame] = true;
            for (std::uint8_t p = 1; p <= 3; ++p) {
                const Move ym{'y', p};
                const auto nf = rotated_frame(start.frame, ym);
                if (!seen_frames[nf]) {
                    seen_frames[nf] = true;
                    search.root_rotations.push_back({nf, {ym}});
                }
            }
        } else if (pre_rot == PatternPreRotation::All24) {
            static const std::array<Move, 9> all_rots = {{
                {'x', 1}, {'x', 2}, {'x', 3},
                {'y', 1}, {'y', 2}, {'y', 3},
                {'z', 1}, {'z', 2}, {'z', 3},
            }};
            static const auto full_closures = rotation_closures(all_rots);
            std::array<bool, 24> seen_frames{};
            for (const auto& r : search.root_rotations) seen_frames[r.frame] = true;
            for (const auto& r : full_closures[start.frame]) {
                if (!seen_frames[r.frame]) {
                    seen_frames[r.frame] = true;
                    search.root_rotations.push_back(r);
                }
            }
        }
        // Assign dense effect IDs to unique paid transforms for O(1) incremental cursors.
        std::unordered_map<std::string, std::uint32_t> effect_ids;
        std::vector<MoveTransform> forward_transforms;
        for (int frame = 0; frame < 24; ++frame) {
            for (auto move : search.paid_moves) {
                const auto& t = move_transform(static_cast<std::uint8_t>(frame), move);
                const auto key = transform_key(t);
                auto [it, inserted] = effect_ids.emplace(key, static_cast<std::uint32_t>(forward_transforms.size()));
                if (inserted) forward_transforms.push_back(t);
                const auto sym_idx = static_cast<std::uint8_t>(move_names.find(move.symbol));
                search.paid_transitions[frame].push_back({move, &t, it->second, sym_idx});
            }
        }
        const bool has_usage_limits = request.max_rotations >= 0 || !search.constrained_symbols.empty();
        search.quotient_rotations = !has_usage_limits && !request.require_d_on_d && request.memoize_dead_ends &&
            request.quotient_free_rotations &&
            can_quotient_free_rotations(effective_allowed, start.frame, search.rotations);
        report.free_rotation_quotient = search.quotient_rotations;
        if (search.quotient_rotations) {
            const auto frames = reachable_frames_from(effective_allowed, start.frame);
            std::unordered_set<std::string> seen;
            for (auto m : search.paid_moves) for (auto frame : frames) {
                const auto& t = move_transform(frame, m);
                if (seen.insert(transform_key(t)).second) search.canonical_effects.push_back(t);
            }
        }
        if (request.modulo_u && request.remove_u_symmetric_prefix) {
            for (int k = 1; k <= 3; ++k) search.redundant_root_u[k] = u_conjugation_invariant(request.pattern, k);
            report.root_u_symmetric = search.redundant_root_u[1];
        }
        control.check(true);
        if (search.match_goal(start) < 0 && request.max_turns != 0 && !search.paid_moves.empty()) {
            // Pass effective_allowed when building bounds if zero-budget symbols/rotations were stripped.
            PatternRequest bound_req = request;
            bound_req.allowed_moves = effective_allowed;
            search.bounds = make_bounds(bound_req, goals, control);
            const bool enable_cursors = request.heuristic != PatternHeuristic::None &&
                                        (request.max_turns < 0 || request.max_turns >= 6);
            search.fast_table->build(search.bounds, forward_transforms, enable_cursors);
        }
        report.preparation_time = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - control.start);
        control.preparing = false;
        const int lower = search.root_lower_bound(start);
        const CursorState root_cursor = search.fast_table->make_cursor(start);
        search.prove_minimum = request.min_turns <= lower;
        if (search.match_goal(start) >= 0) {
            control.depth = 0;
            control.check(true, true);
            search.visit(start, root_cursor, 0, 0);
            report.completed_depth = request.max_turns < 0 ? 0 : request.max_turns;
        } else if (search.paid_moves.empty() || lower == std::numeric_limits<int>::max() ||
                   (request.max_turns >= 0 && lower > request.max_turns)) {
            report.completed_depth = request.max_turns < 0 ? 0 : request.max_turns;
        } else {
            const std::size_t workers = recommended_worker_count(false);
            for (int depth = std::max(request.min_turns, lower);;) {
                control.depth = depth; control.check(true, true);
                if (workers >= 2 && depth >= 5 && (request.max_nodes == 0 || request.max_nodes >= 100'000))
                    search.run_depth_parallel(start, root_cursor, depth);
                else
                    search.visit(start, root_cursor, 0, depth);
                report.completed_depth = depth;
                if (request.max_turns >= 0 && depth >= request.max_turns) break;
                if (depth == std::numeric_limits<int>::max()) throw std::overflow_error("Formula depth exceeds the representable integer range.");
                ++depth;
            }
        }
        report.stop = PatternStop::Exhausted;
    } catch (const Abort& abort) {
        report.stop = abort.reason;
    } catch (const std::invalid_argument& e) {
        report.stop = PatternStop::InvalidInput; report.message = e.what();
    }
    report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - control.start);
    if (control.preparing) report.preparation_time = report.elapsed;
    return report;
}

} // namespace cube
