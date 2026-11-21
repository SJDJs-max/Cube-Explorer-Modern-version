#include "cube/pattern.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace cube;
namespace {
std::size_t checks = 0, valid_cases = 0;
void require(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
bool matches(const PatternState& a, const PatternState& b) {
    const auto fields = [](const auto& x, const auto& y) {
        for (std::size_t i = 0; i < x.size(); ++i) if (x[i] >= 0 && y[i] >= 0 && x[i] != y[i]) return false;
        return true;
    };
    return fields(a.cp,b.cp) && fields(a.co,b.co) && fields(a.ep,b.ep) && fields(a.eo,b.eo) && fields(a.center_ori,b.center_ori);
}
PatternState reference_replay(const PatternState& start, const FaceletState& geometry) {
    // The frozen 0.3 verifier's geometric decoder, intentionally uncached.
    const auto total = from_facelets(geometry);
    PatternState replay; replay.frame = total.frame;
    for (int i = 0; i < 8; ++i) {
        const int source = total.cubies.cp[i];
        replay.cp[i] = start.cp[source];
        replay.co[i] = static_cast<std::int8_t>(start.co[source] < 0 ? -1 : (start.co[source] + total.cubies.co[i]) % 3);
    }
    for (int i = 0; i < 12; ++i) {
        const int source = total.cubies.ep[i];
        replay.ep[i] = start.ep[source];
        replay.eo[i] = static_cast<std::int8_t>(start.eo[source] < 0 ? -1 : (start.eo[source] + total.cubies.eo[i]) % 2);
    }
    for (int i = 0; i < 6; ++i)
        replay.center_ori[i] = static_cast<std::int8_t>(start.center_ori[i] < 0 ? -1 : (start.center_ori[i] + total.center_ori[i]) % 4);
    return replay;
}
PatternState initial(const PatternRequest& request) {
    if (request.direction == PatternDirection::Solve) return request.pattern;
    auto p = pattern_from_cube(CubeState{}, true); p.frame = request.pattern.frame; return p;
}
bool reference_valid(const PatternRequest& request, const PatternSolution& solution) {
    std::vector<PatternState> goals;
    const auto target = request.direction == PatternDirection::Solve ? pattern_from_cube(CubeState{}, true) : request.pattern;
    for (int u = 0; u < (request.modulo_u ? 4 : 1); ++u) {
        auto goal = target; goal.frame = 0;
        if (u) goal = apply_pattern(goal, Move{'U', static_cast<std::uint8_t>(u)});
        goals.push_back(goal);
    }
    const auto start = initial(request);
    CubeState cube; cube.frame = start.frame;
    auto geometry = to_facelets(cube);
    auto replay = start;
    for (auto move : solution.moves) {
        if (std::any_of(goals.begin(), goals.end(), [&](const auto& goal) { return matches(replay, goal); })) return false;
        geometry = apply_facelets(geometry, move);
        replay = reference_replay(start, geometry);
    }
    return replay == solution.terminal && matches(replay, goals[solution.residual_u]);
}
void compare(PatternRequest request, const std::vector<Move>& moves) {
    request.allowed_moves = all_moves(); request.remove_u_symmetric_prefix = false;
    request.forbid_consecutive_same_symbol = false;
    request.reject_alternating_triples = false;
    CubeState reference; reference.frame = initial(request).frame;
    PatternSolution solution{moves, turn_count(moves), rotation_count(moves), 0,
        reference_replay(initial(request), apply_facelets(to_facelets(reference), moves))};
    const bool expected = reference_valid(request, solution);
    valid_cases += expected;
    require(static_cast<bool>(verify_pattern_solution(request, solution)) == expected, "Cached independent geometry disagrees with the 0.3 verifier.");
    solution.terminal.frame = static_cast<std::uint8_t>((solution.terminal.frame + 1) % 24);
    require(!verify_pattern_solution(request, solution), "Cached verifier accepted a forged terminal frame.");
}
void mask(PatternState& p, int kind) {
    if (!kind) return;
    for (int i = 0; i < 8; ++i) {
        if (i % 3 == 0) p.cp[i] = -1;
        if (i % 3 == 1) p.co[i] = -1;
    }
    for (int i = 0; i < 12; ++i) {
        if (i % 3 == 0) p.ep[i] = -1;
        if (i % 3 == 1) p.eo[i] = -1;
    }
    if (kind == 2) for (int i = 0; i < 6; i += 2) p.center_ori[i] = -1;
}
}
int main() {
    try {
        std::mt19937 random(0x531703);
        for (int frame = 0; frame < 24; ++frame) for (int move = 0; move < 54; ++move) for (int kind = 0; kind < 3; ++kind) {
            auto goal = pattern_from_cube(CubeState{}, true); goal.frame = static_cast<std::uint8_t>(frame); mask(goal, kind);
            const std::vector<Move> moves{move_from_index(move)};
            PatternRequest request; request.pattern = apply_pattern(goal, cube::inverse(moves));
            compare(request, moves);
        }
        for (int sample = 0; sample < 1200; ++sample) {
            const auto frame = static_cast<std::uint8_t>(random() % 24);
            std::vector<Move> moves;
            for (unsigned i = 0, length = 4 + random() % 25; i < length; ++i) moves.push_back(move_from_index(random() % 54));
            auto goal = pattern_from_cube(CubeState{}, true); goal.frame = frame; mask(goal, sample % 3);
            PatternRequest request; request.pattern = apply_pattern(goal, cube::inverse(moves));
            compare(request, moves);
            CubeState generated; generated.frame = frame;
            request.pattern = pattern_from_cube(cube::apply(generated, moves), true);
            request.pattern.frame = frame; mask(request.pattern, sample % 3);
            request.direction = PatternDirection::Generate;
            compare(request, moves);
        }
        std::cout << "accepted differential cases: " << valid_cases << '\n';
        require(valid_cases > 3500, "Differential geometry tests did not exercise enough accepted formulas.");
        std::cout << "pattern performance correctness: " << checks << " checks, " << valid_cases << " accepted geometry cases passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "failure after " << checks << ": " << error.what() << '\n'; return 1; }
}
