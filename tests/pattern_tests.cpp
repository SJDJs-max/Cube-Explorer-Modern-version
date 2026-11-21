#include "cube/pattern.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace cube;
namespace {
std::size_t checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
PatternRequest request(std::string_view scramble, std::string_view moves, int depth) {
    PatternRequest r;
    r.pattern = pattern_from_cube(cube::apply(CubeState{}, parse_moves(scramble)));
    r.allowed_moves = parse_moves(moves);
    r.max_turns = depth;
    r.max_results = 0;
    r.max_nodes = 0;
    r.time_limit = std::chrono::milliseconds(120000);
    r.heuristic = PatternHeuristic::None;
    r.memoize_dead_ends = false;
    return r;
}
std::set<std::string> keys(const PatternReport& report) {
    std::set<std::string> k;
    for (const auto& solution : report.solutions) k.insert(commuting_key(solution.moves));
    return k;
}
std::vector<std::string> ordered_words(const PatternReport& report) {
    std::vector<std::string> words;
    for (const auto& solution : report.solutions) words.push_back(format_moves(solution.moves));
    return words;
}
void verified(const PatternRequest& r, const PatternReport& report) {
    require(report.stop == PatternStop::Exhausted, "Unexpected search interruption");
    for (const auto& s : report.solutions) {
        require(s.verified, "Unverified result was published");
        require(static_cast<bool>(verify_pattern_solution(r, s)), "Independent replay failed");
        require(s.turn_cost + s.rotations == static_cast<int>(s.moves.size()), "Counters do not sum to action count");
    }
}
}

int main() {
    try {
        for (auto scramble : {"", "U", "U2", "U'"}) {
            auto r = request(scramble, "R R' U U' x y z", 3);
            auto report = enumerate_pattern(r);
            verified(r, report);
            require(report.solutions.size() == 1, "Modulo-U input must return only the zero-turn result");
            require(report.solutions.front().moves.empty(), "A modulo-U solved input contains moves");
        }
        {
            auto r = request("R", "x x2 x' y y2 y' z z2 z'", 4);
            auto report = enumerate_pattern(r);
            verified(r, report);
            require(report.solutions.empty(), "Free rotations changed logical R into a modulo-U goal");
            require(report.nodes < 100, "Zero-cost rotations formed an infinite cycle");
        }
        {
            auto r = request("R U", "R R'", 1);
            auto solve = enumerate_pattern(r);
            verified(r, solve);
            require(solve.solutions.empty(), "Solve used the wrong side of the modulo-U orbit");
            r.direction = PatternDirection::Generate;
            auto generate = enumerate_pattern(r);
            verified(r, generate);
            require(generate.solutions.size() == 1 && generate.solutions[0].moves == parse_moves("R"), "Generate did not accept the right modulo-U orbit");
        }
        {
            auto r = request("U R", "R R'", 1);
            auto solve = enumerate_pattern(r);
            verified(r, solve);
            require(solve.solutions.size() == 1 && solve.solutions[0].moves == parse_moves("R'"), "Solve appended a final U");
            r.direction = PatternDirection::Generate;
            require(enumerate_pattern(r).solutions.empty(), "Generation incorrectly inverted a modulo-U solution");
        }
        {
            auto r = request("R' U'", "U R", 2);
            auto result = enumerate_pattern(r);
            verified(r, result);
            require(result.solutions.size() == 1 && result.solutions[0].moves == parse_moves("U R"), "Required initial U was lost");
            require(format_moves(result.solutions[0].moves, true) == "[U] R", "Required initial U was not bracketed");
            require(result.solutions[0].turn_cost == 2, "Bracketed U was excluded from cost");
        }
        require(violates_alternating_triple(parse_moves("R r"), Move{'R',1}), "R r R must be rejected");
        require(violates_alternating_triple(parse_moves("r2 R'"), Move{'r',3}), "r2 R' r' must be rejected");
        require(!violates_alternating_triple(parse_moves("l R"), Move{'r',1}), "l R r was incorrectly rejected");
        require(!violates_alternating_triple(parse_moves("r"), Move{'R',1}), "r R was incorrectly rejected");
        require(commuting_key(parse_moves("L R U")) == commuting_key(parse_moves("R L U")), "Commuting outer turns were not deduplicated");
        require(commuting_key(parse_moves("U r R F")) == commuting_key(parse_moves("U R r F")), "Commuting mixed-width turns were not deduplicated");
        require(commuting_key(parse_moves("R U")) != commuting_key(parse_moves("U R")), "Noncommuting turns were incorrectly merged");
        require(commuting_key(parse_moves("R R")) != commuting_key(parse_moves("R2")), "Trace deduplication rewrote algebraically equivalent words");
        {
            auto r = request("R2", "R", 2);
            r.forbid_consecutive_same_symbol = false;
            auto report = enumerate_pattern(r);
            verified(r, report);
            require(report.solutions.size() == 1 && report.solutions[0].turn_cost == 2, "Opt-in restricted powers lost the R R solution");
        }
        {
            // Default forbid_consecutive_same_symbol must truly ban F2: never emit F F, F' F', or F B F!
            auto r = request("F2", "F F'", 2);
            auto report = enumerate_pattern(r);
            verified(r, report);
            require(report.solutions.empty(), "Banning F2 still emitted F F / F' F'");
            PatternSolution fake_ff{parse_moves("F F"), 2, 0, 0, apply_pattern(r.pattern, parse_moves("F F"))};
            require(!verify_pattern_solution(r, fake_ff), "Verifier accepted consecutive F F when F2 is banned");

            auto r2 = request("F2 B", "F F' B B2 B'", 3);
            auto plain2 = enumerate_pattern(r2);
            verified(r2, plain2);
            require(plain2.solutions.empty(), "Banning F2 still synthesized F2 via commuting F B F");
            r2.heuristic = PatternHeuristic::Standard;
            r2.memoize_dead_ends = true;
            auto pruned2 = enumerate_pattern(r2);
            verified(r2, pruned2);
            require(pruned2.solutions.empty(), "Pruned search synthesized banned F2 via F B F");
            PatternSolution fake_fbf{parse_moves("F B' F"), 3, 0, 0, apply_pattern(r2.pattern, parse_moves("F B' F"))};
            require(!verify_pattern_solution(r2, fake_fbf), "Verifier accepted F B' F synthesizing banned F2");
        }
        for (auto scramble : {"r U", "M U", "l R r", "R U R'", "F2 R' U", "x R U"}) {
            auto r = request(scramble, "R R2 R' U U2 U' r r2 r' M M2 M' x x'", 3);
            auto unpruned = enumerate_pattern(r);
            verified(r, unpruned);
            r.heuristic = PatternHeuristic::Standard;
            r.memoize_dead_ends = true;
            auto pruned = enumerate_pattern(r);
            verified(r, pruned);
            require(keys(unpruned) == keys(pruned), "Pruning changed the shallow canonical result set");
            std::cout << "pruning parity " << scramble << ": " << pruned.solutions.size() << " formulas, " << pruned.nodes << "/" << unpruned.nodes << " nodes\n";
        }
        {
            PatternRequest r;
            r.pattern.cp[0] = 0; r.pattern.co[0] = -1;
            r.pattern.co[2] = 1;
            r.pattern.ep[3] = 3; r.pattern.eo[3] = 0;
            r.allowed_moves = parse_moves("R R' U U' F F'");
            r.max_turns = 3; r.max_results = 0; r.max_nodes = 0;
            r.time_limit = std::chrono::milliseconds(120000);
            r.heuristic = PatternHeuristic::None;
            r.memoize_dead_ends = false;
            auto unpruned = enumerate_pattern(r); verified(r, unpruned);
            r.heuristic = PatternHeuristic::Standard;
            r.memoize_dead_ends = true;
            auto pruned = enumerate_pattern(r); verified(r, pruned);
            require(keys(unpruned) == keys(pruned), "Partial orientation-only PDB changed the result set");
            r.direction = PatternDirection::Generate;
            r.heuristic = PatternHeuristic::None;
            r.memoize_dead_ends = false;
            auto generated = enumerate_pattern(r); verified(r, generated);
            r.heuristic = PatternHeuristic::Standard;
            r.memoize_dead_ends = true;
            auto generated_pruned = enumerate_pattern(r); verified(r, generated_pruned);
            require(keys(generated) == keys(generated_pruned), "Partial generation PDB changed the result set");
        }
        {
            auto r = request("R U", "R R' U U'", 4);
            r.cancelled = [] { return true; };
            require(enumerate_pattern(r).stop == PatternStop::Cancelled, "Cancellation was not honored");
            r.cancelled = {};
            r.max_nodes = 2;
            require(enumerate_pattern(r).stop == PatternStop::NodeLimit, "Node limit was not distinguished from exhaustion");
            r.max_nodes = 0;
            r.max_results = 1;
            require(enumerate_pattern(r).stop == PatternStop::ResultLimit, "Result limit was not distinguished from exhaustion");
        }
        {
            auto r = request("U", "U R", 1);
            r.max_nodes = 1;
            auto zero = enumerate_pattern(r);
            require(zero.solutions.size() == 1 && zero.solutions[0].turn_cost == 0, "A one-node budget lost the root zero-step solution");
            r.time_limit = std::chrono::milliseconds(-1);
            require(enumerate_pattern(r).stop == PatternStop::InvalidInput, "Negative time limit was silently treated as unlimited");
            auto impossible = pattern_from_cube(CubeState{}, true); impossible.center_ori[0] = 1;
            require(!validate_pattern(impossible), "Impossible complete supercube center parity was accepted");
        }
        for (bool partial : {false, true}) for (bool centers : {false, true}) for (auto direction : {PatternDirection::Solve, PatternDirection::Generate}) {
            auto r = request("R U R'", "R R' U U' r r' x x' y y'", 3);
            r.pattern = pattern_from_cube(cube::apply(CubeState{}, parse_moves("R U R'")), centers);
            if (partial) { r.pattern.cp[0] = -1; r.pattern.co[1] = -1; r.pattern.ep[0] = -1; r.pattern.eo[1] = -1; }
            r.direction = direction;
            r.memoize_dead_ends = false;
            auto plain = enumerate_pattern(r); verified(r, plain);
            r.memoize_dead_ends = true; r.quotient_free_rotations = false;
            auto exact_cache = enumerate_pattern(r); verified(r, exact_cache);
            require(!exact_cache.free_rotation_quotient, "Disabled free-rotation quotient was still active");
            r.quotient_free_rotations = true;
            auto quotient = enumerate_pattern(r); verified(r, quotient);
            require(quotient.free_rotation_quotient, "Full free rotations did not pass the mechanical realization proof");
            require(ordered_words(plain) == ordered_words(exact_cache), "Negative cache changed the complete result order");
            require(ordered_words(plain) == ordered_words(quotient), "Free-rotation quotient changed the complete result order");
        }
        {
            auto r = request("R U", "R R' U U' x x'", 2);
            r.memoize_dead_ends = true;
            require(!enumerate_pattern(r).free_rotation_quotient, "Restricted x-only rotations incorrectly qualified for the global quotient");
        }
        {
            auto r = request("R", "R'", 1);
            auto report = enumerate_pattern(r);
            auto forged = report.solutions.front(); forged.turn_cost = 0;
            require(!verify_pattern_solution(r, forged), "Verifier accepted a forged step count");
            forged = report.solutions.front(); forged.moves = parse_moves("R");
            require(!verify_pattern_solution(r, forged), "Verifier accepted a disabled move");
        }
        {
            // Single-axis y rotation with R U F qualifies for local-orbit quotient and preserves exact results.
            auto r = request("R U F'", "R R' U U' F F' y y2 y'", 3);
            r.memoize_dead_ends = false;
            auto plain = enumerate_pattern(r); verified(r, plain);
            r.memoize_dead_ends = true;
            r.heuristic = PatternHeuristic::Standard;
            auto quot = enumerate_pattern(r); verified(r, quot);
            require(quot.free_rotation_quotient, "Single-axis RUF+y should qualify for local-orbit free-rotation quotient");
            require(ordered_words(plain) == ordered_words(quot), "Single-axis local-orbit quotient changed result order");
        }
        {
            // Fine-grained max_rotations and max_symbol_uses limits: exact pruning & verifier parity.
            auto r = request("y R U R' F", "R R2 R' U U2 U' F F' y y2 y'", 4);
            r.max_rotations = 1;
            r.max_symbol_uses[2] = 1; // F <= 1
            auto unpruned = enumerate_pattern(r); verified(r, unpruned);
            for (const auto& s : unpruned.solutions) {
                require(s.rotations <= 1, "Solution exceeded max_rotations");
                int f_cnt = 0;
                for (auto m : s.moves) if (m.symbol == 'F') ++f_cnt;
                require(f_cnt <= 1, "Solution exceeded max_symbol_uses[F]");
            }
            r.heuristic = PatternHeuristic::Standard;
            r.memoize_dead_ends = true;
            auto pruned = enumerate_pattern(r); verified(r, pruned);
            require(ordered_words(unpruned) == ordered_words(pruned), "Usage-limited pruning changed exact result set");
            // Invalid limits must be rejected explicitly.
            auto bad = r; bad.max_rotations = -2;
            require(enumerate_pattern(bad).stop == PatternStop::InvalidInput, "Invalid max_rotations was accepted");
            bad = r; bad.max_symbol_uses[0] = -2;
            require(enumerate_pattern(bad).stop == PatternStop::InvalidInput, "Invalid max_symbol_uses was accepted");
        }
        {
            // Initial pre-rotation (pre_rotate_y) for Cross/XCross:
            // Scramble "F' U F" is solved in 3 turns by "F' U' F", which is unreachable in 3 turns using <R, U, L> alone,
            // but becomes "y' L' U' L" (or "y R' U' R") in 3 turns when initial y/y2/y' pre-rotation is allowed!
            auto r = request("F' U F", "R R2 R' U U2 U' L L2 L'", 3);
            r.modulo_u = false;
            r.pre_rotate_y = false;
            auto no_pre = enumerate_pattern(r); verified(r, no_pre);
            require(no_pre.solutions.empty(), "F' U F should not be 3-move solvable in <R, U, L> without pre-rotation");

            r.pre_rotate_y = true;
            r.heuristic = PatternHeuristic::None;
            auto with_pre_plain = enumerate_pattern(r); verified(r, with_pre_plain);
            const auto plain_words = ordered_words(with_pre_plain);
            require(std::ranges::find(plain_words, "y' R' U' R") != plain_words.end(),
                    "pre_rotate_y missed y' R' U' R in <R, U, L>");
            require(std::ranges::find(plain_words, "y L' U' L") != plain_words.end(),
                    "pre_rotate_y missed y L' U' L in <R, U, L>");
            for (const auto& s : with_pre_plain.solutions) {
                for (std::size_t i = 1; i < s.moves.size(); ++i)
                    require(!s.moves[i].is_rotation(), "pre_rotate_y emitted a mid-solve rotation when rotations were disabled");
            }

            r.heuristic = PatternHeuristic::Standard;
            r.memoize_dead_ends = true;
            auto with_pre_pruned = enumerate_pattern(r); verified(r, with_pre_pruned);
            require(ordered_words(with_pre_plain) == ordered_words(with_pre_pruned),
                    "pre_rotate_y heuristic pruning changed exact result set");

            // Full 24-orientation pre-rotation (PatternPreRotation::All24):
            // Scramble "D' R D" requires a D move without x/z pre-rotation, so it is unreachable in 3 moves inside <R, U, F>
            // under None or YOnly, but becomes solvable in 3 turns (e.g. "x F' R' F" or "z' R' U' R") under All24!
            auto r24 = request("D' R D", "R R2 R' U U2 U' F F2 F'", 3);
            r24.modulo_u = false;
            r24.pre_rotation = PatternPreRotation::YOnly;
            auto y_only = enumerate_pattern(r24); verified(r24, y_only);
            require(y_only.solutions.empty(), "D' R D should not be 3-move solvable in <R, U, F> with YOnly pre-rotation");

            r24.pre_rotation = PatternPreRotation::All24;
            r24.heuristic = PatternHeuristic::None;
            auto all24_plain = enumerate_pattern(r24); verified(r24, all24_plain);
            require(!all24_plain.solutions.empty(), "All24 pre-rotation failed to solve D' R D in 3 turns inside <R, U, F>");
            for (const auto& s : all24_plain.solutions) {
                bool seen_paid = false;
                for (auto m : s.moves) {
                    if (!m.is_rotation()) seen_paid = true;
                    else require(!seen_paid, "All24 pre-rotation emitted a mid-solve rotation after a paid turn");
                }
            }
            r24.heuristic = PatternHeuristic::Standard;
            r24.memoize_dead_ends = true;
            auto all24_pruned = enumerate_pattern(r24); verified(r24, all24_pruned);
            require(ordered_words(all24_plain) == ordered_words(all24_pruned),
                    "All24 pre-rotation heuristic pruning changed exact result set");

            // All24 pre-rotation + require_d_on_d (Cross/XCross finishing with bottom color on physical D):
            // 1. With outer moves only (<R, U, F>), "D' R D" cannot finish with D center on physical D if D is banned.
            auto r24_d_outer = r24;
            r24_d_outer.require_d_on_d = true;
            auto d_outer = enumerate_pattern(r24_d_outer); verified(r24_d_outer, d_outer);
            require(d_outer.solutions.empty(), "require_d_on_d failed to reject All24 outer-only solutions that left D center off D");

            // 2. With wide moves (<r, R, U>): scramble "U' R' r' x" (3 paid turns: U' R' r', followed by x so start.frame == 0!)
            // has inverse "x' r R U", and because r and R commute, its canonical deduplicated form in move_names order is "x' R r U",
            // which starts with pre-rotation x' (temporarily moving D center to F) and then wide move r brings the D center right back onto physical D!
            auto r_wide = request("U' R' r' x", "r r2 r' R R2 R' U U2 U'", 3);
            r_wide.modulo_u = false;
            r_wide.require_d_on_d = true;
            r_wide.pre_rotation = PatternPreRotation::YOnly;
            auto wide_y = enumerate_pattern(r_wide); verified(r_wide, wide_y);
            require(wide_y.solutions.empty(), "U' R' r' x should not be solvable in 3 turns in <r, R, U> with YOnly pre-rotation");

            r_wide.pre_rotation = PatternPreRotation::All24;
            r_wide.heuristic = PatternHeuristic::None;
            auto wide_24_plain = enumerate_pattern(r_wide); verified(r_wide, wide_24_plain);
            const auto wide_words = ordered_words(wide_24_plain);
            require(std::ranges::find(wide_words, "x' R r U") != wide_words.end(),
                    "All24 + require_d_on_d missed wide-move solution 'x' R r U' that brings D center back to D");
            for (const auto& s : wide_24_plain.solutions) {
                require(logical_face(s.terminal.frame, Face::D) == Face::D,
                        "All24 + require_d_on_d emitted a solution whose terminal D center is not on physical D");
            }
            r_wide.heuristic = PatternHeuristic::Standard;
            r_wide.memoize_dead_ends = true;
            auto wide_24_pruned = enumerate_pattern(r_wide); verified(r_wide, wide_24_pruned);
            require(ordered_words(wide_24_plain) == ordered_words(wide_24_pruned),
                    "All24 + require_d_on_d heuristic pruning changed exact result set");
        }
        std::cout << "pattern tests: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "pattern test failure after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
