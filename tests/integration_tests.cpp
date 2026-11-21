#include "cube/model.hpp"
#include "cube/pattern.hpp"
#include "cube/twophase.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void require(bool ok, const std::string& text) {
    ++checks;
    if (!ok) throw std::runtime_error(text);
}
std::set<std::string> result_keys(const cube::PatternReport& report) {
    std::set<std::string> keys;
    for (const auto& solution : report.solutions)
        keys.insert(cube::commuting_key(solution.moves));
    return keys;
}
void check_result(const cube::PatternRequest& request, const cube::PatternReport& report) {
    std::set<std::string> keys;
    for (const auto& solution : report.solutions) {
        require(solution.verified, "solution was not verified before publication");
        require(static_cast<bool>(cube::verify_pattern_solution(request, solution)), "formula verification failed");
        require(solution.turn_cost == cube::turn_count(solution.moves), "search turn cost mismatch");
        require(solution.rotations == cube::rotation_count(solution.moves), "search rotation count mismatch");
        require(solution.moves.size() == static_cast<std::size_t>(solution.turn_cost + solution.rotations), "token accounting mismatch");
        require(cube::parse_moves(cube::format_moves(solution.moves, true)) == solution.moves, "formatting changed moves");
        for (std::size_t n=0; n<solution.moves.size(); ++n)
            require(!cube::violates_alternating_triple(std::span(solution.moves).first(n), solution.moves[n]), "alternating triple escaped search");
        if (request.deduplicate_commuting)
            require(keys.insert(cube::commuting_key(solution.moves)).second, "duplicate commutation class");
    }
}
cube::PatternRequest small_request(std::string_view scramble, std::string_view allowed) {
    cube::PatternRequest request;
    request.pattern = cube::pattern_from_cube(cube::apply(cube::CubeState{}, cube::parse_moves(scramble)));
    request.allowed_moves = cube::parse_moves(allowed);
    request.max_turns = 3;
    request.max_results = 100000;
    request.max_nodes = 2000000;
    request.time_limit = std::chrono::seconds(20);
    return request;
}
}

int main() {
    try {
        for (const auto initial : {"", "U", "U2", "U'", "x U", "y U2", "z U'"}) {
            // Rotate the viewing frame after applying a logical-U state.
            const std::string text = initial;
            auto state = cube::CubeState{};
            if (text.size() > 2 && (text[0]=='x' || text[0]=='y' || text[0]=='z')) {
                state = cube::apply(state, cube::parse_moves(text.substr(2)));
                state = cube::apply(state, cube::Move{text[0],1});
            } else state = cube::apply(state, cube::parse_moves(text));
            auto request = small_request("", "U U2 U' R R2 R' x x2 x' y y2 y' z z2 z'");
            request.pattern = cube::pattern_from_cube(state);
            request.max_turns = 1;
            for (auto direction : {cube::PatternDirection::Solve, cube::PatternDirection::Generate}) {
                request.direction = direction;
                const auto report = cube::enumerate_pattern(request);
                check_result(request, report);
                require(!report.solutions.empty(), "mod-U initial goal not recognized");
                require(report.solutions.front().turn_cost == 0, "mod-U goal not zero turns");
                require(report.solutions.front().moves.empty(), "initial goal generated unnecessary moves");
            }
        }
        for (const auto rotation : {"", "x", "y", "z", "x y"}) {
            auto state = cube::apply(cube::CubeState{}, cube::Move{'R',1});
            state = cube::apply(state, cube::parse_moves(rotation));
            auto request = small_request("", "x x2 x' y y2 y' z z2 z'");
            request.pattern = cube::pattern_from_cube(state);
            request.max_turns = 0;
            const auto report = cube::enumerate_pattern(request);
            require(report.exhausted(), "rotation closure did not terminate");
            require(report.solutions.empty(), "logical R became a free mod-U goal");
        }

        {
            auto request = small_request("R' U'", "U U2 U' R R2 R'");
            request.max_turns = 2;
            const auto report = cube::enumerate_pattern(request);
            check_result(request, report);
            bool essential_u = false;
            for (const auto& solution : report.solutions) {
                if (!solution.moves.empty() && solution.moves.front().symbol == 'U') {
                    essential_u = true;
                    require(solution.turn_cost == 2, "essential initial U not counted");
                    require(cube::format_moves(solution.moves, true).front() == '[', "essential initial U not bracketed");
                }
            }
            require(essential_u, "root-U pruning removed an essential move");
        }
        {
            auto request = small_request("U R", "U U2 U' R R2 R'");
            request.max_turns = 1;
            const auto solving = cube::enumerate_pattern(request);
            check_result(request, solving);
            require(!solving.solutions.empty(), "mod-U solve should stop after R'");
            for (const auto& solution : solving.solutions)
                require(solution.moves.size() == 1 && solution.moves.front().symbol == 'R', "finishing U was appended");
            request.direction = cube::PatternDirection::Generate;
            const auto generating = cube::enumerate_pattern(request);
            require(generating.exhausted() && generating.solutions.empty(), "generation incorrectly inverted a mod-U solution");
            request.max_turns = 2;
            const auto generating_two = cube::enumerate_pattern(request);
            check_result(request, generating_two);
            require(!generating_two.solutions.empty(), "mod-U target should be generated in two turns");
        }

        struct Case { const char* scramble; const char* moves; int depth; };
        const Case cases[] = {
            {"R U R'", "U U2 U' R R2 R'", 3},
            {"r U r'", "U U2 U' R R2 R' r r2 r'", 3},
            {"M U", "U U2 U' M M2 M'", 3},
            {"l R r", "L L2 L' R R2 R' l l2 l' r r2 r'", 3},
            {"F R", "R R2 R' F F2 F' x x2 x'", 2}
        };
        for (const auto& c : cases) for (auto direction : {cube::PatternDirection::Solve, cube::PatternDirection::Generate}) {
            auto request = small_request(c.scramble, c.moves);
            request.max_turns = c.depth;
            request.direction = direction;
            request.heuristic = cube::PatternHeuristic::None;
            request.memoize_dead_ends = false;
            const auto exhaustive = cube::enumerate_pattern(request);
            require(exhaustive.exhausted(), "small unpruned reference search did not exhaust");
            check_result(request, exhaustive);
            request.heuristic = cube::PatternHeuristic::Standard;
            request.memoize_dead_ends = true;
            const auto pruned = cube::enumerate_pattern(request);
            require(pruned.exhausted(), "small pruned search did not exhaust");
            check_result(request, pruned);
            require(result_keys(exhaustive) == result_keys(pruned), "heuristic lost a formula equivalence class");
        }

        const auto path = std::filesystem::path(CUBE_PROJECT_ROOT)/"tables"/"original";
        cube::TwoPhaseSolver ordinary(path);
        auto one = ordinary.solve(cube::apply(cube::CubeState{}, cube::Move{'U',1}));
        require(one.solved && one.solution.turn_count == 1, "ordinary solver incorrectly inherited modulo U");
        auto wide_input = cube::apply(cube::CubeState{}, cube::parse_moves("r U M' f2 D l'"));
        auto ordinary_result = ordinary.solve(wide_input);
        require(ordinary_result.solved, "ordinary solver could not solve a state entered with wide moves");
        for (const auto move : ordinary_result.solution.moves)
            require(cube::face_names.find(move.symbol) != std::string_view::npos, "wide switch contaminated ordinary move set");
        require(cube::is_solved(ordinary_result.solution.final_state), "ordinary endpoint is not exact");
        std::cout << "PASS cross-core integration: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL cross-core integration: " << error.what() << '\n';
        return 1;
    }
}
