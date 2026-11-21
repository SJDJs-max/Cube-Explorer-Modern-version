#include "cube/model.hpp"

#include <algorithm>
#include <cctype>
#include <numeric>
#include <stdexcept>

namespace cube {
namespace {

constexpr std::array<std::array<std::uint8_t, 3>, 8> corner_colors = {{{0,1,2},{0,2,4},{0,4,5},{0,5,1},{3,2,1},{3,4,2},{3,5,4},{3,1,5}}};
constexpr std::array<std::array<std::uint8_t, 2>, 12> edge_colors = {{{0,1},{0,2},{0,4},{0,5},{3,1},{3,2},{3,4},{3,5},{2,1},{2,4},{5,4},{5,1}}};

// Direct transcription of CubeDefs.pas CornerCubieMove / EdgeCubieMove.
constexpr std::array<CubieState, 6> quarter_turns = {{
    {{3,0,1,2,4,5,6,7}, {}, {3,0,1,2,4,5,6,7,8,9,10,11}, {}},
    {{4,1,2,0,7,5,6,3}, {2,0,0,1,1,0,0,2}, {8,1,2,3,11,5,6,7,4,9,10,0}, {}},
    {{1,5,2,3,0,4,6,7}, {1,2,0,0,2,1,0,0}, {0,9,2,3,4,8,6,7,1,5,10,11}, {0,1,0,0,0,1,0,0,1,1,0,0}},
    {{0,1,2,3,5,6,7,4}, {}, {0,1,2,3,5,6,7,4,8,9,10,11}, {}},
    {{0,2,6,3,4,1,5,7}, {0,1,2,0,0,2,1,0}, {0,1,10,3,4,5,9,7,8,2,6,11}, {}},
    {{0,1,3,7,4,5,2,6}, {0,0,1,2,0,0,2,1}, {0,1,2,11,4,5,6,10,8,9,3,7}, {0,0,0,1,0,0,0,1,0,0,1,1}}
}};

struct Vec {
    int x{}, y{}, z{};
    bool operator==(const Vec&) const = default;
};
struct Matrix {
    // Images of the positive X, Y and Z unit vectors.
    std::array<Vec, 3> columns{{{1,0,0},{0,1,0},{0,0,1}}};
    bool operator==(const Matrix&) const = default;
};
constexpr std::array<Vec, 6> normals{{{0,1,0},{1,0,0},{0,0,1},{0,-1,0},{-1,0,0},{0,0,-1}}};
constexpr std::array<Vec, 6> face_up{{{0,0,-1},{0,1,0},{0,1,0},{0,0,1},{0,1,0},{0,1,0}}};

Vec transform(const Matrix& m, Vec v) {
    return {m.columns[0].x*v.x + m.columns[1].x*v.y + m.columns[2].x*v.z,
            m.columns[0].y*v.x + m.columns[1].y*v.y + m.columns[2].y*v.z,
            m.columns[0].z*v.x + m.columns[1].z*v.y + m.columns[2].z*v.z};
}
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix result;
    for (int i=0; i<3; ++i) result.columns[i] = transform(a,b.columns[i]);
    return result;
}
Matrix transpose(const Matrix& a) {
    return {{{{a.columns[0].x,a.columns[1].x,a.columns[2].x},
              {a.columns[0].y,a.columns[1].y,a.columns[2].y},
              {a.columns[0].z,a.columns[1].z,a.columns[2].z}}}};
}
Vec rotate_positive(Vec v, int axis) {
    if (axis==0) return {v.x,-v.z,v.y};
    if (axis==1) return {v.z,v.y,-v.x};
    return {-v.y,v.x,v.z};
}
Vec rotate(Vec v, int axis, int quarter_steps) {
    quarter_steps = (quarter_steps%4+4)%4;
    for (int i=0; i<quarter_steps; ++i) v=rotate_positive(v,axis);
    return v;
}
Matrix rotation_matrix(int axis, int steps) {
    Matrix m;
    for (auto& col:m.columns) col=rotate(col,axis,steps);
    return m;
}
int face_of_normal(Vec n) {
    const auto it=std::find(normals.begin(),normals.end(),n);
    if (it==normals.end()) throw std::logic_error("Invalid face normal");
    return static_cast<int>(it-normals.begin());
}
struct FrameTables {
    std::array<Matrix,24> matrices{};
    FrameTables() {
        int count=1;
        for (int current=0; current<count; ++current) {
            for (int axis=0; axis<3; ++axis) {
                const auto next=multiply(rotation_matrix(axis,-1),matrices[current]);
                if (std::find(matrices.begin(),matrices.begin()+count,next)==matrices.begin()+count) {
                    if (count==24) throw std::logic_error("Too many cube orientations");
                    matrices[count++]=next;
                }
            }
        }
        if (count!=24) throw std::logic_error("Incomplete cube orientation group");
    }
    std::uint8_t index(const Matrix& m) const {
        const auto it=std::find(matrices.begin(),matrices.end(),m);
        if (it==matrices.end()) throw std::logic_error("Unknown cube orientation");
        return static_cast<std::uint8_t>(it-matrices.begin());
    }
};
const FrameTables& frames() {
    static const FrameTables tables;
    return tables;
}
void check_frame(std::uint8_t frame) {
    if (frame>=24) throw std::invalid_argument("Cube frame must be in 0..23");
}
void check_move(Move move) {
    if (move_names.find(move.symbol)==std::string_view::npos || move.power<1 || move.power>3)
        throw std::invalid_argument("Move must use URFDLBurfdlbMESxyz and power 1..3");
}

struct TransitionTables {
    std::array<std::array<CubieState,3>,6> outer{};
    std::array<std::array<MoveTransform,54>,24> transitions{};
    TransitionTables() {
        for (int face=0; face<6; ++face) {
            outer[face][0]=quarter_turns[face];
            outer[face][1]=compose(outer[face][0],outer[face][0]);
            outer[face][2]=compose(outer[face][1],outer[face][0]);
        }
        for (int frame=0; frame<24; ++frame) for (int index=0; index<54; ++index) {
            const Move m{move_names[index/3], static_cast<std::uint8_t>(index%3+1)};
            auto& out=transitions[frame][index];
            out.next_frame=static_cast<std::uint8_t>(frame);
            const auto add_outer=[&](Face physical, int power) {
                const auto inv=transpose(frames().matrices[frame]);
                const auto f=face_of_normal(transform(inv,normals[static_cast<int>(physical)]));
                power=(power%4+4)%4;
                if (power==0) return;
                out.delta=compose(out.delta,outer[f][power-1]);
                out.center_delta[f]=static_cast<std::uint8_t>((out.center_delta[f]+power)%4);
            };
            const auto rotate_frame=[&](int axis, int power) {
                out.next_frame=frames().index(multiply(rotation_matrix(axis,-power),frames().matrices[frame]));
            };
            const int p=m.power;
            const auto ordinary=face_names.find(m.symbol);
            if (ordinary!=std::string_view::npos) add_outer(static_cast<Face>(ordinary),p);
            else switch(m.symbol) {
                // Exact physical identities. The two factors commute along their common axis.
                case 'r': rotate_frame(0,p);  add_outer(Face::L,p); break; // r = x L
                case 'l': rotate_frame(0,-p); add_outer(Face::R,p); break; // l = x' R
                case 'u': rotate_frame(1,p);  add_outer(Face::D,p); break;
                case 'd': rotate_frame(1,-p); add_outer(Face::U,p); break;
                case 'f': rotate_frame(2,p);  add_outer(Face::B,p); break;
                case 'b': rotate_frame(2,-p); add_outer(Face::F,p); break;
                case 'M': rotate_frame(0,-p); add_outer(Face::R,p); add_outer(Face::L,-p); break;
                case 'E': rotate_frame(1,-p); add_outer(Face::U,p); add_outer(Face::D,-p); break;
                case 'S': rotate_frame(2,p);  add_outer(Face::F,-p); add_outer(Face::B,p); break;
                case 'x': rotate_frame(0,p); break;
                case 'y': rotate_frame(1,p); break;
                case 'z': rotate_frame(2,p); break;
                default: throw std::logic_error("Unimplemented move");
            }
        }
    }
};
const TransitionTables& transitions() {
    static const TransitionTables tables;
    return tables;
}

struct StickerGeometry { Vec position; Vec normal; };
StickerGeometry geometry(int index) {
    const int face=index/9, row=(index%9)/3, col=index%3;
    Vec p;
    switch(face) {
        case 0:p={col-1,1,row-1};break;
        case 1:p={1,1-row,1-col};break;
        case 2:p={col-1,1-row,1};break;
        case 3:p={col-1,-1,1-row};break;
        case 4:p={-1,1-row,col-1};break;
        default:p={1-col,1-row,-1};break;
    }
    return {p,normals[face]};
}
int sticker_index(Vec position, Vec normal) {
    const int face=face_of_normal(normal);
    int row{},col{};
    switch(face) {
        case 0:row=position.z+1;col=position.x+1;break;
        case 1:row=1-position.y;col=1-position.z;break;
        case 2:row=1-position.y;col=position.x+1;break;
        case 3:row=1-position.z;col=position.x+1;break;
        case 4:row=1-position.y;col=position.z+1;break;
        default:row=1-position.y;col=1-position.x;break;
    }
    return face*9+row*3+col;
}
int normal_axis(Vec n) { return n.x ? 0 : n.y ? 1 : 2; }
int normal_sign(Vec n) { return n.x+n.y+n.z; }
Vec orientation_tangent(int face, int orientation) {
    return rotate(face_up[face],normal_axis(normals[face]),-normal_sign(normals[face])*orientation);
}
std::uint8_t tangent_orientation(int face, Vec tangent) {
    for (int p=0;p<4;++p) if (orientation_tangent(face,p)==tangent) return static_cast<std::uint8_t>(p);
    throw std::logic_error("Invalid center tangent");
}
FaceletState rigid_transform(const FaceletState& state, const Matrix& matrix) {
    FaceletState out;
    for (int i=0;i<54;++i) {
        const auto g=geometry(i);
        out.colors[sticker_index(transform(matrix,g.position),transform(matrix,g.normal))]=state.colors[i];
    }
    for (int face=0;face<6;++face) {
        const int destination=face_of_normal(transform(matrix,normals[face]));
        out.center_ori[destination]=tangent_orientation(destination,transform(matrix,orientation_tangent(face,state.center_ori[face])));
    }
    return out;
}
struct GeometricMove { int axis, direction, layer; bool wide, all; };
GeometricMove describe(Move move) {
    const char c=static_cast<char>(std::toupper(static_cast<unsigned char>(move.symbol)));
    const bool wide=move.is_wide();
    if(move.symbol=='x') return {0,-1,0,false,true};
    if(move.symbol=='y') return {1,-1,0,false,true};
    if(move.symbol=='z') return {2,-1,0,false,true};
    switch(c) {
        case 'R':return {0,-1,1,wide,false};
        case 'L':return {0,1,-1,wide,false};
        case 'U':return {1,-1,1,wide,false};
        case 'D':return {1,1,-1,wide,false};
        case 'F':return {2,-1,1,wide,false};
        case 'B':return {2,1,-1,wide,false};
        case 'M':return {0,1,0,false,false};
        case 'E':return {1,1,0,false,false};
        case 'S':return {2,-1,0,false,false};
        default:throw std::invalid_argument("Unknown geometric move");
    }
}
bool affected(Vec p, const GeometricMove& m) {
    const int coordinate=m.axis==0?p.x:m.axis==1?p.y:p.z;
    return m.all || coordinate==m.layer || (m.wide && coordinate==0);
}
template<std::size_t N>
int permutation_parity(const std::array<std::uint8_t,N>& p) {
    int parity=0;
    for(std::size_t i=0;i<N;++i) for(std::size_t j=i+1;j<N;++j) parity^=(p[i]>p[j]);
    return parity;
}

std::string normalize_notation(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto b0 = static_cast<unsigned char>(input[i]);
        if (b0 == '`') {
            out += '\'';
        } else if (b0 == 0xC2 && i + 1 < input.size()) {
            const auto b1 = static_cast<unsigned char>(input[i + 1]);
            if (b1 == 0xB2) { out += '2'; ++i; continue; } // ²
            if (b1 == 0xA0) { out += ' '; ++i; continue; } // NBSP
            out += static_cast<char>(b0);
        } else if (b0 == 0xCA && i + 1 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0xBC) {
            out += '\''; ++i; // ʼ
        } else if (b0 == 0xE2 && i + 2 < input.size()) {
            const auto b1 = static_cast<unsigned char>(input[i + 1]);
            const auto b2 = static_cast<unsigned char>(input[i + 2]);
            if (b1 == 0x80 && (b2 == 0x98 || b2 == 0x99 || b2 == 0xB2)) {
                out += '\''; i += 2; continue; // ‘ ’ ′
            }
            out += static_cast<char>(b0);
        } else if (b0 == 0xE3 && i + 2 < input.size() &&
                   static_cast<unsigned char>(input[i + 1]) == 0x80 &&
                   static_cast<unsigned char>(input[i + 2]) == 0x80) {
            out += ' '; i += 2; // Fullwidth space
        } else if (b0 == 0xEF && i + 2 < input.size()) {
            const auto b1 = static_cast<unsigned char>(input[i + 1]);
            const auto b2 = static_cast<unsigned char>(input[i + 2]);
            if (b1 == 0xBC) {
                if (b2 == 0x87) { out += '\''; i += 2; continue; }
                if (b2 == 0x88) { out += '('; i += 2; continue; }
                if (b2 == 0x89) { out += ')'; i += 2; continue; }
                if (b2 == 0xBB) { out += '['; i += 2; continue; }
                if (b2 == 0xBD) { out += ']'; i += 2; continue; }
                if (b2 >= 0x90 && b2 <= 0x99) { out += static_cast<char>('0' + (b2 - 0x90)); i += 2; continue; }
                if (b2 >= 0xA1 && b2 <= 0xBA) { out += static_cast<char>('A' + (b2 - 0xA1)); i += 2; continue; }
            } else if (b1 == 0xBD && b2 >= 0x81 && b2 <= 0x9A) {
                out += static_cast<char>('a' + (b2 - 0x81)); i += 2; continue;
            }
            out += static_cast<char>(b0);
        } else {
            out += static_cast<char>(b0);
        }
    }
    return out;
}

class MoveParser {
    std::string owned;
    std::string_view text;
    std::size_t offset{};
    static constexpr std::size_t maximum_moves=100000;
    void skip_space() {
        while(offset<text.size()) {
            if(std::isspace(static_cast<unsigned char>(text[offset]))) {++offset;continue;}
            if(text[offset]=='/' && offset+1<text.size() && text[offset+1]=='/') {
                while(offset<text.size() && text[offset]!='\n')++offset;
                continue;
            }
            break;
        }
    }
    [[noreturn]] void fail(std::string_view what) const {
        throw std::invalid_argument(std::string(what)+" at character "+std::to_string(offset+1));
    }
    std::vector<Move> sequence(char closing,int nesting) {
        if(nesting>32) fail("Parentheses nested too deeply");
        std::vector<Move> result;
        while(true) {
            skip_space();
            if(offset==text.size()) {
                if(closing) fail("Missing closing bracket");
                return result;
            }
            const char c=text[offset];
            if(c==closing && closing) {++offset;return result;}
            if(c==')'||c==']') fail("Unexpected closing bracket");
            if(c=='('||c=='[') {
                ++offset;
                auto part=sequence(c=='('?')':']',nesting+1);
                bool invert_group=false;
                if(offset<text.size() && text[offset]=='\'') {invert_group=true;++offset;}
                unsigned repeats=1;
                if(c=='(' && offset<text.size() && std::isdigit(static_cast<unsigned char>(text[offset]))) {
                    repeats=0;
                    while(offset<text.size() && std::isdigit(static_cast<unsigned char>(text[offset]))) {
                        repeats=repeats*10+static_cast<unsigned>(text[offset++]-'0');
                        if(repeats>maximum_moves)fail("Repetition count is too large");
                    }
                }
                if(!invert_group && offset<text.size() && text[offset]=='\'') {invert_group=true;++offset;}
                if(invert_group) part=inverse(part);
                if(part.size()*repeats>maximum_moves-result.size())fail("Formula is too long");
                for(unsigned i=0;i<repeats;++i) result.insert(result.end(),part.begin(),part.end());
                continue;
            }
            if(move_names.find(c)==std::string_view::npos)fail("Unknown move");
            ++offset;
            Move move{c,1};
            // Rw / RW is accepted as an alias of r. Lowercase remains the canonical notation.
            if(face_names.find(c)!=std::string_view::npos && offset<text.size() && (text[offset]=='w'||text[offset]=='W')) {
                move.symbol=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));++offset;
            }
            if(offset<text.size() && text[offset]=='2') {
                move.power=2;++offset;
                if(offset<text.size() && text[offset]=='\'')++offset;
            } else if(offset<text.size() && text[offset]=='\'') {
                ++offset;
                if(offset<text.size() && text[offset]=='2') {
                    move.power=2;++offset;
                } else {
                    move.power=3;
                }
            } else if(offset<text.size() && text[offset]=='3') {
                move.power=3;++offset;
            }
            result.push_back(move);
            if(result.size()>maximum_moves)fail("Formula is too long");
        }
    }
public:
    explicit MoveParser(std::string_view input):owned(normalize_notation(input)),text(owned){}
    std::vector<Move> run(){return sequence(0,0);}
};

} // namespace

bool Move::is_rotation() const noexcept {return symbol=='x'||symbol=='y'||symbol=='z';}
bool Move::is_wide() const noexcept {return std::string_view("urfdlb").find(symbol)!=std::string_view::npos;}
bool Move::is_slice() const noexcept {return symbol=='M'||symbol=='E'||symbol=='S';}

CubieState compose(const CubieState& state,const CubieState& delta) noexcept {
    CubieState result;
    for(int i=0;i<8;++i) {
        result.cp[i]=state.cp[delta.cp[i]];
        result.co[i]=static_cast<std::uint8_t>((state.co[delta.cp[i]]+delta.co[i])%3);
    }
    for(int i=0;i<12;++i) {
        result.ep[i]=state.ep[delta.ep[i]];
        result.eo[i]=state.eo[delta.ep[i]]^delta.eo[i];
    }
    return result;
}
CubieState inverse(const CubieState& state) noexcept {
    CubieState result;
    for(int i=0;i<8;++i){result.cp[state.cp[i]]=static_cast<std::uint8_t>(i);result.co[state.cp[i]]=static_cast<std::uint8_t>((3-state.co[i])%3);}
    for(int i=0;i<12;++i){result.ep[state.ep[i]]=static_cast<std::uint8_t>(i);result.eo[state.ep[i]]=state.eo[i];}
    return result;
}
Move inverse(Move move) noexcept {move.power=static_cast<std::uint8_t>(4-move.power);return move;}
std::vector<Move> inverse(std::span<const Move> moves) {
    std::vector<Move> result;result.reserve(moves.size());
    for(auto it=moves.rbegin();it!=moves.rend();++it)result.push_back(inverse(*it));
    return result;
}
const CubieState& outer_move(Face face,int power) {
    if(static_cast<int>(face)>=6||power<1||power>3)throw std::invalid_argument("Invalid outer face turn");
    return transitions().outer[static_cast<int>(face)][power-1];
}
int move_index(Move move) {check_move(move);return static_cast<int>(move_names.find(move.symbol))*3+move.power-1;}
Move move_from_index(int index) {
    if(index<0||index>=54)throw std::invalid_argument("Move index must be in 0..53");
    return {move_names[index/3],static_cast<std::uint8_t>(index%3+1)};
}
const MoveTransform& move_transform(std::uint8_t frame,Move move) {check_frame(frame);return transitions().transitions[frame][move_index(move)];}
CubeState apply(const CubeState& state,Move move) {auto result=state;apply_in_place(result,move);return result;}
CubeState apply(const CubeState& state,std::span<const Move> moves) {auto result=state;for(auto m:moves)apply_in_place(result,m);return result;}
void apply_in_place(CubeState& state,Move move) {
    const auto& t=move_transform(state.frame,move);
    state.cubies=compose(state.cubies,t.delta);
    state.frame=t.next_frame;
    for(int f=0;f<6;++f)state.center_ori[f]=static_cast<std::uint8_t>((state.center_ori[f]+t.center_delta[f])%4);
}
bool is_solved(const CubieState& state) noexcept {return state==CubieState{};}
bool is_solved(const CubeState& state,bool oriented_centers) noexcept {
    return is_solved(state.cubies)&&(!oriented_centers||state.center_ori==std::array<std::uint8_t,6>{});
}
int solved_mod_u(const CubeState& state,bool oriented_centers) noexcept {
    CubieState target;
    for(int p=0;p<4;++p) {
        std::array<std::uint8_t,6> centers{};centers[0]=static_cast<std::uint8_t>(p);
        if(state.cubies==target&&(!oriented_centers||state.center_ori==centers))return p;
        target=compose(target,quarter_turns[0]);
    }
    return -1;
}
Validation validate(const CubieState& state) {
    std::array<bool,8> corners{};std::array<bool,12> edges{};
    int co=0,eo=0;
    for(int i=0;i<8;++i) {
        if(state.cp[i]>=8||corners[state.cp[i]])return {false,"Corner identities must form a permutation of 0..7"};
        corners[state.cp[i]]=true;
        if(state.co[i]>=3)return {false,"Corner orientations must be 0, 1 or 2"};
        co+=state.co[i];
    }
    for(int i=0;i<12;++i) {
        if(state.ep[i]>=12||edges[state.ep[i]])return {false,"Edge identities must form a permutation of 0..11"};
        edges[state.ep[i]]=true;
        if(state.eo[i]>=2)return {false,"Edge orientations must be 0 or 1"};
        eo+=state.eo[i];
    }
    if(co%3)return {false,"Corner twist sum must be divisible by 3"};
    if(eo%2)return {false,"Edge flip sum must be even"};
    if(permutation_parity(state.cp)!=permutation_parity(state.ep))return {false,"Corner and edge permutation parities differ"};
    return {};
}
Validation validate(const CubeState& state) {
    if(state.frame>=24)return {false,"Cube frame must be in 0..23"};
    for(auto value:state.center_ori)if(value>3)return {false,"Center orientations must be in 0..3"};
    return validate(state.cubies);
}
std::vector<Move> parse_moves(std::string_view text) {return MoveParser(text).run();}
std::string format_move(Move move) {
    check_move(move);
    std::string s(1,move.symbol);if(move.power==2)s+='2';else if(move.power==3)s+='\'';
    return s;
}
std::string format_moves(std::span<const Move> moves,bool bracket_initial_u) {
    std::string result;
    for(std::size_t i=0;i<moves.size();++i) {
        if(i)result+=' ';
        const bool bracket=i==0&&bracket_initial_u&&moves[i].symbol=='U';
        if(bracket)result+='[';
        result+=format_move(moves[i]);
        if(bracket)result+=']';
    }
    return result;
}
int turn_count(std::span<const Move> moves) noexcept {int count=0;for(auto m:moves)count+=m.turn_cost();return count;}
int rotation_count(std::span<const Move> moves) noexcept {int count=0;for(auto m:moves)count+=m.rotation_count();return count;}
int move_axis(Move move) {check_move(move);return describe(move).axis;}
std::vector<Move> all_moves(bool slices,bool wide,bool rotations) {
    std::vector<Move> result;
    for(char symbol:move_names) for(int p=1;p<=3;++p) {
        Move m{symbol,static_cast<std::uint8_t>(p)};
        if((m.is_slice()&&!slices)||(m.is_wide()&&!wide)||(m.is_rotation()&&!rotations))continue;
        result.push_back(m);
    }
    return result;
}
Face logical_face(std::uint8_t frame,Face physical) {
    check_frame(frame);return static_cast<Face>(face_of_normal(transform(transpose(frames().matrices[frame]),normals[static_cast<int>(physical)])));
}
Face physical_face(std::uint8_t frame,Face logical) {
    check_frame(frame);return static_cast<Face>(face_of_normal(transform(frames().matrices[frame],normals[static_cast<int>(logical)])));
}
std::uint8_t rotated_frame(std::uint8_t frame,Move rotation) {
    if(!rotation.is_rotation())throw std::invalid_argument("Expected x, y or z rotation");
    return move_transform(frame,rotation).next_frame;
}
FaceletState solved_facelets() {
    FaceletState result;for(int i=0;i<54;++i)result.colors[i]=static_cast<std::int8_t>(i/9);return result;
}
FaceletState to_facelets(const CubeState& state) {
    const auto valid=validate(state);if(!valid)throw std::invalid_argument(valid.message);
    auto result=solved_facelets();
    for(int pos=0;pos<8;++pos)for(int n=0;n<3;++n)
        result.colors[corner_facelets[pos][(n+state.cubies.co[pos])%3]]=static_cast<std::int8_t>(corner_colors[state.cubies.cp[pos]][n]);
    for(int pos=0;pos<12;++pos)for(int n=0;n<2;++n)
        result.colors[edge_facelets[pos][(n+state.cubies.eo[pos])%2]]=static_cast<std::int8_t>(edge_colors[state.cubies.ep[pos]][n]);
    result.center_ori=state.center_ori;
    return rigid_transform(result,frames().matrices[state.frame]);
}
CubeState from_facelets(const FaceletState& state) {
    std::array<int,6> counts{};
    for(auto value:state.colors){if(value<0||value>=6)throw std::invalid_argument("A complete cube needs 54 known facelets");++counts[value];}
    for(int n:counts)if(n!=9)throw std::invalid_argument("Each color must occur exactly nine times");
    for(auto p:state.center_ori)if(p>3)throw std::invalid_argument("Center orientation must be in 0..3");
    int frame=-1;
    for(int f=0;f<24;++f) {
        bool match=true;
        for(int logical=0;logical<6;++logical) {
            const int physical=face_of_normal(transform(frames().matrices[f],normals[logical]));
            match=match&&state.colors[physical*9+4]==logical;
        }
        if(match){frame=f;break;}
    }
    if(frame<0)throw std::invalid_argument("Center colors do not describe a proper cube orientation");
    const auto normalized=rigid_transform(state,transpose(frames().matrices[frame]));
    CubeState result;result.frame=static_cast<std::uint8_t>(frame);result.center_ori=normalized.center_ori;
    for(int pos=0;pos<8;++pos) {
        int ori=0;
        for(;ori<3;++ori){auto color=normalized.colors[corner_facelets[pos][ori]];if(color==0||color==3)break;}
        if(ori==3)throw std::invalid_argument("Corner has no U or D color");
        const auto c1=normalized.colors[corner_facelets[pos][(ori+1)%3]],c2=normalized.colors[corner_facelets[pos][(ori+2)%3]];
        int piece=0;for(;piece<8;++piece)if(corner_colors[piece][1]==c1&&corner_colors[piece][2]==c2&&corner_colors[piece][0]==normalized.colors[corner_facelets[pos][ori]])break;
        if(piece==8)throw std::invalid_argument("Invalid or reflected corner colors");
        result.cubies.cp[pos]=static_cast<std::uint8_t>(piece);result.cubies.co[pos]=static_cast<std::uint8_t>(ori);
    }
    for(int pos=0;pos<12;++pos) {
        bool found=false;
        for(int piece=0;piece<12&&!found;++piece)for(int ori=0;ori<2&&!found;++ori)
            if(normalized.colors[edge_facelets[pos][0]]==edge_colors[piece][ori]&&normalized.colors[edge_facelets[pos][1]]==edge_colors[piece][1-ori]) {
                result.cubies.ep[pos]=static_cast<std::uint8_t>(piece);result.cubies.eo[pos]=static_cast<std::uint8_t>(ori);found=true;
            }
        if(!found)throw std::invalid_argument("Invalid edge colors");
    }
    const auto valid=validate(result);if(!valid)throw std::invalid_argument(valid.message);
    return result;
}
FaceletState parse_facelets(std::string_view text) {
    FaceletState result;std::size_t index=0;
    for(char c:text) {
        if(std::isspace(static_cast<unsigned char>(c)))continue;
        if(index==54)throw std::invalid_argument("Expected exactly 54 facelets");
        const auto f=face_names.find(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        if(f!=std::string_view::npos)result.colors[index++]=static_cast<std::int8_t>(f);
        else if(c=='?'||c=='-'||c=='.')result.colors[index++]=-1;
        else throw std::invalid_argument("Facelets must be URFDLB or ? for unspecified");
    }
    if(index!=54)throw std::invalid_argument("Expected exactly 54 facelets");
    return result;
}
std::string format_facelets(const FaceletState& state) {
    std::string result;result.reserve(54);
    for(auto c:state.colors){if(c<-1||c>=6)throw std::invalid_argument("Invalid facelet color");result+=c<0?'?':face_names[c];}
    return result;
}
FaceletState apply_facelets(const FaceletState& state,Move move) {
    check_move(move);const auto m=describe(move);const int steps=m.direction*move.power;
    auto result=state;
    for(int i=0;i<54;++i) {
        auto g=geometry(i);if(!affected(g.position,m))continue;
        g.position=rotate(g.position,m.axis,steps);g.normal=rotate(g.normal,m.axis,steps);
        result.colors[sticker_index(g.position,g.normal)]=state.colors[i];
    }
    for(int face=0;face<6;++face)if(affected(normals[face],m)) {
        const auto dest=face_of_normal(rotate(normals[face],m.axis,steps));
        result.center_ori[dest]=tangent_orientation(dest,rotate(orientation_tangent(face,state.center_ori[face]),m.axis,steps));
    }
    return result;
}
FaceletState apply_facelets(const FaceletState& state,std::span<const Move> moves) {auto result=state;for(auto m:moves)result=apply_facelets(result,m);return result;}
Validation verify_sequence(const CubeState& start,std::span<const Move> moves,const CubeState& expected) {
    try {
        if(apply_facelets(to_facelets(start),moves)!=to_facelets(expected))return {false,"Independent facelet replay disagrees with solver state"};
        return {};
    }catch(const std::exception& e){return {false,e.what()};}
}

} // namespace cube
