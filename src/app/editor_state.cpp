#include "editor_state.hpp"

#include <algorithm>
#include <cctype>
#include <numeric>
#include <random>
#include <stdexcept>

namespace cube::editor {
namespace {
bool color(char c) { return face_names.find(c) != std::string_view::npos; }
bool ignored(char c) { return color(static_cast<char>(std::toupper(static_cast<unsigned char>(c)))) && !color(c); }
char base(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
bool free(char c) { return c == '?' || c == '.'; }
void check_buffer(const std::string& raw) {
    if(raw.size()!=54) throw std::invalid_argument("Editor state needs 54 facelets.");
    for(char c:raw) if(!color(c)&&!ignored(c)&&c!='@'&&!free(c)) throw std::invalid_argument("Unknown editor facelet marker.");
}

// Original editor indexes colors by the currently displayed centers. The new
// model keeps physical colors fixed, so normalize only while applying its rules.
struct LocalColors {
    std::array<char,6> physical{};
    explicit LocalColors(const std::string& raw) {
        for(int f=0;f<6;++f) {
            physical[f]=raw[f*9+4];
            if(!color(physical[f])) throw std::invalid_argument("Centers must have six ordinary colors.");
        }
        auto sorted=physical;std::sort(sorted.begin(),sorted.end());
        if(std::adjacent_find(sorted.begin(),sorted.end())!=sorted.end()) throw std::invalid_argument("Center colors must be distinct.");
    }
    std::string normalize(std::string raw) const {
        for(char& c:raw) {
            if(free(c)){c='?';continue;}
            if(c=='@')continue;
            const bool lower=ignored(c);
            const int f=static_cast<int>(std::find(physical.begin(),physical.end(),base(c))-physical.begin());
            c=face_names[f];if(lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return raw;
    }
    std::string restore(std::string raw) const {
        for(char& c:raw) if(c!='?'&&c!='@') {
            const bool lower=ignored(c);c=physical[face_names.find(base(c))];
            if(lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return raw;
    }
};

template<std::size_t Width,std::size_t Count>
void check_piece(std::string& raw,int clicked,bool submission,
                 const std::array<std::array<int,Width>,Count>& blocks) {
    int selected=-1;
    for(int i=0;i<static_cast<int>(Count);++i)
        if(std::find(blocks[i].begin(),blocks[i].end(),clicked)!=blocks[i].end()) {selected=i;break;}
    if(selected<0)return;
    const auto& block=blocks[selected];
    if(ignored(raw[clicked])) {
        for(int f:block)if(color(raw[f]))raw[f]=static_cast<char>(std::tolower(static_cast<unsigned char>(raw[f])));
    } else if(raw[clicked]=='@') {
        for(int f:block)if(f!=clicked)raw[f]='?';
    } else if(color(raw[clicked])) {
        for(int f:block)if(ignored(raw[f]))raw[f]=base(raw[f]);else if(raw[f]=='@')raw[f]='?';
    }
    const int unknown=static_cast<int>(std::count_if(block.begin(),block.end(),[&](int f){return raw[f]=='?';}));
    const bool complete_corner=Width==3&&unknown==1;
    if(unknown!=0&&!complete_corner)return;
    int identity=-1,orientation=-1;
    for(int candidate=0;candidate<static_cast<int>(Count)&&identity<0;++candidate) {
        for(int shift=0;shift<static_cast<int>(Width);++shift) {
            int matches=0;
            for(int n=0;n<static_cast<int>(Width);++n)
                if(base(raw[block[n]])==face_names[blocks[candidate][(n+shift)%Width]/9])++matches;
            if(matches==static_cast<int>(Width)-unknown){identity=candidate;orientation=shift;break;}
        }
    }
    if(identity<0) {
        for(int f:block)if(submission?(f==clicked):(f!=clicked))raw[f]='?';
        return;
    }
    // Two agreeing colors identify a physical cubie. Match the source's first
    // duplicate in position order; do not globally clear all duplicates.
    int duplicate=-1;
    for(int other=0;other<static_cast<int>(Count)&&duplicate<0;++other) {
        if(other==selected)continue;
        for(int shift=0;shift<static_cast<int>(Width);++shift) {
            int matches=0;
            for(int n=0;n<static_cast<int>(Width);++n)
                if(raw[block[n]]!='?'&&raw[block[n]]!='@'&&base(raw[block[n]])==base(raw[blocks[other][(n+shift)%Width]]))++matches;
            if(matches>=2){duplicate=other;break;}
        }
    }
    if(duplicate>=0) {
        if(submission)raw[clicked]='?';
        else for(int f:blocks[duplicate])raw[f]='?';
    }
    if(complete_corner&&!submission) {
        for(int n=0;n<static_cast<int>(Width);++n)if(raw[block[n]]=='?') {
            char c=face_names[blocks[identity][(n+orientation)%Width]/9];
            if(ignored(raw[block[(n+1)%Width]]))c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            raw[block[n]]=c;
        }
    }
}

template<std::size_t Width,std::size_t Count>
bool decode(const std::string& raw,const std::array<std::array<int,Width>,Count>& blocks,
            std::array<int,Count>& ids,std::array<int,Count>& orientations) {
    ids.fill(-1);orientations.fill(-1);
    for(int position=0;position<static_cast<int>(Count);++position) {
        const auto& block=blocks[position];
        int unknown=0,colored=0,marker=-1;
        for(int n=0;n<static_cast<int>(Width);++n) {
            if(raw[block[n]]=='?')++unknown;
            else if(raw[block[n]]=='@')marker=n;
            else ++colored;
        }
        if(unknown>0&&colored>0)return false; // Source defers global fixes for half-painted pieces.
        if(marker>=0){orientations[position]=marker;continue;}
        if(unknown==static_cast<int>(Width))continue;
        for(int id=0;id<static_cast<int>(Count)&&ids[position]<0;++id) {
            for(int orientation=0;orientation<static_cast<int>(Width);++orientation) {
                bool match=true;
                for(int n=0;n<static_cast<int>(Width);++n)
                    if(base(raw[block[(n+orientation)%Width]])!=face_names[blocks[id][n]/9])match=false;
                if(match){ids[position]=id;orientations[position]=ignored(raw[block[0]])?-1:orientation;break;}
            }
        }
        if(ids[position]<0)return false; // Defined behavior for invalid buffers; original constructor had no safe fallback.
    }
    // Original constructor infers the last missing identity for parity only;
    // it does not paint its stickers or infer an ignored orientation.
    if(std::count(ids.begin(),ids.end(),-1)==1) {
        const auto unknown=std::find(ids.begin(),ids.end(),-1);
        for(int id=0;id<static_cast<int>(Count);++id)
            if(std::find(ids.begin(),ids.end(),id)==ids.end()){*unknown=id;break;}
    }
    return true;
}
template<class Array> int parity(const Array& ids) {
    int inversions=0;
    for(std::size_t i=0;i<ids.size();++i)for(std::size_t j=0;j<i;++j)inversions+=ids[j]>ids[i];
    return inversions%2;
}
void global_fixes(std::string& raw,CheckResult& result) {
    std::array<int,8> cp,co;std::array<int,12> ep,eo;
    if(!decode(raw,corner_facelets,cp,co)||!decode(raw,edge_facelets,ep,eo))return;
    if(std::find(co.begin(),co.end(),-1)==co.end()) {
        const int turns=std::accumulate(co.begin(),co.end(),0)%3;
        if(turns) {
            const auto block=corner_facelets[0];
            for(int t=0;t<turns;++t){const char c=raw[block[0]];raw[block[0]]=raw[block[1]];raw[block[1]]=raw[block[2]];raw[block[2]]=c;}
            result.corrections.push_back(Correction::CornerTwist);
        }
    }
    if(std::find(eo.begin(),eo.end(),-1)==eo.end()&&std::accumulate(eo.begin(),eo.end(),0)%2) {
        std::swap(raw[edge_facelets[1][0]],raw[edge_facelets[1][1]]);
        result.corrections.push_back(Correction::EdgeFlip);
    }
    if(std::count(cp.begin(),cp.end(),-1)>=2||std::count(ep.begin(),ep.end(),-1)>=2)return;
    if(parity(cp)!=parity(ep)) {
        for(int n=0;n<2;++n)std::swap(raw[edge_facelets[1][n]],raw[edge_facelets[5][n]]);
        result.corrections.push_back(Correction::PermutationParity);
    }
}
CubeState center_frame(const std::string& raw) {
    FaceletState physical;
    for(int f=0;f<6;++f)for(int n=0;n<9;++n)physical.colors[f*9+n]=static_cast<std::int8_t>(face_names.find(raw[f*9+4]));
    return from_facelets(physical);
}
} // namespace

CheckResult check_facelet(std::string& raw,int facelet,bool submission) {
    check_buffer(raw);
    if(facelet<0||facelet>=54)throw std::out_of_range("Editor facelet index.");
    if(facelet%9==4)return {};
    const LocalColors palette(raw);
    auto local=palette.normalize(raw);
    const auto before=local;
    check_piece(local,facelet,submission,corner_facelets);
    check_piece(local,facelet,submission,edge_facelets);
    CheckResult result;
    if(!submission||facelet==53)global_fixes(local,result);
    result.changed=local!=before;result.accepted=!result.changed;
    raw=palette.restore(std::move(local));
    return result;
}
CheckResult check_all(std::string& raw) {
    CheckResult total;
    for(int f=0;f<54;++f)if(f%9!=4) {
        const auto part=check_facelet(raw,f,true);
        total.changed|=part.changed;total.accepted&=part.accepted;
        total.corrections.insert(total.corrections.end(),part.corrections.begin(),part.corrections.end());
    }
    return total;
}

void reset(std::string& raw,std::array<std::int8_t,6>& centers,Reset kind,Pieces pieces,std::uint64_t seed) {
    check_buffer(raw);const LocalColors palette(raw);
    if(kind==Reset::Random) {
        std::mt19937_64 rng(seed);CubeState state=center_frame(raw);
        if(pieces!=Pieces::Edges) {
            std::shuffle(state.cubies.cp.begin(),state.cubies.cp.end(),rng);
            int sum=0;for(int n=0;n<7;++n){state.cubies.co[n]=static_cast<std::uint8_t>(rng()%3);sum+=state.cubies.co[n];}
            state.cubies.co[7]=static_cast<std::uint8_t>((3-sum%3)%3);
        }
        if(pieces!=Pieces::Corners) {
            std::shuffle(state.cubies.ep.begin(),state.cubies.ep.end(),rng);
            int sum=0;for(int n=0;n<11;++n){state.cubies.eo[n]=static_cast<std::uint8_t>(rng()%2);sum+=state.cubies.eo[n];}
            state.cubies.eo[11]=static_cast<std::uint8_t>(sum%2);
        }
        int center_parity=0;
        if(pieces==Pieces::All)for(auto& value:state.center_ori){value=static_cast<std::uint8_t>(rng()%4);center_parity+=value;}
        if(parity(state.cubies.cp)!=center_parity%2)std::swap(state.cubies.cp[0],state.cubies.cp[1]);
        if(parity(state.cubies.ep)!=parity(state.cubies.cp))std::swap(state.cubies.ep[0],state.cubies.ep[1]);
        raw=format_facelets(to_facelets(state));
        for(int f=0;f<6;++f)if(centers[f]>=0)centers[f]=static_cast<std::int8_t>(state.center_ori[f]);
        return;
    }
    auto local=palette.normalize(raw);
    for(int f=0;f<54;++f) {
        const int slot=f%9;const bool corner=(slot==0||slot==2||slot==6||slot==8);
        const bool edge=(slot==1||slot==3||slot==5||slot==7);
        if(pieces==Pieces::All||(pieces==Pieces::Corners&&corner)||(pieces==Pieces::Edges&&edge))
            if(slot!=4)local[f]=kind==Reset::Empty?'?':face_names[f/9];
    }
    raw=palette.restore(std::move(local));
    for(auto& c:centers)if(c>=0)c=0;
}

void apply_moves_raw(std::string& raw,std::array<std::int8_t,6>& centers,std::span<const Move> moves) {
    check_buffer(raw);LocalColors palette(raw);
    auto state=center_frame(raw);
    for(int f=0;f<6;++f)state.center_ori[f]=static_cast<std::uint8_t>(std::max<int>(0,centers[f]));
    state=cube::apply(state,moves);
    FaceletState indices;for(int f=0;f<54;++f)indices.colors[f]=static_cast<std::int8_t>(f);
    indices=apply_facelets(indices,moves);
    auto result=raw;for(int f=0;f<54;++f)result[f]=raw[indices.colors[f]];
    raw=std::move(result);
    for(int f=0;f<6;++f)if(centers[f]>=0)centers[f]=static_cast<std::int8_t>(state.center_ori[f]);
}

std::vector<Move> parse_legacy_moves(std::string_view text) {
    std::string normal;
    normal.reserve(text.size());
    for(std::size_t i=0;i<text.size();++i) {
        const auto c=static_cast<unsigned char>(text[i]);
        if(c==0xc2&&i+1<text.size()&&static_cast<unsigned char>(text[i+1])==0xb2){normal+='2';++i;}
        else if(c==0xc2&&i+1<text.size()&&static_cast<unsigned char>(text[i+1])==0xa0){normal+=' ';++i;}
        else if(c==0xb2)normal+='2'; // Legacy ANSI squared exponent.
        else if(c==0xca&&i+1<text.size()&&static_cast<unsigned char>(text[i+1])==0xbc){normal+='\'';++i;}
        else if(c==0xe2&&i+2<text.size()&&static_cast<unsigned char>(text[i+1])==0x80&&
                (static_cast<unsigned char>(text[i+2])==0x98||static_cast<unsigned char>(text[i+2])==0x99||
                 static_cast<unsigned char>(text[i+2])==0xb2)){normal+='\'';i+=2;}
        else if(c==0xe2&&i+2<text.size()&&
                ((static_cast<unsigned char>(text[i+1])==0x88&&static_cast<unsigned char>(text[i+2])==0x92)||
                 (static_cast<unsigned char>(text[i+1])==0x80&&static_cast<unsigned char>(text[i+2])==0x93))){
            normal+='\'';i+=2;if(i+1<text.size()&&text[i+1]=='1')++i;
        }
        else if(c==0xe3&&i+2<text.size()&&static_cast<unsigned char>(text[i+1])==0x80&&
                static_cast<unsigned char>(text[i+2])==0x80){normal+=' ';i+=2;}
        else if(c==0xef&&i+2<text.size()&&static_cast<unsigned char>(text[i+1])==0xbc){
            const auto b2=static_cast<unsigned char>(text[i+2]);
            if(b2==0x87){normal+='\'';i+=2;}
            else if(b2==0x88){normal+='(';i+=2;}
            else if(b2==0x89){normal+=')';i+=2;}
            else if(b2==0xbb){normal+='[';i+=2;}
            else if(b2==0xbd){normal+=']';i+=2;}
            else if(b2>=0x90&&b2<=0x99){normal+=static_cast<char>('0'+(b2-0x90));i+=2;}
            else if(b2>=0xa1&&b2<=0xba){normal+=static_cast<char>('A'+(b2-0xa1));i+=2;}
            else normal+=static_cast<char>(c);
        }
        else if(c==0xef&&i+2<text.size()&&static_cast<unsigned char>(text[i+1])==0xbd&&
                static_cast<unsigned char>(text[i+2])>=0x81&&static_cast<unsigned char>(text[i+2])<=0x9a){
            normal+=static_cast<char>('a'+(static_cast<unsigned char>(text[i+2])-0x81));i+=2;
        }
        else if(c=='`')normal+='\'';
        else if(c=='-'){normal+='\'';if(i+1<text.size()&&text[i+1]=='1')++i;}
        else normal+=static_cast<char>(c);
    }
    auto read_power=[&](std::size_t& pos){
        int power=1;
        if(pos<normal.size()&&normal[pos]=='2'){power=2;++pos;if(pos<normal.size()&&normal[pos]=='\'')++pos;}
        else if(pos<normal.size()&&normal[pos]=='\''){++pos;if(pos<normal.size()&&normal[pos]=='2'){power=2;++pos;}else power=3;}
        else if(pos<normal.size()&&normal[pos]=='3'){power=3;++pos;}
        return power;
    };
    std::string expanded;
    for(std::size_t i=0;i<normal.size();) {
        const char face=normal[i];const auto index=face_names.find(face);
        if(index==std::string_view::npos){expanded+=face;++i;continue;}
        std::size_t cursor=i+1;char mode=0;int power=1;
        if(cursor<normal.size()&&(normal[cursor]=='s'||normal[cursor]=='a')){mode=normal[cursor++];power=read_power(cursor);}
        else {power=read_power(cursor);if(cursor<normal.size()&&(normal[cursor]=='s'||normal[cursor]=='a'))mode=normal[cursor++];}
        if(!mode){expanded+=face;++i;continue;}
        const char opposite=face_names[(index+3)%6];
        expanded+=' ';expanded+=format_move({face,static_cast<std::uint8_t>(power)});expanded+=' ';
        expanded+=format_move({opposite,static_cast<std::uint8_t>(mode=='s'?4-power:power)});expanded+=' ';
        i=cursor;
    }
    return parse_moves(expanded);
}

std::optional<std::string> parse_legacy_facelets(std::string_view text) {
    std::string stickers;std::array<int,6> counts{};
    for(char c:text) {
        if(std::isspace(static_cast<unsigned char>(c)))continue;
        c=base(c);
        if(c=='N'||c=='?'||c=='.')stickers+='?';
        else if(color(c)){stickers+=c;++counts[face_names.find(c)];}
        else return std::nullopt;
    }
    if(stickers.size()!=48||std::any_of(counts.begin(),counts.end(),[](int n){return n>8;}))return std::nullopt;
    auto raw=format_facelets(solved_facelets());
    for(std::size_t i=0;i<48;++i)raw[legacy_facelet_order[i]]=stickers[i];
    return raw;
}
} // namespace cube::editor
