#include "editor_state.hpp"
#include "cube/pattern.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::string solved(){return cube::format_facelets(cube::solved_facelets());}
std::string empty(){auto result=solved();for(int i=0;i<54;++i)if(i%9!=4)result[i]='?';return result;}
}
int main(){
    using namespace cube;using namespace cube::editor;
    try{
        auto raw=solved();
        require(check_all(raw).accepted&&raw==solved(),"Solved submission was changed");
        raw[8]='u';auto check=check_facelet(raw,8);
        require(!check.accepted&&check.changed&&raw[9]=='r'&&raw[20]=='f',"Shift marker did not propagate only to the painted corner");
        require(raw[6]=='U',"Shift marker leaked to another corner");
        raw[8]='@';check=check_facelet(raw,8);
        require(!check.accepted&&raw[9]=='?'&&raw[20]=='?',"Ctrl orientation marker did not free siblings");
        raw[9]='R';check=check_facelet(raw,9);
        require(raw[8]=='?'&&raw[9]=='R'&&raw[20]=='?',"Ordinary paint did not clear old orientation marker");

        raw=empty();raw[8]='U';raw[9]='R';check=check_facelet(raw,9);
        require(raw[20]=='F'&&!check.accepted,"AutoFix did not complete the unique third corner sticker");
        raw=empty();raw[8]='u';raw[9]='r';check=check_facelet(raw,9);
        require(raw[20]=='f',"AutoFix lost ignore-orientation while completing a corner");
        raw=empty();raw[8]='U';raw[9]='D';check=check_facelet(raw,9);
        require(raw[8]=='?'&&raw[9]=='D'&&raw[20]=='?',"Manual AutoFix must preserve the new impossible paint");
        raw=empty();raw[8]='U';raw[9]='D';check=check_facelet(raw,9,true);
        require(raw[8]=='U'&&raw[9]=='?',"Submission AutoFix must remove the offending current paint");
        raw=empty();raw[5]='U';check=check_facelet(raw,5);
        require(raw[10]=='?'&&check.accepted,"AutoFix must not guess a second edge sticker");

        raw=empty();raw[8]='U';raw[9]='R';raw[20]='F';
        raw[6]='U';raw[18]='R';raw[38]='F';check=check_facelet(raw,38);
        require(raw[8]=='?'&&raw[9]=='?'&&raw[20]=='?'&&raw[6]=='U',"Manual duplicate fix must clear the older corner");
        raw=empty();raw[5]='U';raw[10]='R';raw[7]='U';raw[19]='R';check=check_facelet(raw,19);
        require(raw[5]=='?'&&raw[10]=='?'&&raw[7]=='U'&&raw[19]=='R',"Manual duplicate fix must clear the older edge");

        raw=solved();raw[8]='F';raw[9]='U';raw[20]='R';check=check_all(raw);
        require(!check.accepted&&raw==solved(),"Submission did not correct total corner twist at URF");
        require(std::find(check.corrections.begin(),check.corrections.end(),Correction::CornerTwist)!=check.corrections.end(),"Missing corner correction report");
        raw=solved();std::swap(raw[7],raw[19]);check=check_all(raw);
        require(!check.accepted&&raw==solved(),"Submission did not correct total edge flip at UF");
        raw=solved();std::swap(raw[7],raw[28]);std::swap(raw[19],raw[25]);check=check_all(raw);
        require(!check.accepted&&raw==solved(),"Submission did not correct UF/DF parity");
        raw=empty();raw[8]='U';check=check_all(raw);
        require(check.accepted&&raw[8]=='U'&&raw[9]=='?',"Submission should leave a half-painted corner for validation, not invent colors");

        std::array<std::int8_t,6> centers{-1,-1,-1,-1,-1,-1};
        for(auto selection:{Pieces::All,Pieces::Corners,Pieces::Edges}){
            raw=solved();reset(raw,centers,Reset::Empty,selection,0);
            for(int i=0;i<54;++i){const int s=i%9;bool affected=s!=4&&(selection==Pieces::All||(selection==Pieces::Corners&&(s==0||s==2||s==6||s==8))||(selection==Pieces::Edges&&(s==1||s==3||s==5||s==7)));
                require(raw[i]==(affected?'?':face_names[i/9]),"Empty C/E changed wrong sticker");}
            reset(raw,centers,Reset::Clean,selection,0);require(raw==solved(),"Clean C/E failed");
            for(std::uint64_t seed=0;seed<40;++seed){
                raw=empty();reset(raw,centers,Reset::Random,selection,seed);
                const auto state=from_facelets(parse_facelets(raw));require(static_cast<bool>(validate(state)),"Random coordinates produced impossible cube");
                if(selection==Pieces::Corners)require(state.cubies.ep==CubieState{}.ep&&state.cubies.eo==CubieState{}.eo,"Random C must reset edges");
                if(selection==Pieces::Edges)require(state.cubies.cp==CubieState{}.cp&&state.cubies.co==CubieState{}.co,"Random E must reset corners");
            }
        }
        centers.fill(0);raw=solved();reset(raw,centers,Reset::Random,Pieces::All,47);
        int sum=0;for(auto c:centers)sum+=c;
        const auto state=from_facelets(parse_facelets(raw));int swaps=0;for(int i=0;i<8;++i)for(int j=0;j<i;++j)swaps+=state.cubies.cp[j]>state.cubies.cp[i];
        require(sum%2==swaps%2,"Random center and permutation parity disagree");

        raw=solved();raw[0]='?';raw[1]='@';raw[9]='r';raw[20]='?'; // Intentionally incomplete/invalid edit buffer.
        centers.fill(-1);const auto before=raw;
        for(auto move:all_moves()){
            auto trial=raw;auto arrows=centers;apply_moves_raw(trial,arrows,std::span(&move,1));const auto undo=inverse(move);apply_moves_raw(trial,arrows,std::span(&undo,1));
            require(trial==before&&arrows==centers,"Raw move/inverse failed on partial edit buffer");
        }
        raw=solved();auto rotations=parse_moves("x y");apply_moves_raw(raw,centers,rotations);require(check_all(raw).accepted,"Rotated solved editor failed normalization");
        const auto rotated=raw;reset(raw,centers,Reset::Empty);reset(raw,centers,Reset::Clean);require(raw==rotated,"Reset should preserve current center palette/frame");
        raw[corner_facelets[0][0]]=static_cast<char>(std::tolower(raw[corner_facelets[0][0]]));check=check_facelet(raw,corner_facelets[0][0]);
        for(int i:corner_facelets[0])require(std::islower(static_cast<unsigned char>(raw[i])),"Shift propagation lost rotated colors");
        for(const auto& [legacy,modern]:std::vector<std::pair<std::string,std::string>>{
            {"Rs","R L'"},{"Ra","R L"},{"Ds'","D' U"},{"F2s","F2 B2"},{"B'a","B' F'"},
            {"(Rs U-1 Ra2)3","(R L' U' R2 L2)3"},{"r U- R-1 r3 U²","r U' R' r' U2"},
            {"(M E S)2'","(M E S)2'"},{"Rw U Rw'","r U r'"},{"(R U)2","R U R U"}}){
            const auto a=parse_legacy_moves(legacy),b=parse_moves(modern);
            require(a==b,"Legacy notation expansion did not match exact modern actions");
            require(cube::apply(CubeState{},a)==cube::apply(CubeState{},b),"Legacy notation changed physical action");
        }
        for(char face:face_names)for(char mode:std::string_view("sa"))for(int power=1;power<=3;++power){
            const auto token=std::string(1,face)+mode+(power==1?"":power==2?"2":"'");
            const auto parsed=parse_legacy_moves(token);const char opposite=face_names[(face_names.find(face)+3)%6];
            require(parsed==std::vector<Move>{{face,static_cast<std::uint8_t>(power)},{opposite,static_cast<std::uint8_t>(mode=='s'?4-power:power)}},"Legacy opposite-face family expansion failed");
        }
        for(const auto& stateText:{solved(),empty(),format_facelets(to_facelets(cube::apply(CubeState{},parse_moves("R U F2 D' L B"))))}){
            std::string line;
            for(std::size_t i=0;i<legacy_facelet_order.size();++i){const char c=stateText[legacy_facelet_order[i]];line+=(c=='?'?'N':c);if(i%3==2)line+=' ';}
            const auto parsed=parse_legacy_facelets(line);require(parsed&&*parsed==stateText,"Legacy 48 sticker line did not roundtrip");
        }
        require(!parse_legacy_facelets("R U R' U'"),"Formula mistaken for legacy facelets");
        require(!parse_legacy_facelets(std::string(48,'U')),"Impossible color-count text mistaken for legacy facelets");
        std::cout<<"PASS editor state: "<<checks<<" checks (original AutoFix, reset C/E, coordinate random, raw moves).\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL editor: "<<e.what()<<'\n';return 1;}
}
