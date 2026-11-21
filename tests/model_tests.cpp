#include "cube/model.hpp"
#include "reference_facelet_moves.hpp"

#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

using namespace cube;

namespace {
int checks=0;
void require(bool condition,const std::string& message) {
    ++checks;
    if(!condition)throw std::runtime_error(message);
}
template<class Action>
void require_rejected(Action action,const std::string& message) {
    bool rejected=false;try{action();}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,message);
}
void original_reference() {
    FaceletState labelled;
    for(int i=0;i<54;++i)labelled.colors[i]=static_cast<std::int8_t>(i);
    constexpr std::string_view names="URFDLBxyzEMS";
    for(int m=0;m<12;++m) {
        const auto actual=apply_facelets(labelled,{names[m],1});
        for(int i=0;i<54;++i)
            require(actual.colors[original_facelet_moves[m][i]]==i,
                "Geometry differs from original Pascal facelet move "+std::string(1,names[m])+" at sticker "+std::to_string(i));
    }
}
std::vector<CubeState> all_frames() {
    std::vector<CubeState> result(1);
    std::array<bool,24> seen{};seen[0]=true;
    for(std::size_t i=0;i<result.size();++i)for(char c:std::string_view("xyz")) {
        const auto next=cube::apply(result[i],{c,1});
        if(!seen[next.frame]){seen[next.frame]=true;result.push_back(next);}
    }
    require(result.size()==24,"Whole rotations must generate exactly 24 orientations");
    return result;
}
void atomic_moves() {
    const auto start=cube::apply(CubeState{},parse_moves("R U F2 D' L B U2 R'"));
    for(const auto& orientation:all_frames()) {
        auto oriented=start;oriented.frame=orientation.frame;
        for(int f=0;f<6;++f)
            require(logical_face(oriented.frame,physical_face(oriented.frame,static_cast<Face>(f)))==static_cast<Face>(f),"Frame face maps must invert");
        for(auto move:all_moves()) {
            const auto result=cube::apply(oriented,move);
            require(static_cast<bool>(validate(result)),"Move generated invalid cubie state: "+format_move(move));
            require(static_cast<bool>(verify_sequence(oriented,std::array<Move,1>{move},result)),"Cubie/geometry disagreement in frame "+std::to_string(oriented.frame)+" for "+format_move(move));
            require(cube::apply(result,inverse(move))==oriented,"Inverse failed for "+format_move(move));
            auto four=oriented;
            for(int i=0;i<4;++i)four=cube::apply(four,move);
            require(four==oriented,"Four identical actions must be identity");
            auto repeated=oriented;
            for(int i=0;i<move.power;++i)repeated=cube::apply(repeated,{move.symbol,1});
            require(repeated==result,"Action power differs from repeated quarter turn");
            require(from_facelets(to_facelets(result))==result,"Facelet conversion failed to round trip");
        }
    }
}
void wide_identities() {
    constexpr std::array<std::pair<std::string_view,std::string_view>,9> identities={{{"r","R M'"},{"l","L M"},{"u","U E'"},{"d","D E"},{"f","F S"},{"b","B S'"},{"x","R L' M'"},{"y","U D' E'"},{"z","F B' S"}}};
    for(const auto& frame:all_frames()) {
        auto start=cube::apply(CubeState{},parse_moves("F R2 U' B L'"));start.frame=frame.frame;
        for(const auto& [left,right]:identities) {
            require(cube::apply(start,parse_moves(left))==cube::apply(start,parse_moves(right)),"Cubie identity failed: "+std::string(left)+" = "+std::string(right));
            const auto facelets=to_facelets(start);
            require(apply_facelets(facelets,parse_moves(left))==apply_facelets(facelets,parse_moves(right)),"Geometry identity failed: "+std::string(left));
        }
    }
}
void notation_and_costs() {
    require(format_moves(parse_moves("U2 r x'"),true)=="[U2] r x'","Initial U must receive brackets without changes");
    require(format_moves(parse_moves("Rw2 (R U)2 [U']"))=="r2 R U R U U'","Parser failed wide aliases, groups or brackets");
    require(format_moves(parse_moves("(R U)'"))=="U' R'","Grouped inverse failed");
    require(parse_moves("// no moves\n").empty(),"Comments must not be actions");
    const auto moves=parse_moves("Rw2 x y' [U] (R U R' U')2 // comment");
    require(turn_count(moves)==10,"Turn cost must count half/wide turns once");
    require(rotation_count(moves)==2,"Rotations must be counted separately");
    require(turn_count(moves)+rotation_count(moves)==static_cast<int>(moves.size()),"Every action must be accounted for");
    require(cube::apply(cube::apply(CubeState{},moves),inverse(moves))==CubeState{},"Formula inverse must undo rotations as well as face turns");
    for(std::string_view bad:{"Q","R0","R22","(R U","R]","((R)0x)999999999"})
        require_rejected([&]{(void)parse_moves(bad);},"Invalid notation was silently accepted: "+std::string(bad));
}
void input_validation() {
    require(static_cast<bool>(validate(CubeState{})),"Solved cube invalid");
    auto bad=CubieState{};bad.eo[0]=1;
    require(!validate(bad),"One flipped edge was accepted");
    bad=CubieState{};bad.co[0]=1;
    require(!validate(bad),"One twisted corner was accepted");
    bad=CubieState{};std::swap(bad.ep[0],bad.ep[1]);
    require(!validate(bad),"Single edge transposition was accepted");
    bad=CubieState{};bad.cp[0]=1;
    require(!validate(bad),"Duplicate cubie was accepted");
    require_rejected([]{(void)parse_facelets("URFDLB");},"Short facelet input accepted");
    auto facelets=solved_facelets();facelets.colors[0]=1;
    require_rejected([&]{(void)from_facelets(facelets);},"Wrong color counts accepted");
    facelets=solved_facelets();std::swap(facelets.colors[4],facelets.colors[13]);
    require_rejected([&]{(void)from_facelets(facelets);},"Reflected center arrangement accepted");
    require(format_facelets(parse_facelets(format_facelets(solved_facelets())))==format_facelets(solved_facelets()),"Facelet text roundtrip failed");
}
void logical_u_goals() {
    for(const auto& frame:all_frames())for(int p=0;p<4;++p) {
        auto state=p?cube::apply(CubeState{},Move{'U',static_cast<std::uint8_t>(p)}):CubeState{};
        state.frame=frame.frame;
        require(solved_mod_u(state,true)==p,"U orbit must be invariant under viewing frame");
        if(p)require(!is_solved(state),"Strict solved test must not adopt mod U");
        else require(is_solved(state,true),"Solved state must remain solved under viewing frame");
    }
    for(const auto& frame:all_frames()) {
        auto state=cube::apply(CubeState{},Move{'R',1});state.frame=frame.frame;
        require(solved_mod_u(state)<0,"Free rotation incorrectly made logical R into zero-cost U");
    }
}
void random_sequences() {
    std::mt19937 random(0x43425545U);
    auto cube=CubeState{};auto stickers=solved_facelets();
    for(int i=0;i<20000;++i) {
        const auto move=move_from_index(static_cast<int>(random()%54));
        cube=cube::apply(cube,move);stickers=apply_facelets(stickers,move);
        require(to_facelets(cube)==stickers,"Random sequence physical mismatch at step "+std::to_string(i));
        if(i%71==0) {
            require(from_facelets(stickers)==cube,"Random facelet decoding mismatch");
            require(compose(cube.cubies,inverse(cube.cubies))==CubieState{},"Cubie inverse failed");
        }
    }
}
}

int main() {
    try {
        original_reference();atomic_moves();wide_identities();notation_and_costs();input_validation();logical_u_goals();random_sequences();
        std::cout<<"Model validation passed: "<<checks<<" checks; original Pascal facelets, 24 frames x 54 moves, wide identities, independent 20000-action replay.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"Model test failure: "<<error.what()<<'\n';return 1;}
}
