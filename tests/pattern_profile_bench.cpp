#include "cube/pattern.hpp"
#include "../bench/v5/profile.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <set>
using namespace cube;
std::vector<Move> profile_moves(std::string_view symbols) {
    std::vector<Move> result;
    for (char s : symbols) for (int p = 1; p <= 3; ++p) result.push_back({s, static_cast<std::uint8_t>(p)});
    return result;
}
int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("case 1, 2, 3, dense, or rotations required");
        const std::string name = argv[1];
        PatternRequest request;
        request.collect_solutions = false;
        request.pattern = parse_pattern_facelets("?@?@U@?@????RRRRRR???FFFFFFDDDDDDDDD???LLLLLL???BBBBBB");
        if (name == "1") {
            request.pattern = apply_pattern(request.pattern, parse_moves("R U' R' U F' U L' U2 L F U"));
            request.allowed_moves = profile_moves("RUr"); request.max_turns = 11;
        } else if (name == "2") {
            request.pattern = apply_pattern(request.pattern, parse_moves("M' U R U' R' U' R U' r' U2 r U' r'"));
            request.allowed_moves = profile_moves("RUFr"); request.max_turns = 11;
        } else if (name == "3") {
            request.pattern = pattern_from_cube(cube::apply(CubeState{}, parse_moves("B F' L2 U L2 U' L2 B' U' F")));
            request.allowed_moves = profile_moves("RUD"); request.max_turns = 16;
        } else if (name == "dense" || name == "rotations") {
            request.pattern = PatternState{}; request.pattern.co[0] = 1;
            request.allowed_moves = name == "rotations" ? profile_moves("RUFrxy") : all_moves(false, true, false);
            request.max_turns = 4; request.max_results = 10000;
        } else throw std::runtime_error("Unknown benchmark case.");
        std::vector<PatternSolution> results;
        const auto report = enumerate_pattern(request, [&](const auto& result) { results.push_back(result); });
        const auto metrics = cube::profile::latest;
        // Outside the timed search: independently recheck every benchmark
        // result, including the isolated experiment that skips in-loop checks.
        std::set<std::string> traces;
        std::ofstream words;
        if (argc > 2) words.open(argv[2], std::ios::binary);
        for (const auto& result : results) {
            if (!verify_pattern_solution(request, result)) throw std::runtime_error("External result replay failed.");
            if (!traces.insert(commuting_key(result.moves)).second) throw std::runtime_error("Duplicate trace.");
            if (words.is_open()) words << result.turn_cost << ' ' << result.optimality_proven << ' ' << format_moves(result.moves) << '\n';
        }
        if (report.results != results.size()) throw std::runtime_error("Stream count mismatch.");
        const auto ms = [](auto time) { return std::chrono::duration<double, std::milli>(time).count(); };
        const double search_ms = report.elapsed.count() - report.preparation_time.count() - ms(metrics.dedup) - ms(metrics.verification) - ms(metrics.callback);
        std::cout << "case,total_ms,prep_ms,search_ms,dedup_ms,verification_ms,callback_ms,nodes,candidates,discarded,results,depth,stop\n"
            << std::fixed << std::setprecision(3) << name << ',' << report.elapsed.count() << ',' << report.preparation_time.count() << ','
            << search_ms << ',' << ms(metrics.dedup) << ',' << ms(metrics.verification) << ',' << ms(metrics.callback) << ','
            << report.nodes << ',' << metrics.candidates << ',' << metrics.discarded << ',' << report.results << ','
            << report.completed_depth << ',' << pattern_stop_name(report.stop) << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
