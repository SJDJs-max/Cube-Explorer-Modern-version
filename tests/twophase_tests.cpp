#include "cube/twophase.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <stop_token>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void verify(const cube::CubeState& start, const cube::TwoPhaseResult& result) {
    require(result.solved, "no solution: " + result.message);
    require(result.solution.turn_count == static_cast<int>(result.solution.moves.size()), "wrong algorithm turn count");
    require(result.solution.phase1_count + result.solution.phase2_count == result.solution.turn_count, "wrong phase lengths");
    require(cube::is_solved(result.solution.final_state), "reported endpoint is not solved");
    require(cube::apply(start, result.solution.moves) == result.solution.final_state, "incorrect cubie replay");
    require(static_cast<bool>(cube::verify_sequence(start, result.solution.moves, result.solution.final_state)), "independent replay failed");
    for (const auto m : result.solution.moves)
        require(cube::face_names.find(m.symbol) != std::string_view::npos, "ordinary solver introduced non-face move");
    require(result.solution.search_axis >= 0 && result.solution.search_axis < 3, "invalid two-phase axis");
    for (int i=result.solution.phase1_count;i<result.solution.turn_count;++i) {
        const auto move=result.solution.moves[i];
        const auto physical=static_cast<cube::Face>(cube::face_names.find(move.symbol));
        const int logical=static_cast<int>(cube::logical_face(start.frame,physical));
        require(move.power==2 || logical%3==result.solution.search_axis,
                "phase-two suffix is outside the selected axis subgroup");
    }
}

template<std::size_t N>
int permutation_parity(const std::array<std::uint8_t, N>& p) {
    int parity = 0;
    for (std::size_t i = 0; i < N; ++i)
        for (std::size_t j = i + 1; j < N; ++j) parity ^= p[i] > p[j];
    return parity;
}
}

int main(int argc, char** argv) {
    try {
        const auto initial = std::chrono::steady_clock::now();
        const auto directory = std::filesystem::path(CUBE_PROJECT_ROOT) / "tables" / "original";
        std::stop_source preparation;
        preparation.request_stop();
        bool preparation_cancelled = false;
        try { cube::TwoPhaseSolver cancelled(directory, {}, preparation.get_token()); }
        catch (const std::runtime_error& e) {
            preparation_cancelled = std::string_view(e.what()).find("cancelled") != std::string_view::npos;
        }
        require(preparation_cancelled, "table preparation cancellation ignored");
        cube::TwoPhaseSolver solver(directory, [](std::string_view step) { std::cout << step << '\n'; });
        const auto init_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - initial).count();
        cube::TwoPhaseOptions short_options;
        short_options.max_moves = 3;
        short_options.target_moves = 3;
        short_options.time_limit = std::chrono::milliseconds(2000);
        const cube::CubeState solved;
        auto zero = solver.solve(solved, short_options);
        verify(solved, zero);
        require(zero.solution.turn_count == 0, "solved cube is not zero turns");
        for (int face = 0; face < 6; ++face) for (int power = 1; power <= 3; ++power) {
            const cube::Move move{cube::face_names[face], static_cast<std::uint8_t>(power)};
            const auto c = cube::apply(solved, move);
            auto options = short_options;
            options.max_moves = options.target_moves = 1;
            auto result = solver.solve(c, options);
            verify(c, result);
            require(result.solution.turn_count == 1, "one-move cube not solved in one move");
        }

        const auto rotated = cube::apply(solved, cube::parse_moves("x y R U F2 L D'"));
        auto rotated_options = short_options;
        rotated_options.max_moves = 30;
        rotated_options.target_moves = 22;
        verify(rotated, solver.solve(rotated, rotated_options));

        auto invalid = solved;
        invalid.cubies.eo[0] = 1;
        require(solver.solve(invalid).stop_reason == cube::TwoPhaseStop::invalid_input, "edge-flip parity accepted");
        std::stop_source source;
        source.request_stop();
        require(solver.solve(rotated, {}, source.get_token()).stop_reason == cube::TwoPhaseStop::cancelled, "pre-cancellation ignored");
        auto limited = short_options;
        limited.max_moves = 0;
        auto bounded = solver.solve(rotated, limited);
        require(!bounded.solved && bounded.stop_reason == cube::TwoPhaseStop::depth_limit, "depth-limit status wrong");
        limited.max_moves = 30;
        limited.node_limit = 1;
        auto node_limited = solver.solve(rotated, limited);
        require(node_limited.stop_reason == cube::TwoPhaseStop::node_limit && node_limited.nodes == 1, "node limit ignored");

        cube::CubeState superflip;
        superflip.cubies.eo.fill(1);
        auto timed_options = short_options;
        timed_options.max_moves = 30;
        timed_options.target_moves = 0;
        timed_options.time_limit = std::chrono::milliseconds(1);
        auto timed = solver.solve(superflip, timed_options);
        require(timed.stop_reason == cube::TwoPhaseStop::time_limit, "time budget ignored");
        if (timed.solved) verify(superflip, timed);

        // Cancelling after a streamed candidate must retain that exact result.
        std::stop_source after_candidate;
        auto improve_options = short_options;
        improve_options.max_moves = 30;
        improve_options.target_moves = 0;
        int callbacks = 0;
        auto improved = solver.solve(rotated, improve_options, after_candidate.get_token(),
            [&](const cube::TwoPhaseSolution&) { ++callbacks; after_candidate.request_stop(); });
        verify(rotated, improved);
        require(callbacks == 1 && improved.stop_reason == cube::TwoPhaseStop::cancelled, "streamed-candidate cancellation lost result");

        std::vector<std::future<cube::TwoPhaseResult>> parallel;
        auto shared = solver;
        for (int i = 0; i < 8; ++i)
            parallel.push_back(std::async(std::launch::async, [shared, rotated, rotated_options] {
                return shared.solve(rotated, rotated_options);
            }));
        for (auto& future : parallel) verify(rotated, future.get());

        // Independently constructed GUI workers must reuse the same immutable
        // tables, including canonical aliases and simultaneous constructors.
        std::atomic<int> rebuilt_tables=0;
        std::vector<std::future<void>> constructors;
        for (int i=0;i<8;++i)
            constructors.push_back(std::async(std::launch::async,[&,i] {
                const auto alias=i%2?directory:directory/".."/"original";
                cube::TwoPhaseSolver again(alias,[&](std::string_view message) {
                    if (message.find("Reusing")==std::string_view::npos) ++rebuilt_tables;
                });
                verify(rotated,again.solve(rotated,rotated_options));
            }));
        for (auto& future:constructors) future.get();
        require(rebuilt_tables==0,"independent solvers rebuilt shared tables");
        bool warm_cancelled=false;
        try { cube::TwoPhaseSolver cancelled(directory,{},preparation.get_token()); }
        catch(const std::runtime_error&) { warm_cancelled=true; }
        require(warm_cancelled,"cached table constructor ignored pre-cancellation");

        auto triple_options=rotated_options;
        triple_options.use_triple_search=true;
        for(int frame=0;frame<24;++frame) {
            cube::CubeState oriented;
            oriented.frame=static_cast<std::uint8_t>(frame);
            oriented=cube::apply(oriented,cube::parse_moves("R U F2 L D' B"));
            const auto result=solver.solve(oriented,triple_options);
            verify(oriented,result);
            require(result.search_axes==3,"triple option did not run three-axis search");
        }
        auto triple_nodes=triple_options;
        triple_nodes.node_limit=1;
        const auto node_budget=solver.solve(superflip,triple_nodes);
        require(node_budget.stop_reason==cube::TwoPhaseStop::node_limit && node_budget.nodes==1,
                "triple search multiplied the shared node budget");
        auto triple_time=timed_options;
        triple_time.use_triple_search=true;
        const auto time_budget=solver.solve(superflip,triple_time);
        require(time_budget.stop_reason==cube::TwoPhaseStop::time_limit,"triple time budget ignored");
        if(time_budget.solved) verify(superflip,time_budget);
        require(solver.solve(rotated,triple_options,source.get_token()).stop_reason==cube::TwoPhaseStop::cancelled,
                "triple pre-cancellation ignored");
        std::stop_source triple_after_candidate;
        triple_options.target_moves=0;
        int triple_callbacks=0;
        const auto triple_cancelled=solver.solve(rotated,triple_options,triple_after_candidate.get_token(),
            [&](const cube::TwoPhaseSolution&) { ++triple_callbacks; triple_after_candidate.request_stop(); });
        verify(rotated,triple_cancelled);
        require(triple_callbacks==1 && triple_cancelled.stop_reason==cube::TwoPhaseStop::cancelled,
                "triple candidate cancellation must retain one verified candidate");
        for (const bool triple : {false, true}) {
            for (const std::uint64_t budget : {2u, 31u, 1024u, 10000u}) {
                auto budget_options=triple_options;
                budget_options.use_triple_search=triple;
                budget_options.target_moves=0;
                budget_options.time_limit=std::chrono::milliseconds{0};
                budget_options.node_limit=budget;
                const auto bounded_search=solver.solve(superflip,budget_options);
                require(bounded_search.stop_reason==cube::TwoPhaseStop::node_limit && bounded_search.nodes==budget,
                        "ordinary search changed the exact shared node budget");
                if (bounded_search.solved) verify(superflip,bounded_search);
            }
        }
        triple_options.target_moves=22;
        parallel.clear();
        for (int i=0;i<8;++i)
            parallel.push_back(std::async(std::launch::async,[shared,rotated,triple_options] {
                return shared.solve(rotated,triple_options);
            }));
        for(auto& future:parallel) verify(rotated,future.get());

        // Exercise phase two directly, including the projection's blind spot:
        // solved corners/UD edges with an even middle-slice permutation.
        auto phase2_options=rotated_options;
        phase2_options.max_phase1_moves=0;
        phase2_options.target_moves=30;
        cube::CubeState middle_cycle;
        middle_cycle.cubies.ep[8]=9;
        middle_cycle.cubies.ep[9]=10;
        middle_cycle.cubies.ep[10]=8;
        auto middle_result=solver.solve(middle_cycle,phase2_options);
        verify(middle_cycle,middle_result);
        require(middle_result.solution.phase1_count==0 && middle_result.solution.turn_count>0,
                "phase-two projection ignored the middle-slice permutation");
        std::mt19937 subgroup_random(0x58e71b2u);
        const auto subgroup_moves=cube::parse_moves("U U2 U' R2 F2 D D2 D' L2 B2");
        for (int i=0;i<24;++i) {
            cube::CubeState subgroup;
            for (int turn=0;turn<20;++turn)
                subgroup=cube::apply(subgroup,subgroup_moves[subgroup_random()%subgroup_moves.size()]);
            const auto result=solver.solve(subgroup,phase2_options);
            verify(subgroup,result);
            require(result.solution.phase1_count==0,"G1 fixture unexpectedly used phase one");
        }

        const int random_count = argc > 1 ? std::stoi(argv[1]) : 30;
        std::mt19937 random(0x5cfee229u);
        std::uint64_t total_nodes = 0;
        std::int64_t total_ms = 0;
        int longest = 0;
        std::ofstream samples;
        if (argc > 2) {
            samples.open(argv[2]);
            if (!samples) throw std::runtime_error("cannot write benchmark sample file");
            samples << "facelets\tturns\tnodes\telapsed_us\tformula\tfirst_candidate_us\tcandidate_formulas\n";
        }
        const auto batch_start = std::chrono::steady_clock::now();
        std::chrono::nanoseconds search_time{};
        for (int i = 0; i < random_count; ++i) {
            cube::CubeState state;
            if ((i & 1) == 0) {
                int last = -1;
                for (int n = 0; n < 25; ++n) {
                    int f;
                    do { f = static_cast<int>(random() % 6); } while (f == last);
                    last = f;
                    state = cube::apply(state, cube::Move{cube::face_names[f], static_cast<std::uint8_t>(random() % 3 + 1)});
                }
            } else {
                // Sample legal cubie states directly, independently of moves.
                std::shuffle(state.cubies.cp.begin(), state.cubies.cp.end(), random);
                std::shuffle(state.cubies.ep.begin(), state.cubies.ep.end(), random);
                if (permutation_parity(state.cubies.cp) != permutation_parity(state.cubies.ep))
                    std::swap(state.cubies.ep[0], state.cubies.ep[1]);
                int twists = 0, flips = 0;
                for (int c = 0; c < 7; ++c) {
                    state.cubies.co[c] = static_cast<std::uint8_t>(random() % 3);
                    twists += state.cubies.co[c];
                }
                state.cubies.co[7] = static_cast<std::uint8_t>((3 - twists % 3) % 3);
                for (int e = 0; e < 11; ++e) {
                    state.cubies.eo[e] = static_cast<std::uint8_t>(random() % 2);
                    flips += state.cubies.eo[e];
                }
                state.cubies.eo[11] = static_cast<std::uint8_t>(flips % 2);
            }
            cube::TwoPhaseOptions options;
            options.target_moves = argc > 3 ? std::stoi(argv[3]) : 21;
            options.use_triple_search = argc > 4 && std::stoi(argv[4]) != 0;
            options.time_limit = std::chrono::milliseconds(3000);
            int last_candidate_length = options.max_moves+1;
            std::string candidate_formulas;
            std::chrono::nanoseconds first_candidate_time{};
            const auto solve_start = std::chrono::steady_clock::now();
            auto result = solver.solve(state, options, {}, [&](const cube::TwoPhaseSolution& candidate) {
                require(candidate.turn_count < last_candidate_length, "streamed candidates did not strictly improve");
                require(candidate.turn_count == static_cast<int>(candidate.moves.size()) &&
                        candidate.phase1_count+candidate.phase2_count == candidate.turn_count,
                        "streamed candidate changed algorithm step counts");
                require(cube::is_solved(candidate.final_state), "streamed candidate is not solved");
                if (candidate_formulas.empty())
                    first_candidate_time=std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now()-solve_start);
                else candidate_formulas += " | ";
                candidate_formulas += cube::format_moves(candidate.moves);
                last_candidate_length=candidate.turn_count;
            });
            const auto solve_elapsed = std::chrono::steady_clock::now() - solve_start;
            search_time += std::chrono::duration_cast<std::chrono::nanoseconds>(solve_elapsed);
            verify(state, result);
            total_nodes += result.nodes;
            total_ms += result.elapsed.count();
            longest = std::max(longest, result.solution.turn_count);
            std::cout << "sample=" << i << " turns=" << result.solution.turn_count
                      << " nodes=" << result.nodes << " ms=" << result.elapsed.count()
                      << " stop=" << cube::to_string(result.stop_reason) << '\n';
            if (samples)
                samples << cube::format_facelets(cube::to_facelets(state)) << '\t'
                        << result.solution.turn_count << '\t' << result.nodes << '\t'
                        << std::fixed << std::setprecision(3)
                        << std::chrono::duration<double, std::micro>(solve_elapsed).count() << '\t'
                        << cube::format_moves(result.solution.moves) << '\t'
                        << std::chrono::duration<double,std::micro>(first_candidate_time).count() << '\t'
                        << candidate_formulas << '\n';
        }
        const auto batch_elapsed = std::chrono::steady_clock::now() - batch_start;
        std::cout << "PASS two-phase: init_ms=" << init_ms << " resident_bytes=" << solver.table_info().resident_bytes
                  << " samples=" << random_count << " reported_ms_sum=" << total_ms
                  << " search_wall_ms=" << std::chrono::duration<double, std::milli>(search_time).count()
                  << " batch_wall_ms=" << std::chrono::duration<double, std::milli>(batch_elapsed).count()
                  << " total_nodes=" << total_nodes << " longest=" << longest << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL two-phase: " << error.what() << '\n';
        return 1;
    }
}
