#include "cube/twophase.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main() {
    using Clock = std::chrono::steady_clock;
    const auto path = std::filesystem::path(CUBE_PROJECT_ROOT) / "tables/original";
    const auto state = cube::apply(cube::CubeState{}, cube::parse_moves("R U F2"));
    std::cout << std::fixed << std::setprecision(3);
    for (int repetition = 0; repetition < 6; ++repetition) {
        const auto started = Clock::now();
        cube::TwoPhaseSolver solver(path, [&](std::string_view message) {
            std::cout << "prepare=" << repetition << " elapsed_ms="
                      << std::chrono::duration<double, std::milli>(Clock::now()-started).count()
                      << " message=" << message << '\n';
        });
        const auto ready = Clock::now();
        const auto result = solver.solve(state);
        const auto solved = Clock::now();
        if (!result.solved || !cube::verify_sequence(state,result.solution.moves,result.solution.final_state))
            throw std::runtime_error("Startup sample failed independent replay");
        std::cout << "construct=" << repetition << " preparation_ms="
                  << std::chrono::duration<double, std::milli>(ready-started).count()
                  << " solve_ms=" << std::chrono::duration<double, std::milli>(solved-ready).count()
                  << " turns=" << result.solution.turn_count << '\n';
        if (repetition == 5) {
            const auto verification_started=Clock::now();
            for (int i=0;i<10000;++i)
                if (!cube::verify_sequence(state,result.solution.moves,result.solution.final_state))
                    throw std::runtime_error("Repeated independent verification failed");
            std::cout << "verification_10000_ms="
                      << std::chrono::duration<double,std::milli>(Clock::now()-verification_started).count() << '\n';
        }
    }
    const auto long_scramble=cube::parse_moves("R U2 F L2 D B' R2 F' D2 L U B2 R' F2 U' L2 B D R2 F");
    const auto long_state=cube::apply(cube::CubeState{},long_scramble);
    const auto long_solution=cube::inverse(long_scramble);
    const auto long_verification_started=Clock::now();
    for (int i=0;i<10000;++i)
        if (!cube::verify_sequence(long_state,long_solution,cube::CubeState{}))
            throw std::runtime_error("Long independent verification failed");
    std::cout << "verification_20_moves_10000_ms="
              << std::chrono::duration<double,std::milli>(Clock::now()-long_verification_started).count() << '\n';
}
