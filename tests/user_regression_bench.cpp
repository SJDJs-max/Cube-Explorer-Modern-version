#include "cube/pattern.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
using namespace cube;
int main(int argc, char** argv) {
    try {
        PatternRequest request;
        request.pattern = pattern_from_cube(cube::apply(CubeState{}, parse_moves("B F' L2 U L2 U' L2 B' U' F")));
        request.allowed_moves = parse_moves("R R2 R' U U2 U' D D2 D'");
        request.max_turns = 16; request.collect_solutions = false;
        request.heuristic = argc > 1 && std::string_view(argv[1]) == "strong" ? PatternHeuristic::Strong : PatternHeuristic::Standard;
        std::set<std::string> formulas;
        const auto report = enumerate_pattern(request, [&](const auto& result) {
            if (!verify_pattern_solution(request, result) || result.moves.back().symbol == 'U')
                throw std::runtime_error("Independent replay or final-U regression failed.");
            formulas.insert(format_moves(result.moves));
        });
        if (!report.exhausted() || report.completed_depth != 16 || report.results != 18 || formulas.size() != 18)
            throw std::runtime_error("Benchmark did not fully enumerate its expected depth-sixteen set.");
        std::cout << "heuristic,preparation_ms,total_ms,nodes,results,completed_depth,stop\n"
            << (request.heuristic == PatternHeuristic::Strong ? "strong" : "standard") << ','
            << report.preparation_time.count() << ',' << report.elapsed.count() << ',' << report.nodes << ','
            << report.results << ',' << report.completed_depth << ',' << pattern_stop_name(report.stop) << '\n';
        for (const auto& formula : formulas) std::cout << formula << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
