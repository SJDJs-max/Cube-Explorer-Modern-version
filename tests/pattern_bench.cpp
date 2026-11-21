#include "cube/pattern.hpp"
#include <iostream>

int main(int argc, char**) {
    using namespace cube;
    struct Case { const char* name; const char* scramble; bool partial; };
    const Case cases[] = {
        {"Sune-full", "R U R' U R U2 R'", false},
        {"Sune-orientation", "R U R' U R U2 R'", true},
        {"U-permutation", "R U' R U R U R U' R' U' R2", false}
    };
    std::cout << "case,heuristic,wide,prep_ms,total_ms,nodes,completed_depth,stop,solutions,first_cost\n";
    for (const auto& c : cases) for (bool wide : {false, true}) for (auto heuristic : {PatternHeuristic::Standard, PatternHeuristic::Strong}) {
        if (argc > 1 && (!wide || heuristic != PatternHeuristic::Standard)) continue;
        PatternRequest r;
        r.pattern = pattern_from_cube(cube::apply(CubeState{}, parse_moves(c.scramble)));
        if (c.partial) { for (int i = 0; i < 4; ++i) { r.pattern.cp[i] = -1; r.pattern.ep[i] = -1; } }
        r.allowed_moves = all_moves(false, wide, argc > 2);
        r.max_turns = 12; r.max_nodes = argc > 1 ? 100'000'000 : 5'000'000; r.max_results = 1;
        r.time_limit = std::chrono::milliseconds(15000);
        r.heuristic = heuristic;
        const auto report = enumerate_pattern(r);
        std::cout << c.name << ',' << (heuristic == PatternHeuristic::Strong ? "strong" : "standard") << ',' << wide << ','
            << report.preparation_time.count() << ',' << report.elapsed.count() << ',' << report.nodes << ',' << report.completed_depth << ','
            << pattern_stop_name(report.stop) << ',' << report.solutions.size() << ','
            << (report.solutions.empty() ? -1 : report.solutions.front().turn_cost) << '\n' << std::flush;
    }
}
