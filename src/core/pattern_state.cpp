#include "cube/pattern.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <stdexcept>

namespace cube {

PatternState::PatternState() {
    cp.fill(-1); co.fill(-1); ep.fill(-1); eo.fill(-1); center_ori.fill(-1);
}

PatternState pattern_from_cube(const CubeState& cube, bool include_centers) {
    PatternState p;
    std::copy(cube.cubies.cp.begin(), cube.cubies.cp.end(), p.cp.begin());
    std::copy(cube.cubies.co.begin(), cube.cubies.co.end(), p.co.begin());
    std::copy(cube.cubies.ep.begin(), cube.cubies.ep.end(), p.ep.begin());
    std::copy(cube.cubies.eo.begin(), cube.cubies.eo.end(), p.eo.begin());
    if (include_centers) std::copy(cube.center_ori.begin(), cube.center_ori.end(), p.center_ori.begin());
    p.frame = cube.frame;
    return p;
}

PatternState apply_pattern(const PatternState& p, Move move) {
    const auto& t = move_transform(p.frame, move);
    PatternState q;
    q.frame = t.next_frame;
    for (int i = 0; i < 8; ++i) {
        q.cp[i] = p.cp[t.delta.cp[i]];
        const int o = p.co[t.delta.cp[i]];
        q.co[i] = static_cast<std::int8_t>(o < 0 ? -1 : (o + t.delta.co[i]) % 3);
    }
    for (int i = 0; i < 12; ++i) {
        q.ep[i] = p.ep[t.delta.ep[i]];
        const int o = p.eo[t.delta.ep[i]];
        q.eo[i] = static_cast<std::int8_t>(o < 0 ? -1 : (o + t.delta.eo[i]) % 2);
    }
    for (int i = 0; i < 6; ++i)
        q.center_ori[i] = static_cast<std::int8_t>(p.center_ori[i] < 0 ? -1 : (p.center_ori[i] + t.center_delta[i]) % 4);
    return q;
}

PatternState apply_pattern(const PatternState& p, std::span<const Move> moves) {
    auto q = p;
    for (auto move : moves) q = apply_pattern(q, move);
    return q;
}

Validation validate_pattern(const PatternState& p) {
    if (p.frame >= 24) return {false, "Pattern frame must be in 0..23."};
    auto check = [](const auto& ids, const auto& ori, int n, int base, std::string_view label) -> Validation {
        std::array<bool, 12> seen{};
        int sum = 0; bool complete_orientation = true;
        for (int i = 0; i < n; ++i) {
            if (ids[i] < -1 || ids[i] >= n || ori[i] < -1 || ori[i] >= base)
                return {false, std::string(label) + " has an out-of-range identity or orientation."};
            if (ids[i] >= 0 && seen[ids[i]]) return {false, std::string(label) + " identity occurs more than once."};
            if (ids[i] >= 0) seen[ids[i]] = true;
            if (ori[i] >= 0) sum += ori[i]; else complete_orientation = false;
        }
        if (complete_orientation && sum % base != 0)
            return {false, std::string(label) + " orientations have impossible parity."};
        return {};
    };
    if (auto v = check(p.cp, p.co, 8, 3, "Corner"); !v) return v;
    if (auto v = check(p.ep, p.eo, 12, 2, "Edge"); !v) return v;
    for (auto o : p.center_ori) if (o < -1 || o > 3) return {false, "Center orientation must be -1 or 0..3."};
    // If at most one identity is unknown, its identity is forced. Use that
    // completion only for the physical parity check; keep the mask unchanged.
    auto parity = [](auto ids) -> int {
        int missing_count = 0, missing_slot = -1;
        std::array<bool, 12> seen{};
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] < 0) { ++missing_count; missing_slot = static_cast<int>(i); }
            else seen[ids[i]] = true;
        }
        if (missing_count > 1) return -1;
        if (missing_count == 1) for (std::size_t i = 0; i < ids.size(); ++i)
            if (!seen[i]) ids[missing_slot] = static_cast<std::int8_t>(i);
        int result = 0;
        for (std::size_t i = 0; i < ids.size(); ++i) for (std::size_t j = i + 1; j < ids.size(); ++j)
            result ^= ids[i] > ids[j];
        return result;
    };
    const int cp = parity(p.cp), ep = parity(p.ep);
    if (cp >= 0 && ep >= 0 && cp != ep) return {false, "Corner and edge permutation parities disagree."};
    bool all_centers = true; int center_sum = 0;
    for (auto o : p.center_ori) { all_centers &= o >= 0; if (o >= 0) center_sum += o; }
    const int determined_parity = cp >= 0 ? cp : ep;
    if (all_centers && determined_parity >= 0 && center_sum % 2 != determined_parity)
        return {false, "Center orientations disagree with the cubie permutation parity."};
    return {};
}

namespace {
std::vector<Move> frame_path(std::uint8_t from, std::uint8_t to) {
    std::array<bool, 24> seen{};
    std::array<std::vector<Move>, 24> paths;
    std::deque<std::uint8_t> queue;
    seen[from] = true; queue.push_back(from);
    while (!queue.empty()) {
        auto f = queue.front(); queue.pop_front();
        if (f == to) return paths[f];
        for (char s : std::string_view("xyz")) for (int power = 1; power <= 3; ++power) {
            Move m{s, static_cast<std::uint8_t>(power)};
            const auto g = rotated_frame(f, m);
            if (!seen[g]) { seen[g] = true; paths[g] = paths[f]; paths[g].push_back(m); queue.push_back(g); }
        }
    }
    throw std::logic_error("Unreachable proper cube frame.");
}

char normalized_color(char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}
}

PatternState parse_pattern_facelets(std::string_view text) {
    std::string input;
    for (char c : text) if (!std::isspace(static_cast<unsigned char>(c))) input += c;
    if (input.size() != 54) throw std::invalid_argument("A pattern needs exactly 54 facelets.");
    FaceletState markers = solved_facelets();
    FaceletState centers = solved_facelets();
    for (int face = 0; face < 6; ++face) {
        auto value = face_names.find(normalized_color(input[face * 9 + 4]));
        if (value == std::string_view::npos) throw std::invalid_argument("All six center colors must be specified.");
        for (int j = 0; j < 9; ++j) centers.colors[face * 9 + j] = static_cast<std::int8_t>(value);
    }
    const auto frame = from_facelets(centers).frame;
    for (int i = 0; i < 54; ++i) {
        const char c = input[i];
        if (c == '?' || c == '.') markers.colors[i] = -1;
        else if (c == '@') markers.colors[i] = 12;
        else {
            const auto index = face_names.find(normalized_color(c));
            if (index == std::string_view::npos) throw std::invalid_argument("Unknown pattern facelet symbol.");
            markers.colors[i] = static_cast<std::int8_t>(index + (std::islower(static_cast<unsigned char>(c)) ? 6 : 0));
        }
    }
    markers = apply_facelets(markers, frame_path(frame, 0));
    PatternState p; p.frame = frame;
    auto decode = [&](const auto& facelets, auto& ids, auto& ori, int base) {
        for (std::size_t pos = 0; pos < facelets.size(); ++pos) {
            int unknown = 0, marked = 0, marker_orientation = -1; bool ignore_ori = false;
            for (int j = 0; j < base; ++j) {
                const int c = markers.colors[facelets[pos][j]];
                unknown += c < 0;
                if (c == 12) { ++marked; marker_orientation = j; }
                ignore_ori |= c >= 6 && c < 12;
            }
            if (unknown == base) continue;
            if (marked == 1 && unknown == base - 1) { ori[pos] = static_cast<std::int8_t>(marker_orientation); continue; }
            if (unknown || marked) throw std::invalid_argument("Specify an entire cubie, an entirely unknown cubie, or one @ orientation marker.");
            bool found = false;
            for (std::size_t piece = 0; piece < facelets.size() && !found; ++piece) for (int o = 0; o < base && !found; ++o) {
                bool matches = true;
                for (int j = 0; j < base; ++j)
                    matches &= markers.colors[facelets[pos][(j + o) % base]] % 6 == facelets[piece][j] / 9;
                if (matches) { ids[pos] = static_cast<std::int8_t>(piece); ori[pos] = static_cast<std::int8_t>(ignore_ori ? -1 : o); found = true; }
            }
            if (!found) throw std::invalid_argument("A cubie has an impossible color combination or reflection.");
        }
    };
    decode(corner_facelets, p.cp, p.co, 3);
    decode(edge_facelets, p.ep, p.eo, 2);
    if (auto v = validate_pattern(p); !v) throw std::invalid_argument(v.message);
    return p;
}

std::string format_pattern_facelets(const PatternState& p) {
    if (auto v = validate_pattern(p); !v) throw std::invalid_argument(v.message);
    FaceletState markers = solved_facelets();
    markers.colors.fill(-1);
    for (int i = 0; i < 6; ++i) markers.colors[i * 9 + 4] = static_cast<std::int8_t>(i);
    auto encode = [&](const auto& facelets, const auto& ids, const auto& ori, int base) {
        for (std::size_t pos = 0; pos < facelets.size(); ++pos) {
            if (ids[pos] < 0) {
                if (ori[pos] >= 0) markers.colors[facelets[pos][ori[pos]]] = 12;
            } else {
                const int orientation = ori[pos] < 0 ? 0 : ori[pos];
                for (int j = 0; j < base; ++j)
                    markers.colors[facelets[pos][(j + orientation) % base]] =
                        static_cast<std::int8_t>(facelets[ids[pos]][j] / 9 + (ori[pos] < 0 ? 6 : 0));
            }
        }
    };
    encode(corner_facelets, p.cp, p.co, 3);
    encode(edge_facelets, p.ep, p.eo, 2);
    markers = apply_facelets(markers, frame_path(0, p.frame));
    std::string text;
    for (int c : markers.colors) {
        if (c < 0) text += '?';
        else if (c == 12) text += '@';
        else if (c < 6) text += face_names[c];
        else text += static_cast<char>(std::tolower(static_cast<unsigned char>(face_names[c - 6])));
    }
    return text;
}

bool violates_alternating_triple(std::span<const Move> path, Move next) noexcept {
    if (path.size() < 2) return false;
    const auto a = path[path.size() - 2], b = path.back();
    if (a.is_rotation() || b.is_rotation() || next.is_rotation() || a.is_slice() || b.is_slice() || next.is_slice()) return false;
    auto upper = [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; };
    return a.symbol == next.symbol && a.symbol != b.symbol && upper(a.symbol) == upper(b.symbol);
}

std::string commuting_key(std::span<const Move> moves) {
    static const auto commutes = [] {
        std::array<std::array<bool, 54>, 54> table{};
        for (int a = 0; a < 54; ++a) for (int b = 0; b < 54; ++b) {
            CubeState ab = cube::apply(cube::apply(CubeState{}, move_from_index(a)), move_from_index(b));
            CubeState ba = cube::apply(cube::apply(CubeState{}, move_from_index(b)), move_from_index(a));
            table[a][b] = ab == ba;
        }
        return table;
    }();
    std::vector<int> ids; ids.reserve(moves.size());
    for (auto m : moves) ids.push_back(move_index(m));
    std::vector<bool> emitted(ids.size());
    std::string key; key.reserve(ids.size());
    // Lexicographically minimal topological order of the word's dependence
    // graph gives a trace-monoid normal form for adjacent commuting swaps.
    for (std::size_t count = 0; count < ids.size(); ++count) {
        std::size_t best = ids.size();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (emitted[i]) continue;
            bool available = true;
            for (std::size_t j = 0; j < i; ++j)
                if (!emitted[j] && !commutes[ids[j]][ids[i]]) { available = false; break; }
            if (available && (best == ids.size() || ids[i] < ids[best])) best = i;
        }
        emitted[best] = true;
        key += static_cast<char>(ids[best] + 1);
    }
    return key;
}

std::string_view pattern_stop_name(PatternStop stop) noexcept {
    switch (stop) {
    case PatternStop::Exhausted: return "exhausted";
    case PatternStop::ResultLimit: return "result_limit";
    case PatternStop::TimeLimit: return "time_limit";
    case PatternStop::NodeLimit: return "node_limit";
    case PatternStop::Cancelled: return "cancelled";
    case PatternStop::InvalidInput: return "invalid_input";
    }
    return "invalid_input";
}

} // namespace cube
