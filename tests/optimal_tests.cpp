#include "cube/optimal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <queue>
#include <random>
#include <stdexcept>
#include <unordered_map>

namespace {
int checks=0;
void require(bool value,const char* message) { ++checks; if(!value) throw std::runtime_error(message); }
std::string key(const cube::CubieState& c) {
    std::string out;
    for(auto x:c.cp)out+=static_cast<char>(x);
    for(auto x:c.co)out+=static_cast<char>(x);
    for(auto x:c.ep)out+=static_cast<char>(x);
    for(auto x:c.eo)out+=static_cast<char>(x);
    return out;
}
void verify(const cube::CubeState& input,const cube::OptimalResult& result) {
    if(!result.solved)throw std::runtime_error("No optimal-search solution: "+std::string(cube::to_string(result.stop_reason))+" / "+result.message+" / "+cube::format_facelets(cube::to_facelets(input)));
    ++checks;
    require(cube::is_solved(result.solution.final_state),"Optimal result not exactly solved");
    require(result.solution.turn_count==static_cast<int>(result.solution.moves.size()),"Optimal result count differs from actions");
    require(static_cast<bool>(cube::verify_sequence(input,result.solution.moves,result.solution.final_state)),"Independent optimal replay failed");
    for(auto m:result.solution.moves)require(cube::face_names.find(m.symbol)!=std::string_view::npos,"Optimal ordinary solver emitted non-face move");
}
}

int main() {
    try {
        const auto path=std::filesystem::path(CUBE_PROJECT_ROOT)/"tables/original";
        cube::TwoPhaseSolver tables(path);cube::OptimalSolver solver(tables);
        // Exhaustive ordinary BFS is an independent optimal-distance oracle;
        // it uses no PDB and no axis or successor pruning.
        std::vector<std::pair<cube::CubieState,int>> states{{cube::CubieState{},0}};
        std::unordered_map<std::string,int> distance{{key(states[0].first),0}};
        for(std::size_t i=0;i<states.size();++i) {
            const auto [state,depth]=states[i];
            if(depth==4)continue;
            for(int face=0;face<6;++face)for(int power=1;power<=3;++power) {
                const auto child=cube::compose(state,cube::outer_move(static_cast<cube::Face>(face),power));
                if(distance.emplace(key(child),depth+1).second)states.emplace_back(child,depth+1);
            }
        }
        for(std::size_t i=0;i<states.size();++i) {
            const auto& [state,depth]=states[i];
            require(solver.lower_bound(state)<=depth,"Three-axis PDB overestimated exact BFS distance");
            if(depth>2 && i%137)continue;
            cube::CubeState input;input.cubies=state;
            cube::OptimalOptions options;options.max_moves=depth;
            const auto result=solver.solve(input,options);
            verify(input,result);
            require(result.optimality_proven,"Exhaustive IDA result lacks optimality proof");
            require(result.solution.turn_count==depth,"IDA length disagrees with exact BFS distance");
        }
        // O(1) cursor transitions must remain identical to fresh exact PDB roots.
        std::mt19937 random(0x21a911);
        cube::CubieState walk;
        auto cursor=tables.phase1_cursor(walk);
        for(int i=0;i<3000;++i) {
            const int m=static_cast<int>(random()%18);
            walk=cube::compose(walk,cube::outer_move(static_cast<cube::Face>(m/3),m%3+1));
            cursor=tables.advance_phase1(cursor,m);
            require(cursor.distance==tables.phase1_lower_bound(walk),"Incremental phase-one cursor distance drifted");
        }
        for(int frame=0;frame<24;++frame) {
            cube::CubeState input;input.frame=static_cast<std::uint8_t>(frame);
            input=cube::apply(input,cube::parse_moves("R U F2"));
            cube::OptimalOptions options;options.max_moves=3;
            const auto result=solver.solve(input,options);
            verify(input,result);require(result.optimality_proven,"Frame-mapped result not optimal");
            require(result.solution.turn_count==3,"Frame changed the shortest length");
        }
        const auto input=cube::apply(cube::CubeState{},cube::parse_moves("R U R' U'"));
        cube::OptimalOptions options;options.max_moves=10;
        auto first=solver.solve(input,options);verify(input,first);
        require(first.solution.turn_count==4 && first.optimality_proven,"Known commutator shortest distance wrong");
        options.excluded_solutions.push_back(cube::format_moves(first.solution.moves));
        auto next=solver.solve(input,options);verify(input,next);
        require(cube::format_moves(next.solution.moves)!=options.excluded_solutions[0],"Next repeated an excluded formula");
        require(next.optimality_proven==(next.solution.turn_count==4),"Next formula has a false optimality flag");
        options.initial_solution=next.solution.moves;
        auto next_upper_bound=solver.solve(input,options);verify(input,next_upper_bound);
        require(next_upper_bound.optimality_proven==(next_upper_bound.solution.turn_count==4),"Excluded shorter target falsely proved a longer upper bound optimal");
        // A deliberately non-shortest but valid candidate must survive a budget
        // interruption without receiving an optimality marker.
        options={};options.initial_solution=cube::parse_moves("U R U' R' F F'");options.node_limit=1;
        auto limited=solver.solve(input,options);verify(input,limited);
        require(!limited.optimality_proven && limited.stop_reason==cube::OptimalStop::node_limit,"Budget-limited candidate falsely proved optimal");
        options.node_limit=0;
        auto improved=solver.solve(input,options);verify(input,improved);
        require(improved.optimality_proven && improved.solution.turn_count==4,"Initial upper bound prevented a shorter proof");
        options.initial_solution=first.solution.moves;
        auto proved=solver.solve(input,options);verify(input,proved);
        require(proved.optimality_proven,"Meeting verified upper and lower bounds failed proof");
        std::stop_source cancelled;cancelled.request_stop();
        auto stopped=solver.solve(input,options,cancelled.get_token());verify(input,stopped);
        require(!stopped.optimality_proven && stopped.stop_reason==cube::OptimalStop::cancelled,"Pre-cancel ignored or falsely proved optimal");
        options={};options.max_moves=3;
        auto too_shallow=solver.solve(input,options);
        require(!too_shallow.solved && too_shallow.stop_reason==cube::OptimalStop::depth_limit,"Insufficient max depth returned a false solution");
        options={};options.initial_solution=cube::parse_moves("R");
        auto invalid=solver.solve(input,options);
        require(invalid.stop_reason==cube::OptimalStop::invalid_input,"Incorrect initial candidate accepted");
        options={};options.max_moves=-1;
        require(solver.solve(input,options).stop_reason==cube::OptimalStop::invalid_input,"Negative max depth accepted");
        options={};options.time_limit=std::chrono::milliseconds(1);
        const auto difficult=cube::apply(cube::CubeState{},cube::parse_moves("R U2 F L2 D B' R2 F' D2 L U B2 R' F2"));
        auto timed=solver.solve(difficult,options);
        require(timed.stop_reason==cube::OptimalStop::time_limit || timed.optimality_proven,"Time budget failed to stop");
        std::stop_source during;
        options={};
        auto interrupted=solver.solve(difficult,options,during.get_token(),[&](const auto&){during.request_stop();});
        require(interrupted.stop_reason==cube::OptimalStop::cancelled,"Progress cancellation failed");
        std::cout<<"PASS optimal search: "<<checks<<" checks, "<<states.size()<<" exhaustive BFS states through depth 4.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL optimal search: "<<error.what()<<'\n';return 1; }
}
