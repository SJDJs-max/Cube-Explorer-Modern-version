#include "cube/model.hpp"
#include "cube/pattern.hpp"

#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>

using namespace cube;
namespace {
constexpr std::array<std::array<int,3>,8> corner_colors={{{0,1,2},{0,2,4},{0,4,5},{0,5,1},{3,2,1},{3,4,2},{3,5,4},{3,1,5}}};
constexpr std::array<std::array<int,2>,12> edge_colors={{{0,1},{0,2},{0,4},{0,5},{3,1},{3,2},{3,4},{3,5},{2,1},{2,4},{5,4},{5,1}}};
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
const std::array<std::vector<Move>,24>& frame_words() {
    static const auto words=[] {
        std::array<std::vector<Move>,24> result;
        std::array<bool,24> seen{};seen[0]=true;
        std::vector<std::uint8_t> queue{0};
        for(std::size_t i=0;i<queue.size();++i)for(char symbol:std::string_view("xyz")) {
            const auto target=rotated_frame(queue[i],{symbol,1});
            if(!seen[target]){seen[target]=true;result[target]=result[queue[i]];result[target].push_back({symbol,1});queue.push_back(target);}
        }
        return result;
    }();
    return words;
}

// Encodes exactly the original FaceCube marker meanings, independently from
// pattern.cpp. 0..5 = exact colors, 6..11 = ignore orientation, 12 = OriCol.
FaceletState encode_markers(const PatternState& state) {
    FaceletState stickers;stickers.colors.fill(-1);
    for(int f=0;f<6;++f)stickers.colors[9*f+4]=static_cast<std::int8_t>(f);
    for(int p=0;p<8;++p) {
        if(state.cp[p]>=0) {
            const int orientation=state.co[p]<0?0:state.co[p];
            for(int n=0;n<3;++n)stickers.colors[corner_facelets[p][(n+orientation)%3]]=static_cast<std::int8_t>(corner_colors[state.cp[p]][n]+(state.co[p]<0?6:0));
        } else if(state.co[p]>=0)stickers.colors[corner_facelets[p][state.co[p]]]=12;
    }
    for(int p=0;p<12;++p) {
        if(state.ep[p]>=0) {
            const int orientation=state.eo[p]<0?0:state.eo[p];
            for(int n=0;n<2;++n)stickers.colors[edge_facelets[p][(n+orientation)%2]]=static_cast<std::int8_t>(edge_colors[state.ep[p]][n]+(state.eo[p]<0?6:0));
        } else if(state.eo[p]>=0)stickers.colors[edge_facelets[p][state.eo[p]]]=12;
    }
    for(int f=0;f<6;++f)stickers.center_ori[f]=state.center_ori[f]<0?0:static_cast<std::uint8_t>(state.center_ori[f]);
    return apply_facelets(stickers,frame_words()[state.frame]);
}
PatternState decode_markers(const FaceletState& physical) {
    int frame=-1;
    for(int f=0;f<24;++f) {
        const auto centers=apply_facelets(solved_facelets(),frame_words()[f]);
        bool equal=true;for(int face=0;face<6;++face)equal=equal&&centers.colors[face*9+4]==physical.colors[face*9+4];
        if(equal){frame=f;break;}
    }
    require(frame>=0,"Marker geometry centers must have a valid frame");
    const auto canonical=apply_facelets(physical,inverse(frame_words()[frame]));
    PatternState state;state.frame=static_cast<std::uint8_t>(frame);
    for(int pos=0;pos<8;++pos) {
        std::array<int,3> colors{};for(int i=0;i<3;++i)colors[i]=canonical.colors[corner_facelets[pos][i]];
        if(std::find(colors.begin(),colors.end(),12)!=colors.end()){state.co[pos]=static_cast<std::int8_t>(std::find(colors.begin(),colors.end(),12)-colors.begin());continue;}
        if(colors[0]<0)continue;
        const bool ignored=colors[0]>=6;for(auto& c:colors)c%=6;
        int orientation=0;while(orientation<3&&colors[orientation]!=0&&colors[orientation]!=3)++orientation;
        require(orientation<3,"Missing primary corner marker");
        for(int piece=0;piece<8;++piece)if(corner_colors[piece][0]==colors[orientation]&&corner_colors[piece][1]==colors[(orientation+1)%3]&&corner_colors[piece][2]==colors[(orientation+2)%3]) {
            state.cp[pos]=static_cast<std::int8_t>(piece);state.co[pos]=ignored?-1:static_cast<std::int8_t>(orientation);break;
        }
        require(state.cp[pos]>=0,"Invalid corner marker colors");
    }
    for(int pos=0;pos<12;++pos) {
        std::array<int,2> colors{};for(int i=0;i<2;++i)colors[i]=canonical.colors[edge_facelets[pos][i]];
        if(std::find(colors.begin(),colors.end(),12)!=colors.end()){state.eo[pos]=static_cast<std::int8_t>(std::find(colors.begin(),colors.end(),12)-colors.begin());continue;}
        if(colors[0]<0)continue;
        const bool ignored=colors[0]>=6;for(auto& c:colors)c%=6;
        for(int piece=0;piece<12;++piece)for(int ori=0;ori<2;++ori)if(edge_colors[piece][ori]==colors[0]&&edge_colors[piece][1-ori]==colors[1]) {
            state.ep[pos]=static_cast<std::int8_t>(piece);state.eo[pos]=ignored?-1:static_cast<std::int8_t>(ori);
        }
        require(state.ep[pos]>=0,"Invalid edge marker colors");
    }
    return state;
}
void independent_partial_transport() {
    std::mt19937 random(0x50415454U);
    for(int sample=0;sample<100;++sample) {
        CubeState complete;
        for(int i=0;i<25;++i)complete=cube::apply(complete,move_from_index(static_cast<int>(random()%54)));
        auto pattern=pattern_from_cube(complete,false);
        for(int i=0;i<8;++i) {
            const auto mask=random()%4;
            if(mask&1)pattern.cp[i]=-1;
            if(mask&2)pattern.co[i]=-1;
        }
        for(int i=0;i<12;++i) {
            const auto mask=random()%4;
            if(mask&1)pattern.ep[i]=-1;
            if(mask&2)pattern.eo[i]=-1;
        }
        const auto encoded=encode_markers(pattern);
        require(decode_markers(encoded)==pattern,"Independent partial encoding roundtrip failed");
        for(auto move:all_moves()) {
            const auto expected=decode_markers(apply_facelets(encoded,move));
            const auto actual=apply_pattern(pattern,move);
            require(actual==expected,"Partial state transport disagrees with physical stickers for "+format_move(move)+" at sample "+std::to_string(sample));
            require(apply_pattern(actual,inverse(move))==pattern,"Partial inverse lost constraints");
        }
    }
}
void pattern_text_roundtrip() {
    std::mt19937 random(711);
    for(int sample=0;sample<200;++sample) {
        CubeState complete;
        for(int i=0;i<12;++i)complete=cube::apply(complete,move_from_index(static_cast<int>(random()%54)));
        auto pattern=pattern_from_cube(complete,false);
        for(int i=0;i<8;++i){if(random()%2)pattern.cp[i]=-1;if(random()%2)pattern.co[i]=-1;}
        for(int i=0;i<12;++i){if(random()%2)pattern.ep[i]=-1;if(random()%2)pattern.eo[i]=-1;}
        require(parse_pattern_facelets(format_pattern_facelets(pattern))==pattern,"Pattern text roundtrip failed with frame "+std::to_string(pattern.frame));
    }
}
void center_constraint_transport() {
    auto complete=cube::apply(CubeState{},parse_moves("r U M2 F x' B2 E L'"));
    for(int frame=0;frame<24;++frame) {
        complete.frame=static_cast<std::uint8_t>(frame);
        auto pattern=pattern_from_cube(complete,true);
        pattern.center_ori[1]=-1;pattern.center_ori[4]=-1;
        for(auto move:all_moves()) {
            const auto full_result=cube::apply(complete,move);
            require(static_cast<bool>(verify_sequence(complete,std::array<Move,1>{move},full_result)),"Center oracle failed physical replay");
            auto expected=pattern_from_cube(full_result,true);
            expected.center_ori[1]=-1;expected.center_ori[4]=-1;
            require(apply_pattern(pattern,move)==expected,"Center constraints were lost or moved to a different logical face");
        }
    }
}
PatternRequest bounded_request(std::string_view scramble, bool centers=false) {
    PatternRequest r;
    r.pattern=pattern_from_cube(cube::apply(CubeState{},parse_moves(scramble)),centers);
    r.allowed_moves=parse_moves("R R' U U' r r' x x'");
    r.max_turns=2;r.max_results=0;r.max_nodes=0;
    r.time_limit=std::chrono::seconds(60);
    r.heuristic=PatternHeuristic::None;
    return r;
}
std::set<std::string> solution_keys(const PatternReport& report) {
    require(report.exhausted(),"Cross-check search did not exhaust its bounded domain: "+std::string(pattern_stop_name(report.stop)));
    std::set<std::string> keys;
    for(const auto& solution:report.solutions)keys.insert(commuting_key(solution.moves));
    return keys;
}
void direction_and_goal_checks() {
    auto r=bounded_request("R U");r.allowed_moves=parse_moves("R R'");r.max_turns=1;
    r.direction=PatternDirection::Generate;
    auto report=enumerate_pattern(r);
    require(report.exhausted()&&report.solutions.size()==1&&format_moves(report.solutions[0].moves)=="R","Generate target R U must admit R in its right U orbit");
    r.direction=PatternDirection::Solve;
    require(enumerate_pattern(r).solutions.empty(),"Solve R U must not inherit inverse of the modulo-U generator");
    r=bounded_request("U R");r.allowed_moves=parse_moves("R R'");r.max_turns=1;
    report=enumerate_pattern(r);
    require(report.solutions.size()==1&&format_moves(report.solutions[0].moves)=="R'","Solve U R must admit R' leaving residual U");
    r.direction=PatternDirection::Generate;
    require(enumerate_pattern(r).solutions.empty(),"Generate U R must not inherit inverse of the modulo-U solver");
    for(const auto& orientation:frame_words())for(int power=1;power<=3;++power) {
        auto c=cube::apply(CubeState{},Move{'U',static_cast<std::uint8_t>(power)});
        c=cube::apply(c,orientation);
        r.pattern=pattern_from_cube(c,true);r.direction=PatternDirection::Solve;r.max_turns=0;r.allowed_moves=parse_moves("x y z");
        report=enumerate_pattern(r);
        require(report.solutions.size()==1&&report.solutions[0].turn_cost==0&&report.solutions[0].moves.empty(),"Oriented-center U orbit must be a true zero-action goal in every frame");
        c=cube::apply(CubeState{},Move{'R',1});c=cube::apply(c,orientation);
        r.pattern=pattern_from_cube(c,false);
        require(enumerate_pattern(r).solutions.empty(),"Free viewing rotation must not turn logical R into a zero-cost U goal");
    }
    // A specified center is a real goal constraint, even when no cubie is constrained.
    r=PatternRequest{};r.pattern.center_ori[1]=1;r.modulo_u=true;r.allowed_moves=parse_moves("R'");r.max_turns=1;r.max_results=0;r.heuristic=PatternHeuristic::None;
    report=enumerate_pattern(r);
    require(report.solutions.size()==1&&report.solutions[0].turn_cost==1,"Specified non-U center direction must require its real turn");
    auto corrupted=report.solutions[0];corrupted.moves.clear();corrupted.turn_cost=0;
    require(!verify_pattern_solution(r,corrupted),"Independent verifier accepted missing actions");
    corrupted=report.solutions[0];++corrupted.turn_cost;
    require(!verify_pattern_solution(r,corrupted),"Independent verifier accepted a false algorithm count");
}
void projected_bound_admissibility() {
    const std::array<std::string_view,4> scrambles={"r U", "U R", "R U", "x r U'"};
    for(auto scramble:scrambles)for(auto direction:{PatternDirection::Solve,PatternDirection::Generate}) {
        auto r=bounded_request(scramble);
        r.direction=direction;
        const auto unpruned=solution_keys(enumerate_pattern(r));
        r.heuristic=PatternHeuristic::Standard;
        const auto optimized=solution_keys(enumerate_pattern(r));
        require(unpruned==optimized,"Projected PDB removed or introduced bounded solution classes for "+std::string(scramble));
        // Exercise anonymous orientation markers and identity-only markers.
        r.pattern.cp[0]=-1;r.pattern.cp[3]=-1;r.pattern.co[1]=-1;
        r.pattern.ep[0]=-1;r.pattern.ep[3]=-1;r.pattern.eo[1]=-1;
        r.heuristic=PatternHeuristic::None;
        const auto unpruned_partial=solution_keys(enumerate_pattern(r));
        r.heuristic=PatternHeuristic::Standard;
        require(unpruned_partial==solution_keys(enumerate_pattern(r)),"PDB lost partial orientation/identity solution classes for "+std::string(scramble));
    }
}
}
int main() {
    try{
        independent_partial_transport();pattern_text_roundtrip();center_constraint_transport();
        direction_and_goal_checks();projected_bound_admissibility();
        std::cout<<"Independent pattern/model cross-validation passed: "<<checks<<" checks.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"Pattern/model cross-validation failed: "<<e.what()<<'\n';return 1;}
}
