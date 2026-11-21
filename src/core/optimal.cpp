// Standard optimal HTM search migrated from Herbert Kociemba's Cube Explorer
// Search.pas:175-686 and OptSearch.pas. GPL-3.0; original source remains read-only.
#include "cube/optimal.hpp"
#include "cube/runtime_config.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>

namespace cube {
namespace {

using Clock = std::chrono::steady_clock;
using Cursors = std::array<Phase1Cursor, 3>;

constexpr int kCornerPermCount = 40320;
constexpr int kTwistCount = 2187;
constexpr int kCornerClassCount = 2768;

int corner_perm_coord(const std::array<std::uint8_t, 8>& cp) noexcept {
    int coord = 0;
    for (int i = 0; i < 8; ++i) {
        int smaller = 0;
        for (int j = i + 1; j < 8; ++j) smaller += cp[j] < cp[i];
        coord = coord * (8 - i) + smaller;
    }
    return coord;
}

void set_corner_perm(std::array<std::uint8_t, 8>& cp, int coord) noexcept {
    std::array<std::uint8_t, 8> pool{0, 1, 2, 3, 4, 5, 6, 7};
    std::array<int, 8> digits{};
    for (int i = 7; i >= 0; --i) {
        digits[i] = coord % (8 - i);
        coord /= (8 - i);
    }
    for (int i = 0; i < 8; ++i) {
        cp[i] = pool[digits[i]];
        for (int j = digits[i]; j < 7 - i; ++j) pool[j] = pool[j + 1];
    }
}

int twist_coord(const std::array<std::uint8_t, 8>& co) noexcept {
    int coord = 0;
    for (int i = 0; i < 7; ++i) coord = coord * 3 + co[i];
    return coord;
}

void set_twist(std::array<std::uint8_t, 8>& co, int coord) noexcept {
    int sum = 0;
    for (int i = 6; i >= 0; --i) {
        co[i] = static_cast<std::uint8_t>(coord % 3);
        sum += co[i];
        coord /= 3;
    }
    co[7] = static_cast<std::uint8_t>((3 - sum % 3) % 3);
}

CubieState sym_mul_corners(const CubieState& a, const CubieState& b) noexcept {
    CubieState out;
    for (int i = 0; i < 8; ++i) {
        out.cp[i] = a.cp[b.cp[i]];
        const int x = a.co[b.cp[i]], y = b.co[i];
        int o;
        if (x < 3 && y < 3) o = (x + y) % 3;
        else if (x < 3) o = 3 + (x + y) % 3;
        else if (y < 3) o = 3 + (x - y + 3) % 3;
        else o = (x - y + 3) % 3;
        out.co[i] = static_cast<std::uint8_t>(o);
    }
    return out;
}

// Exact 8-corner (CornerPerm x Twist) HTM pruning table reduced by the 16 UD symmetries
// (2,768 classes x 2,187 twists = 6,053,616 bytes, ~5.8 MB). Built once lazily in memory.
struct CornerTable {
    std::vector<std::array<std::uint16_t, 18>> corner_move;
    std::vector<std::array<std::uint16_t, 18>> twist_move;
    std::vector<std::array<std::uint16_t, 16>> twist_conj;
    std::vector<std::uint16_t> corner_to_sym; // (class_id << 4) | sym_to_rep
    std::vector<std::uint8_t> dist;           // exact 8-corner HTM distance (0..11)

    CornerTable() {
        // Build the 16 UD symmetries (F2, U4, LR reflection).
        std::array<CubieState, 3> gen;
        gen[0].cp = {5,4,7,6,1,0,3,2};
        gen[1].cp = {3,0,1,2,7,4,5,6};
        gen[2].cp = {1,0,3,2,5,4,7,6};
        gen[2].co.fill(3);

        std::array<CubieState, 16> syms{};
        std::array<int, 16> inv_sym{};
        CubieState cur;
        int idx = 0;
        for (int f = 0; f < 2; ++f) {
            for (int u = 0; u < 4; ++u) {
                for (int r = 0; r < 2; ++r) {
                    syms[idx++] = cur;
                    cur = sym_mul_corners(cur, gen[2]);
                }
                cur = sym_mul_corners(cur, gen[1]);
            }
            cur = sym_mul_corners(cur, gen[0]);
        }
        for (int a = 0; a < 16; ++a)
            for (int b = 0; b < 16; ++b)
                if (sym_mul_corners(syms[a], syms[b]).cp == CubieState{}.cp &&
                    sym_mul_corners(syms[a], syms[b]).co == CubieState{}.co)
                    inv_sym[a] = b;

        auto conj = [&](const CubieState& c, int s) noexcept {
            return sym_mul_corners(sym_mul_corners(syms[s], c), syms[inv_sym[s]]);
        };

        std::array<CubieState, 18> om{};
        for (int m = 0; m < 18; ++m) om[m] = outer_move(static_cast<Face>(m / 3), m % 3 + 1);

        // Coordinate move tables & twist conjugation.
        corner_move.resize(kCornerPermCount);
        twist_move.resize(kTwistCount);
        twist_conj.resize(kTwistCount);
        for (int t = 0; t < kTwistCount; ++t) {
            CubieState c;
            set_twist(c.co, t);
            for (int m = 0; m < 18; ++m) twist_move[t][m] = static_cast<std::uint16_t>(twist_coord(compose(c, om[m]).co));
            for (int s = 0; s < 16; ++s) twist_conj[t][s] = static_cast<std::uint16_t>(twist_coord(conj(c, s).co));
        }
        for (int cp = 0; cp < kCornerPermCount; ++cp) {
            CubieState c;
            set_corner_perm(c.cp, cp);
            for (int m = 0; m < 18; ++m) corner_move[cp][m] = static_cast<std::uint16_t>(corner_perm_coord(compose(c, om[m]).cp));
        }

        // Symmetry equivalence classes for corner permutation + stabilizer masks.
        corner_to_sym.assign(kCornerPermCount, 0xFFFFu);
        std::vector<std::uint16_t> class_rep;
        std::vector<std::uint16_t> class_stab;
        class_rep.reserve(kCornerClassCount);
        class_stab.reserve(kCornerClassCount);
        for (int cp = 0; cp < kCornerPermCount; ++cp) {
            if (corner_to_sym[cp] != 0xFFFFu) continue;
            const auto cls = static_cast<std::uint16_t>(class_rep.size());
            class_rep.push_back(static_cast<std::uint16_t>(cp));
            std::uint16_t stab = 0;
            CubieState c;
            set_corner_perm(c.cp, cp);
            for (int s = 0; s < 16; ++s) {
                const int mapped = corner_perm_coord(conj(c, s).cp);
                if (mapped == cp) stab |= static_cast<std::uint16_t>(1u << s);
                // conj(mapped, inv_sym[s]) == cp (the representative)
                if (corner_to_sym[mapped] == 0xFFFFu)
                    corner_to_sym[mapped] = static_cast<std::uint16_t>((cls << 4) | inv_sym[s]);
            }
            class_stab.push_back(stab);
        }
        if (static_cast<int>(class_rep.size()) != kCornerClassCount)
            throw std::logic_error("Unexpected 8-corner symmetry class count");

        // Precompute class_move[cls][m] = (next_cls << 4) | sym_to_next_rep.
        std::vector<std::array<std::uint16_t, 18>> class_move(kCornerClassCount);
        for (int cls = 0; cls < kCornerClassCount; ++cls) {
            const int rep = class_rep[cls];
            for (int m = 0; m < 18; ++m) class_move[cls][m] = corner_to_sym[corner_move[rep][m]];
        }

        // Exact BFS over all 2,768 x 2,187 = 6,053,616 states (depth 0..11).
        constexpr std::size_t total_states = static_cast<std::size_t>(kCornerClassCount) * kTwistCount;
        dist.assign(total_states, 255);
        dist[0] = 0;

        // Forward BFS for depths 0..8, then parallel backward scan for depths 9..11 (much faster than a 6M queue!).
        std::vector<std::uint32_t> frontier{0}, next_frontier;
        next_frontier.reserve(262144);
        for (int depth = 0; depth <= 7; ++depth) {
            next_frontier.clear();
            const auto nd = static_cast<std::uint8_t>(depth + 1);
            for (std::uint32_t state : frontier) {
                const int cls = static_cast<int>(state / kTwistCount);
                const int t = static_cast<int>(state % kTwistCount);
                for (int m = 0; m < 18; ++m) {
                    const auto cm = class_move[cls][m];
                    const int ncls = cm >> 4;
                    const int s = cm & 15;
                    const int nt = twist_conj[twist_move[t][m]][s];
                    const std::uint32_t nidx = static_cast<std::uint32_t>(ncls * kTwistCount + nt);
                    if (dist[nidx] != 255) continue;
                    dist[nidx] = nd;
                    next_frontier.push_back(nidx);
                    std::uint16_t stab = class_stab[ncls] & ~1u;
                    while (stab) {
                        const int sb = std::countr_zero(stab);
                        stab &= static_cast<std::uint16_t>(stab - 1);
                        const std::uint32_t sidx = static_cast<std::uint32_t>(ncls * kTwistCount + twist_conj[nt][sb]);
                        if (dist[sidx] == 255) {
                            dist[sidx] = nd;
                            next_frontier.push_back(sidx);
                        }
                    }
                }
            }
            frontier.swap(next_frontier);
        }

        // Parallel backward scan for depths 9, 10, 11 (zero contention: each thread owns disjoint classes!).
        const std::size_t threads = std::min<std::size_t>(recommended_worker_count(false), 12);
        for (int depth = 8; depth <= 10; ++depth) {
            const auto target = static_cast<std::uint8_t>(depth);
            const auto nd = static_cast<std::uint8_t>(depth + 1);
            std::atomic<int> next_cls{0};
            std::vector<std::jthread> pool;
            pool.reserve(threads);
            for (std::size_t w = 0; w < threads; ++w) {
                pool.emplace_back([&]() {
                    apply_worker_thread_priority();
                    for (;;) {
                        const int cls = next_cls.fetch_add(16, std::memory_order_relaxed);
                        if (cls >= kCornerClassCount) break;
                        const int cls_end = std::min(cls + 16, kCornerClassCount);
                        for (int c = cls; c < cls_end; ++c) {
                            const std::uint32_t base = static_cast<std::uint32_t>(c * kTwistCount);
                            const auto& cm_row = class_move[c];
                            for (int t = 0; t < kTwistCount; ++t) {
                                if (dist[base + t] != 255) continue;
                                for (int m = 0; m < 18; ++m) {
                                    const auto cm = cm_row[m];
                                    const int nt = twist_conj[twist_move[t][m]][cm & 15];
                                    if (dist[static_cast<std::uint32_t>((cm >> 4) * kTwistCount + nt)] == target) {
                                        dist[base + t] = nd;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                });
            }
        }

        // Verify exact mathematical coverage: every reachable state must have dist <= 11.
        for (std::uint8_t v : dist)
            if (v > 11) throw std::logic_error("8-corner pruning table failed completeness check");
    }

    inline int distance(std::uint16_t cp, std::uint16_t twist) const noexcept {
        const auto sym = corner_to_sym[cp];
        return dist[static_cast<std::uint32_t>((sym >> 4) * kTwistCount + twist_conj[twist][sym & 15])];
    }
};

const CornerTable& corner_table() {
    static const CornerTable table;
    return table;
}

struct Axes {
    std::array<CubieState, 3> symmetry;
    std::array<CubieState, 3> inverse_symmetry;
    std::array<std::array<int, 18>, 3> moves{};
    std::array<std::array<bool, 18>, 3> move_in_g1{};
    std::array<CubieState, 18> outer_moves{};
    Axes() {
        // Original CubeDefs.pas S_URF3; this proper rotation cycles UD/RL/FB.
        auto& s = symmetry[1];
        s.cp = {0,4,5,1,3,7,6,2}; s.co = {1,2,1,2,2,1,2,1};
        s.ep = {1,8,5,9,3,11,7,10,0,4,6,2}; s.eo = {1,0,1,0,1,0,1,0,1,1,1,1};
        symmetry[2] = compose(s, s);
        if (compose(symmetry[2], s) != CubieState{})
            throw std::logic_error("Original URF symmetry does not have order three");
        for (int m = 0; m < 18; ++m)
            outer_moves[m] = outer_move(static_cast<Face>(m / 3), m % 3 + 1);
        for (int axis = 0; axis < 3; ++axis) {
            inverse_symmetry[axis] = inverse(symmetry[axis]);
            for (int m = 0; m < 18; ++m) {
                const auto transformed = conjugate(outer_moves[m], axis);
                int found = -1;
                for (int n = 0; n < 18; ++n)
                    if (outer_moves[n] == transformed) { found = n; break; }
                if (found < 0) throw std::logic_error("URF symmetry failed to preserve face moves");
                moves[axis][m] = found;
                // A move preserves G1(axis) iff its mapped UD-axis move is U/D any power or R2/L2/F2/B2.
                move_in_g1[axis][m] = (found / 3 == 0 || found / 3 == 3 || found % 3 == 1);
            }
        }
    }
    CubieState conjugate(const CubieState& state, int axis) const {
        return compose(compose(symmetry[axis], state), inverse_symmetry[axis]);
    }
};

const Axes& axes() { static const Axes data; return data; }

inline int bound(const Cursors& c) noexcept {
    int maximum = std::max({c[0].distance, c[1].distance, c[2].distance});
    // Original Search.pas equal-three-bounds rule. A last face move belongs to
    // at least one axis subgroup, so a length-d solution reaches that subgroup
    // in at most d-1 moves. Three equal positive d bounds therefore imply d+1.
    if (maximum > 0 && c[0].distance == c[1].distance && c[1].distance == c[2].distance) ++maximum;
    return maximum;
}

Cursors root_cursors(const TwoPhaseSolver& table, const CubieState& state) {
    Cursors result;
    for (int axis = 0; axis < 3; ++axis) result[axis] = table.phase1_cursor(axes().conjugate(state, axis));
    return result;
}

inline int combined_lower_bound(const TwoPhaseSolver& table, const CubieState& state) {
    const int fwd = bound(root_cursors(table, state));
    const int inv = bound(root_cursors(table, inverse(state)));
    return std::max(fwd, inv);
}

// Order the 3 axes descending by distance so the tightest pruning axis runs first.
inline std::array<int, 3> axis_order(const Cursors& c) noexcept {
    int a0 = 0, a1 = 1, a2 = 2;
    if (c[a0].distance < c[a1].distance) std::swap(a0, a1);
    if (c[a1].distance < c[a2].distance) std::swap(a1, a2);
    if (c[a0].distance < c[a1].distance) std::swap(a0, a1);
    return {a0, a1, a2};
}

struct Halt { OptimalStop reason; };
struct WorkerHalt {};

class Search {
    const TwoPhaseSolver& table_;
    const CubeState& input_;
    const OptimalOptions& options_;
    std::stop_token stop_;
    const OptimalSolver::OnProgress& progress_;
    Clock::time_point began_ = Clock::now(), last_report_ = began_;
    std::unordered_set<std::string> excluded_;
    CubieState search_root_{};
    bool search_inverted_ = false;
    std::array<int, 64> path_{};
    int path_len_ = 0;
    int current_depth_ = 0;
    int minimum_goal_depth_ = std::numeric_limits<int>::max();
    bool fallback_eligible_ = false;

public:
    OptimalResult result;

    Search(const TwoPhaseSolver& table, const CubeState& input, const OptimalOptions& options,
           std::stop_token stop, const OptimalSolver::OnProgress& progress)
        : table_(table), input_(input), options_(options), stop_(stop), progress_(progress) {}

    std::chrono::milliseconds elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - began_);
    }

    void check_budget(bool force = false) {
        if (stop_.stop_requested()) throw Halt{OptimalStop::cancelled};
        if (options_.node_limit && result.nodes >= options_.node_limit) throw Halt{OptimalStop::node_limit};
        if (force || !(result.nodes & 1023u)) {
            if (options_.time_limit.count() > 0 && elapsed() >= options_.time_limit) throw Halt{OptimalStop::time_limit};
            const auto now = Clock::now();
            if (progress_ && (force || now - last_report_ >= std::chrono::milliseconds(80))) {
                last_report_ = now;
                progress_({current_depth_, result.completed_depth, result.nodes, elapsed()});
            }
        }
    }

    OptimalSolution verified(std::vector<Move> moves) const {
        OptimalSolution solution;
        for (auto m : moves)
            if (face_names.find(m.symbol) == std::string_view::npos || m.power < 1 || m.power > 3)
                throw std::invalid_argument("Optimal ordinary candidates must contain only 18 outer-face moves");
        solution.turn_count = static_cast<int>(moves.size());
        solution.final_state = cube::apply(input_, moves);
        if (!is_solved(solution.final_state) || !verify_sequence(input_, moves, solution.final_state))
            throw std::invalid_argument("Optimal candidate failed independent exact replay");
        solution.moves = std::move(moves);
        return solution;
    }

    std::vector<Move> physical_path_from(const int* seq, int len) const {
        std::vector<Move> output;
        output.reserve(static_cast<std::size_t>(len));
        if (!search_inverted_) {
            for (int i = 0; i < len; ++i) {
                const int m = seq[i];
                const auto face = physical_face(input_.frame, static_cast<Face>(m / 3));
                output.push_back({face_names[static_cast<int>(face)], static_cast<std::uint8_t>(m % 3 + 1)});
            }
        } else {
            for (int i = len - 1; i >= 0; --i) {
                const int m = seq[i];
                const auto face = physical_face(input_.frame, static_cast<Face>(m / 3));
                const auto inv_power = static_cast<std::uint8_t>(4 - (m % 3 + 1));
                output.push_back({face_names[static_cast<int>(face)], inv_power});
            }
        }
        return output;
    }

    bool replay_is_solved(const int* seq, int len) const noexcept {
        CubieState s = search_root_;
        const auto& om = axes().outer_moves;
        for (int i = 0; i < len; ++i) s = compose(s, om[seq[i]]);
        return is_solved(s);
    }

    // Coordinate-only serial DFS for shallow depths (< 7) or node-limited searches.
    bool dfs_serial(const Cursors& cursors, int remaining, int previous_face) {
        check_budget();
        ++result.nodes;
        if ((cursors[0].distance | cursors[1].distance | cursors[2].distance) == 0) {
            if (replay_is_solved(path_.data(), path_len_)) {
                minimum_goal_depth_ = std::min(minimum_goal_depth_, path_len_);
                auto moves = physical_path_from(path_.data(), path_len_);
                if (excluded_.contains(format_moves(moves))) return false;
                result.solution = verified(std::move(moves));
                result.solved = true;
                result.optimality_proven = result.solution.turn_count == minimum_goal_depth_;
                return true;
            }
        }
        if (remaining == 0 || bound(cursors) > remaining) return false;
        const int next_rem = remaining - 1;
        const auto [a0, a1, a2] = axis_order(cursors);
        const auto& ax = axes();
        const bool g1_filter0 = (next_rem == 0 && cursors[a0].distance == 0);
        const bool g1_filter1 = (next_rem == 0 && cursors[a1].distance == 0);
        const bool g1_filter2 = (next_rem == 0 && cursors[a2].distance == 0);
        for (int face = 0; face < 6; ++face) {
            if (face == previous_face || (face < 3 && previous_face == face + 3)) continue;
            for (int power = 1; power <= 3; ++power) {
                const int m = face * 3 + power - 1;
                if (g1_filter0 && !ax.move_in_g1[a0][m]) continue;
                if (g1_filter1 && !ax.move_in_g1[a1][m]) continue;
                if (g1_filter2 && !ax.move_in_g1[a2][m]) continue;
                const auto c0 = table_.advance_phase1(cursors[a0], ax.moves[a0][m]);
                if (c0.distance > next_rem) continue;
                const auto c1 = table_.advance_phase1(cursors[a1], ax.moves[a1][m]);
                if (c1.distance > next_rem) continue;
                const auto c2 = table_.advance_phase1(cursors[a2], ax.moves[a2][m]);
                if (c2.distance > next_rem) continue;
                if (next_rem > 0 && c0.distance == next_rem && c1.distance == next_rem && c2.distance == next_rem)
                    continue;
                Cursors child;
                child[a0] = c0;
                child[a1] = c1;
                child[a2] = c2;
                path_[path_len_++] = m;
                const bool found = dfs_serial(child, next_rem, face);
                --path_len_;
                if (found) return true;
            }
        }
        return false;
    }

    // Multi-threaded frontier parallel IDA* with exact 8-corner + 3-axis pruning.
    bool run_depth_parallel(const Cursors& root, std::uint16_t root_cp, int depth, const CornerTable* corners) {
        struct Task {
            std::array<int, 4> prefix{};
            int len = 0;
            int prev_face = -1;
            Cursors cursors{};
            std::uint16_t cp = 0;
            int heuristic = 0;
        };
        const auto& ax = axes();
        const int frontier_depth = depth >= 13 ? 3 : 2;
        std::vector<Task> tasks;
        tasks.reserve(frontier_depth == 3 ? 3200 : 256);

        check_budget();
        ++result.nodes;
        if ((root[0].distance | root[1].distance | root[2].distance) == 0 && replay_is_solved(nullptr, 0)) {
            minimum_goal_depth_ = 0;
            auto moves = physical_path_from(nullptr, 0);
            if (!excluded_.contains(format_moves(moves))) {
                result.solution = verified(std::move(moves));
                result.solved = true;
                result.optimality_proven = true;
                return true;
            }
            return false;
        }
        if (bound(root) > depth) return false;
        if (corners && corners->distance(root_cp, root[0].twist) > depth) return false;

        // Expand frontier to depth 2 (or 3 for depth >= 13) on main thread (< 0.3 ms).
        std::array<int, 4> cur_prefix{};
        auto expand = [&](auto&& self, const Cursors& c, std::uint16_t cp, int len, int prev_face) -> void {
            if (len >= frontier_depth) return;
            const int rem = depth - len;
            for (int face = 0; face < 6; ++face) {
                if (face == prev_face || (face < 3 && prev_face == face + 3)) continue;
                for (int power = 1; power <= 3; ++power) {
                    const int m = face * 3 + power - 1;
                    Cursors child;
                    for (int a = 0; a < 3; ++a) child[a] = table_.advance_phase1(c[a], ax.moves[a][m]);
                    const int h_axis = bound(child);
                    if (h_axis > rem - 1) continue;
                    const std::uint16_t ncp = corners ? corners->corner_move[cp][m] : 0;
                    const int h_corner = corners ? corners->distance(ncp, child[0].twist) : 0;
                    if (h_corner > rem - 1) continue;
                    cur_prefix[len] = m;
                    if (len + 1 == frontier_depth) {
                        tasks.push_back({cur_prefix, frontier_depth, face, child, ncp, std::max(h_axis, h_corner)});
                    } else {
                        ++result.nodes;
                        if ((child[0].distance | child[1].distance | child[2].distance) == 0 &&
                            replay_is_solved(cur_prefix.data(), len + 1)) {
                            minimum_goal_depth_ = std::min(minimum_goal_depth_, len + 1);
                            continue;
                        }
                        self(self, child, ncp, len + 1, face);
                    }
                }
            }
        };
        expand(expand, root, root_cp, 0, -1);
        if (tasks.empty()) return false;
        std::stable_sort(tasks.begin(), tasks.end(),
                         [](const Task& a, const Task& b) { return a.heuristic < b.heuristic; });

        std::atomic<std::size_t> next_task{0};
        std::atomic<std::uint64_t> global_nodes{result.nodes};
        std::atomic<int> stop_code{0}; // 1=found, 2=cancelled, 3=time_limit, 4=error
        std::atomic<int> min_goal{minimum_goal_depth_};
        std::mutex shared_mutex;
        std::string worker_error;

        auto update_min_goal = [&](int d) {
            int cur = min_goal.load(std::memory_order_relaxed);
            while (d < cur && !min_goal.compare_exchange_weak(cur, d, std::memory_order_relaxed)) {}
        };
        auto maybe_progress = [&]() {
            if (!progress_) return;
            const auto now = Clock::now();
            if (now - last_report_ < std::chrono::milliseconds(80)) return;
            std::unique_lock lock(shared_mutex, std::try_to_lock);
            if (!lock.owns_lock()) return;
            const auto now2 = Clock::now();
            if (now2 - last_report_ >= std::chrono::milliseconds(80)) {
                last_report_ = now2;
                progress_({current_depth_, result.completed_depth, global_nodes.load(std::memory_order_relaxed),
                           std::chrono::duration_cast<std::chrono::milliseconds>(now2 - began_)});
            }
        };

        const std::size_t max_workers = recommended_worker_count(true);
        const std::size_t worker_count = std::min<std::size_t>(tasks.size(), max_workers);
        {
            std::vector<std::jthread> pool;
            pool.reserve(worker_count);
            for (std::size_t w = 0; w < worker_count; ++w) {
                pool.emplace_back([&]() {
                    apply_worker_thread_priority();
                    std::array<int, 64> local_path{};
                    std::uint64_t local_nodes = 0;
                    std::uint64_t flushed_nodes = 0;

                    auto flush_nodes = [&]() {
                        const auto delta = local_nodes - flushed_nodes;
                        if (delta > 0) {
                            flushed_nodes = local_nodes;
                            global_nodes.fetch_add(delta, std::memory_order_relaxed);
                        }
                    };
                    auto check_worker = [&]() {
                        if (stop_code.load(std::memory_order_relaxed) != 0) throw WorkerHalt{};
                        if (stop_.stop_requested()) {
                            int expected = 0;
                            stop_code.compare_exchange_strong(expected, 2, std::memory_order_relaxed);
                            throw WorkerHalt{};
                        }
                        if (options_.time_limit.count() > 0 && elapsed() >= options_.time_limit) {
                            int expected = 0;
                            stop_code.compare_exchange_strong(expected, 3, std::memory_order_relaxed);
                            throw WorkerHalt{};
                        }
                        maybe_progress();
                    };

                    auto dfs = [&](auto&& self, const Cursors& cursors, std::uint16_t cp,
                                   int remaining, int previous_face, int len) -> bool {
                        ++local_nodes;
                        if ((local_nodes & 4095u) == 0) {
                            flush_nodes();
                            check_worker();
                        }
                        if ((cursors[0].distance | cursors[1].distance | cursors[2].distance) == 0) {
                            if (cp == 0 && replay_is_solved(local_path.data(), len)) {
                                update_min_goal(len);
                                auto moves = physical_path_from(local_path.data(), len);
                                if (excluded_.contains(format_moves(moves))) return false;
                                auto sol = verified(std::move(moves));
                                std::lock_guard lock(shared_mutex);
                                if (stop_code.load(std::memory_order_relaxed) == 0) {
                                    result.solution = std::move(sol);
                                    result.solved = true;
                                    result.optimality_proven =
                                        result.solution.turn_count == min_goal.load(std::memory_order_relaxed);
                                    stop_code.store(1, std::memory_order_relaxed);
                                }
                                return true;
                            }
                        }
                        if (remaining == 0) return false;
                        const int next_rem = remaining - 1;
                        const auto [a0, a1, a2] = axis_order(cursors);
                        const bool use_corner = corners != nullptr && next_rem <= 10;
                        const bool g1_filter0 = (next_rem == 0 && cursors[a0].distance == 0);
                        const bool g1_filter1 = (next_rem == 0 && cursors[a1].distance == 0);
                        const bool g1_filter2 = (next_rem == 0 && cursors[a2].distance == 0);
                        for (int face = 0; face < 6; ++face) {
                            if (face == previous_face || (face < 3 && previous_face == face + 3)) continue;
                            for (int power = 1; power <= 3; ++power) {
                                const int m = face * 3 + power - 1;
                                if (g1_filter0 && !ax.move_in_g1[a0][m]) continue;
                                if (g1_filter1 && !ax.move_in_g1[a1][m]) continue;
                                if (g1_filter2 && !ax.move_in_g1[a2][m]) continue;

                                // Check tightest axis first; when next_rem <= 10 and corner table is active,
                                // evaluate the L3-resident 8-corner bound right after UD axis is known!
                                std::uint16_t ncp = 0;
                                Cursors child;
                                if (use_corner) {
                                    if (a0 == 0) {
                                        const auto c0 = table_.advance_phase1(cursors[0], ax.moves[0][m]);
                                        if (c0.distance > next_rem) continue;
                                        ncp = corners->corner_move[cp][m];
                                        if (corners->distance(ncp, c0.twist) > next_rem) continue;
                                        const auto c1 = table_.advance_phase1(cursors[a1], ax.moves[a1][m]);
                                        if (c1.distance > next_rem) continue;
                                        const auto c2 = table_.advance_phase1(cursors[a2], ax.moves[a2][m]);
                                        if (c2.distance > next_rem) continue;
                                        if (next_rem > 0 && c0.distance == next_rem && c1.distance == next_rem && c2.distance == next_rem)
                                            continue;
                                        child[0] = c0;
                                        child[a1] = c1;
                                        child[a2] = c2;
                                    } else {
                                        const auto c0 = table_.advance_phase1(cursors[a0], ax.moves[a0][m]);
                                        if (c0.distance > next_rem) continue;
                                        const auto cud = table_.advance_phase1(cursors[0], ax.moves[0][m]);
                                        if (cud.distance > next_rem) continue;
                                        ncp = corners->corner_move[cp][m];
                                        if (corners->distance(ncp, cud.twist) > next_rem) continue;
                                        const int other = (a1 == 0) ? a2 : a1;
                                        const auto c2 = table_.advance_phase1(cursors[other], ax.moves[other][m]);
                                        if (c2.distance > next_rem) continue;
                                        if (next_rem > 0 && c0.distance == next_rem && cud.distance == next_rem && c2.distance == next_rem)
                                            continue;
                                        child[a0] = c0;
                                        child[0] = cud;
                                        child[other] = c2;
                                    }
                                } else {
                                    const auto c0 = table_.advance_phase1(cursors[a0], ax.moves[a0][m]);
                                    if (c0.distance > next_rem) continue;
                                    const auto c1 = table_.advance_phase1(cursors[a1], ax.moves[a1][m]);
                                    if (c1.distance > next_rem) continue;
                                    const auto c2 = table_.advance_phase1(cursors[a2], ax.moves[a2][m]);
                                    if (c2.distance > next_rem) continue;
                                    if (next_rem > 0 && c0.distance == next_rem && c1.distance == next_rem && c2.distance == next_rem)
                                        continue;
                                    if (corners) ncp = corners->corner_move[cp][m];
                                    child[a0] = c0;
                                    child[a1] = c1;
                                    child[a2] = c2;
                                }
                                local_path[len] = m;
                                if (self(self, child, ncp, next_rem, face, len + 1)) return true;
                            }
                        }
                        return false;
                    };

                    try {
                        for (;;) {
                            if (stop_code.load(std::memory_order_relaxed) != 0) break;
                            const auto idx = next_task.fetch_add(1, std::memory_order_relaxed);
                            if (idx >= tasks.size()) break;
                            const auto& t = tasks[idx];
                            for (int i = 0; i < t.len; ++i) local_path[i] = t.prefix[i];
                            if (dfs(dfs, t.cursors, t.cp, depth - t.len, t.prev_face, t.len)) break;
                        }
                    } catch (const WorkerHalt&) {
                    } catch (const std::exception& e) {
                        std::lock_guard lock(shared_mutex);
                        if (worker_error.empty()) worker_error = e.what();
                        stop_code.store(4, std::memory_order_relaxed);
                    }
                    flush_nodes();
                });
            }
        }
        result.nodes = global_nodes.load(std::memory_order_relaxed);
        minimum_goal_depth_ = min_goal.load(std::memory_order_relaxed);
        const int stopped = stop_code.load(std::memory_order_relaxed);
        if (stopped == 1) {
            result.optimality_proven = result.solution.turn_count == minimum_goal_depth_;
            return true;
        }
        if (stopped == 2) throw Halt{OptimalStop::cancelled};
        if (stopped == 3) throw Halt{OptimalStop::time_limit};
        if (stopped == 4) throw std::logic_error(worker_error);
        return false;
    }

    OptimalResult run() {
        const auto validity = validate(input_);
        if (!validity) throw std::invalid_argument(validity.message);
        if (options_.max_moves < 0 || options_.max_moves > 60 || options_.time_limit.count() < 0)
            throw std::invalid_argument("Optimal limits require max_moves 0..60 and nonnegative time");
        for (const auto& word : options_.excluded_solutions)
            excluded_.insert(format_moves(parse_moves(word)));
        if (options_.initial_solution) {
            auto candidate = verified(*options_.initial_solution);
            minimum_goal_depth_ = candidate.turn_count;
            fallback_eligible_ =
                candidate.turn_count <= options_.max_moves && !excluded_.contains(format_moves(candidate.moves));
            if (fallback_eligible_) { result.solved = true; result.solution = std::move(candidate); }
        }
        try {
            check_budget(true);
            const auto fwd_cursors = root_cursors(table_, input_.cubies);
            const auto inv_cubies = inverse(input_.cubies);
            const auto inv_cursors = root_cursors(table_, inv_cubies);
            const int fwd_bound = bound(fwd_cursors);
            const int inv_bound = bound(inv_cursors);
            result.lower_bound = std::max(fwd_bound, inv_bound);

            const int fwd_sum = fwd_cursors[0].distance + fwd_cursors[1].distance + fwd_cursors[2].distance;
            const int inv_sum = inv_cursors[0].distance + inv_cursors[1].distance + inv_cursors[2].distance;
            search_inverted_ = excluded_.empty() && (inv_bound > fwd_bound || (inv_bound == fwd_bound && inv_sum > fwd_sum));
            search_root_ = search_inverted_ ? inv_cubies : input_.cubies;
            const Cursors start_cursors = search_inverted_ ? inv_cursors : fwd_cursors;
            const auto root_cp = static_cast<std::uint16_t>(corner_perm_coord(search_root_.cp));

            const unsigned hw = std::thread::hardware_concurrency();
            const CornerTable* corners = nullptr;
            for (current_depth_ = result.lower_bound; current_depth_ <= options_.max_moves; ++current_depth_) {
                check_budget(true);
                if (fallback_eligible_ && current_depth_ >= result.solution.turn_count) {
                    result.optimality_proven = result.solution.turn_count == minimum_goal_depth_;
                    result.stop_reason = OptimalStop::found;
                    result.elapsed = elapsed();
                    return result;
                }
                // Lazily build the 5.8 MB exact 8-corner pruning table once we reach deep search (>= 13 moves).
                if (!corners && current_depth_ >= 13 && options_.node_limit == 0) {
                    corners = &corner_table();
                    check_budget(true);
                }
                path_len_ = 0;
                const bool found = (hw >= 2 && current_depth_ >= 7 && options_.node_limit == 0)
                                       ? run_depth_parallel(start_cursors, root_cp, current_depth_, corners)
                                       : dfs_serial(start_cursors, current_depth_, -1);
                if (found) {
                    result.stop_reason = OptimalStop::found;
                    result.elapsed = elapsed();
                    return result;
                }
                result.completed_depth = current_depth_;
            }
            result.stop_reason = OptimalStop::depth_limit;
        } catch (const Halt& halt) { result.stop_reason = halt.reason; }
        result.elapsed = elapsed();
        result.message = result.solved ? "A verified candidate is retained; optimality has not been proved."
                                       : "No new solution was returned within the completed search and limits.";
        return result;
    }
};
} // namespace

OptimalSolver::OptimalSolver(const std::filesystem::path& path, TwoPhaseSolver::Progress progress, std::stop_token stop)
    : tables_(path, std::move(progress), stop) {}
OptimalSolver::OptimalSolver(TwoPhaseSolver tables) : tables_(std::move(tables)) {}
OptimalResult OptimalSolver::solve(const CubeState& input, const OptimalOptions& options, std::stop_token stop, OnProgress progress) const {
    try { return Search(tables_, input, options, stop, progress).run(); }
    catch (const std::invalid_argument& error) {
        OptimalResult result; result.stop_reason = OptimalStop::invalid_input; result.message = error.what(); return result;
    }
}
int OptimalSolver::lower_bound(const CubieState& state) const { return combined_lower_bound(tables_, state); }
std::string_view to_string(OptimalStop stop) noexcept {
    switch (stop) {
    case OptimalStop::found: return "found";
    case OptimalStop::time_limit: return "time_limit";
    case OptimalStop::node_limit: return "node_limit";
    case OptimalStop::cancelled: return "cancelled";
    case OptimalStop::depth_limit: return "depth_limit";
    case OptimalStop::invalid_input: return "invalid_input";
    }
    return "unknown";
}
} // namespace cube
