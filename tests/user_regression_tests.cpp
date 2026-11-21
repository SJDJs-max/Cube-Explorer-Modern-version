#include "cube/pattern.hpp"
#include <algorithm>
#include <deque>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace cube;
namespace {
std::size_t checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
std::vector<Move> profile(std::string_view symbols) {
    std::vector<Move> moves;
    for (char symbol : symbols) for (int power = 1; power <= 3; ++power)
        moves.push_back({symbol, static_cast<std::uint8_t>(power)});
    return moves;
}
const std::string zbls_facelets = "?@?@U@?@????RRRRRR???FFFFFFDDDDDDDDD???LLLLLL???BBBBBB";
FaceletState marked_facelets(std::string_view text) {
    FaceletState result;
    for (std::size_t i = 0; i < text.size(); ++i)
        result.colors[i] = text[i] == '?' ? -1 : text[i] == '@' ? 12 : static_cast<std::int8_t>(face_names.find(text[i]));
    return result;
}
FaceletState normalize_geometry(FaceletState input) {
    std::deque<FaceletState> queue{input};
    std::set<std::array<std::int8_t, 6>> seen;
    while (!queue.empty()) {
        const auto here = queue.front(); queue.pop_front();
        std::array<std::int8_t, 6> centers{};
        bool normalized = true;
        for (int face = 0; face < 6; ++face) {
            centers[face] = here.colors[face * 9 + 4];
            normalized &= centers[face] == face;
        }
        if (normalized) return here;
        if (!seen.insert(centers).second) continue;
        for (char axis : std::string_view("xyz")) queue.push_back(apply_facelets(here, Move{axis, 1}));
    }
    throw std::runtime_error("Geometric replay has no proper center frame.");
}
// Independent physical-sticker oracle for these exact user fixtures. Their
// anonymous markers are U-edge orientation markers; every other cubie is a
// complete set of known stickers or entirely unknown. No cubie move table or
// PatternState goal matching is used here.
bool geometric_goal(const FaceletState& state) {
    const auto actual = normalize_geometry(state);
    for (int u = 0; u < 4; ++u) {
        auto target = solved_facelets();
        if (u) target = apply_facelets(target, Move{'U', static_cast<std::uint8_t>(u)});
        bool match = true;
        for (int i = 0; i < 54; ++i) {
            const int color = actual.colors[i];
            if (color < 0) continue;
            if (color != 12) { match &= color == target.colors[i]; continue; }
            bool upright_edge = false;
            for (const auto& edge : edge_facelets) upright_edge |= edge[0] == i;
            match &= upright_edge;
        }
        if (match) return true;
    }
    return false;
}
void validate_user_solution(const PatternRequest& request, const FaceletState& scrambled,
                            const PatternSolution& solution, bool no_initial_u) {
    require(solution.verified, "Core published an unverified user result.");
    require(static_cast<bool>(verify_pattern_solution(request, solution)), "Independent core replay rejected user result.");
    require(solution.turn_cost == turn_count(solution.moves), "Structured action count differs from core cost.");
    require(solution.turn_cost + solution.rotations == static_cast<int>(solution.moves.size()), "Actions/cost/rotation count disagree.");
    auto raw = scrambled;
    for (auto move : solution.moves) {
        require(!geometric_goal(raw), "Formula continues after an earlier physical modulo-U goal.");
        raw = apply_facelets(raw, move);
    }
    require(geometric_goal(raw), "Direct marked-sticker replay does not restore the user's pattern.");
    if (no_initial_u && !solution.moves.empty()) require(solution.moves.front().symbol != 'U', "U-symmetric input produced a U prefix.");
    if (!solution.moves.empty() && request.allowed_moves == profile("RUD"))
        require(solution.moves.back().symbol != 'U', "RUD result has a redundant final U.");
}
std::set<std::string> trace_keys(const PatternReport& report) {
    std::set<std::string> result;
    for (const auto& solution : report.solutions) result.insert(commuting_key(solution.moves));
    return result;
}
void user_cases() {
    const auto base = parse_pattern_facelets(zbls_facelets);
    auto expected = pattern_from_cube(CubeState{});
    for (int i = 0; i < 4; ++i) { expected.cp[i] = expected.co[i] = expected.ep[i] = -1; expected.eo[i] = 0; }
    require(base == expected, "ZBLS Ctrl marker construction changed its semantics.");
    require(format_pattern_facelets(base) == zbls_facelets, "ZBLS marker pattern failed exact round trip.");
    {
        const auto scramble = parse_moves("R U' R' U F' U L' U2 L F U");
        const auto rejected = parse_moves("U' r' U2 R r U R' U' R' U2 r'");
        PatternRequest request;
        request.pattern = apply_pattern(base, scramble);
        request.allowed_moves = profile("RUr"); request.max_turns = 11;
        const auto raw = apply_facelets(marked_facelets(zbls_facelets), scramble);
        require(!geometric_goal(apply_facelets(raw, rejected)), "User's negative formula unexpectedly satisfies its sticker target.");
        for (auto direction : {PatternDirection::Solve, PatternDirection::Generate}) {
            request.direction = direction;
            const auto initial = direction == PatternDirection::Solve ? request.pattern : pattern_from_cube(CubeState{}, true);
            PatternSolution candidate{rejected, 11, 0, 0, apply_pattern(initial, rejected)};
            for (int u = 0; u < 4; ++u) {
                candidate.residual_u = u;
                require(!verify_pattern_solution(request, candidate), "User negative formula passed a modulo-U target.");
            }
        }
        request.direction = PatternDirection::Solve;
        const auto report = enumerate_pattern(request, [&](const auto& solution) { validate_user_solution(request, raw, solution, false); });
        require(report.exhausted() && report.completed_depth == 11, "First user case was not exhausted through eleven turns.");
        require(report.results == 0, "Unexpected RUr solution through eleven turns for the negative fixture.");
        std::cout << "user case 1: rejected by geometry; depth 11 exhausted, " << report.nodes << " nodes\n";
    }
    {
        const auto scramble = parse_moves("M' U R U' R' U' R U' r' U2 r U' r'");
        PatternRequest request;
        request.pattern = apply_pattern(base, scramble);
        request.allowed_moves = profile("RUFr"); request.max_turns = 11;
        const auto raw = apply_facelets(marked_facelets(zbls_facelets), scramble);
        const auto report = enumerate_pattern(request, [&](const auto& solution) {
            validate_user_solution(request, raw, solution, true);
            require(solution.optimality_proven, "First complete user solution depth lacks its algorithm proof.");
        });
        require(report.exhausted() && report.completed_depth == 11, "Second user case did not exhaust depth eleven.");
        require(report.root_u_symmetric, "Second user case was not recognized as U symmetric.");
        require(report.results == 5, "Second user's complete depth-eleven trace set changed.");
        auto redundant_prefix = report.solutions.front();
        redundant_prefix.moves.insert(redundant_prefix.moves.begin(), Move{'U', 1});
        ++redundant_prefix.turn_cost;
        redundant_prefix.terminal = apply_pattern(request.pattern, redundant_prefix.moves);
        request.max_turns = 12;
        require(!verify_pattern_solution(request, redundant_prefix), "Verifier accepted a redundant symmetric U prefix.");
        std::cout << "user case 2: U symmetry, " << report.results << " independently replayed formulas, " << report.nodes << " nodes\n";
    }
    {
        const auto scramble = parse_moves("B F' L2 U L2 U' L2 B' U' F");
        PatternRequest request;
        request.pattern = pattern_from_cube(cube::apply(CubeState{}, scramble));
        request.allowed_moves = profile("RUD"); request.max_turns = 16;
        request.heuristic = PatternHeuristic::Strong;
        request.collect_solutions = false;
        const auto raw = apply_facelets(solved_facelets(), scramble);
        std::size_t fifteen = 0, sixteen = 0;
        std::set<std::string> keys;
        std::vector<int> depths;
        const auto report = enumerate_pattern(request, [&](const auto& solution) {
            validate_user_solution(request, raw, solution, false);
            require(keys.insert(commuting_key(solution.moves)).second, "Commuting duplicate escaped the streaming normal form.");
            require(solution.turn_cost == 15 || solution.turn_cost == 16, "Unexpected user case-three solution depth.");
            require(solution.optimality_proven == (solution.turn_cost == 15), "Optimality mark does not follow the proven first depth.");
            solution.turn_cost == 15 ? ++fifteen : ++sixteen;
        }, [&](const auto& progress) { if (progress.depth_started) depths.push_back(progress.depth); });
        require(report.exhausted() && report.completed_depth == 16, "Third user case did not exhaust sixteen turns.");
        require(fifteen == 2 && sixteen == 16 && report.results == 18, "Third user's complete trace set changed.");
        require(report.solutions.empty(), "Streaming collection retained all formula objects.");
        require(!depths.empty() && depths.back() == 16, "Missing forced final-depth progress event.");
        for (std::size_t i = 1; i < depths.size(); ++i) require(depths[i] == depths[i - 1] + 1, "Depth boundary event omitted or duplicated a layer.");
        std::cout << "user case 3: depth 16 exhausted, 2 fifteen-turn + 16 sixteen-turn formulas, " << report.nodes << " nodes\n";
    }
}
void unlimited_and_proofs() {
    PatternRequest request;
    require(request.max_turns == -1 && !request.max_nodes && !request.max_results && request.time_limit.count() == 0, "Formula defaults still impose a budget.");
    request.pattern = pattern_from_cube(CubeState{});
    request.collect_solutions = false;
    std::size_t callbacks = 0;
    auto report = enumerate_pattern(request, [&](const auto& solution) { ++callbacks; require(solution.moves.empty() && solution.optimality_proven, "Solved root was not a proven zero-turn result."); });
    require(report.exhausted() && report.results == 1 && callbacks == 1 && report.solutions.empty(), "Unbounded solved input did not terminate finitely.");
    request.pattern = pattern_from_cube(cube::apply(CubeState{}, Move{'R', 1}));
    request.allowed_moves = profile("xyz");
    report = enumerate_pattern(request);
    require(report.exhausted() && report.results == 0, "Rotation-only search did not exhaust its finite logical set.");
    request.allowed_moves = profile("U");
    report = enumerate_pattern(request);
    require(report.exhausted() && report.results == 0, "Unreachable projected state was not recognized as exhausted.");
    request.heuristic = PatternHeuristic::None;
    bool cancel = false;
    request.cancelled = [&] { return cancel; };
    std::vector<int> depths;
    report = enumerate_pattern(request, {}, [&](const auto& progress) {
        if (progress.depth_started) { depths.push_back(progress.depth); if (progress.depth == 300) cancel = true; }
    });
    require(report.stop == PatternStop::Cancelled && depths.size() == 301 && depths.back() == 300,
        "Unlimited depth stopped at a former 40/255 cap or lost a boundary event.");
    request.cancelled = {}; request.max_turns = 320; request.min_turns = 300;
    report = enumerate_pattern(request);
    require(report.exhausted() && report.completed_depth == 320, "Large finite depth interval was rejected.");
    request.min_turns = 0; request.max_turns = -2;
    require(enumerate_pattern(request).stop == PatternStop::InvalidInput, "Invalid negative maximum was accepted.");
    request.max_turns = -1; request.allowed_moves = profile("RU");
    callbacks = 0; request.max_results = 1;
    report = enumerate_pattern(request, [&](const auto& solution) { ++callbacks; require(solution.optimality_proven, "Shortest depth was not proved."); });
    require(report.stop == PatternStop::ResultLimit && callbacks == 1 && report.results == 1 && report.solutions.empty(), "Streaming result budget used the empty retained vector.");
    request.max_results = 0; cancel = false; request.cancelled = [&] { return cancel; };
    report = enumerate_pattern(request, [&](const auto&) { cancel = true; });
    require(report.stop == PatternStop::Cancelled && report.results == 1, "Callback cancellation was not immediately honored.");
    request.cancelled = {}; request.max_turns = 2;
    PatternSolution redundant_tail{parse_moves("R' U"), 2, 0, 1,
        apply_pattern(request.pattern, parse_moves("R' U"))};
    require(!verify_pattern_solution(request, redundant_tail), "Verifier accepted a U after an earlier solved state.");
    request.cancelled = {}; request.pattern = pattern_from_cube(cube::apply(CubeState{}, parse_moves("R U")));
    request.min_turns = 2; request.max_turns = 2;
    report = enumerate_pattern(request, [&](const auto& solution) { require(!solution.optimality_proven, "Skipped lower depths received a false optimality proof."); });
    require(report.results > 0, "Skipped-depth proof fixture found no solutions.");
}
void streaming_trace_parity() {
    for (auto scramble : {"R U", "r U", "x R U", "l R r"})
        for (auto direction : {PatternDirection::Solve, PatternDirection::Generate}) {
            PatternRequest request;
            request.pattern = pattern_from_cube(cube::apply(CubeState{}, parse_moves(scramble)));
            request.direction = direction;
            request.allowed_moves = parse_moves("R R' U U' r r' x x' y y'");
            request.max_turns = 3; request.heuristic = PatternHeuristic::None; request.memoize_dead_ends = false;
            request.deduplicate_commuting = false;
            const auto unmerged = enumerate_pattern(request);
            require(unmerged.exhausted(), "Plain shallow trace reference did not exhaust.");
            request.deduplicate_commuting = true;
            const auto merged = enumerate_pattern(request);
            require(merged.exhausted(), "Streaming shallow trace normal form did not exhaust.");
            require(trace_keys(unmerged) == trace_keys(merged), "Memory-bounded deduplication changed the complete shallow trace set.");
            require(trace_keys(merged).size() == merged.results, "Streaming normal form emitted a trace twice.");
            request.heuristic = PatternHeuristic::Standard; request.memoize_dead_ends = true;
            const auto pruned = enumerate_pattern(request);
            require(pruned.exhausted() && trace_keys(pruned) == trace_keys(merged), "Reachable-frame projection changed a shallow trace set.");
        }
}
}
int main() {
    try {
        user_cases(); unlimited_and_proofs(); streaming_trace_parity();
        std::cout << "user regression tests: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "user regression failure after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
