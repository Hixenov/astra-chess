#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>

// Astra Chess v0.1 — compact 0x88 chess engine and UCI shell.
namespace {
constexpr int INF = 32000, MATE = 30000;
constexpr int KNIGHT_DIRS[] = {-33,-31,-18,-14,14,18,31,33};
constexpr int BISHOP_DIRS[] = {-17,-15,15,17};
constexpr int ROOK_DIRS[] = {-16,-1,1,16};
constexpr int KING_DIRS[] = {-17,-16,-15,-1,1,15,16,17};
const std::string START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

struct Move { int from=-1, to=-1; char promo=0; int flags=0; };
enum { EP=1, CASTLE=2, PAWN2=4 };
struct Undo { char captured; int castling, ep, halfmove, fullmove; };
struct Board {
    std::array<char,128> sq{};
    int stm=1; // white=1, black=-1
    int castling=15; // KQkq bits 1,2,4,8
    int ep=-1, halfmove=0, fullmove=1;
};
int color(char p) { if (!p || p=='.') return 0; return std::isupper((unsigned char)p)?1:-1; }
bool on(int s) { return s>=0 && s<128 && !(s&0x88); }
int fileOf(int s) { return s&7; }
int rankOf(int s) { return 8-(s>>4); }
std::string nameSq(int s) { return std::string(1, char('a'+fileOf(s)))+char('0'+rankOf(s)); }
int parseSq(const std::string& s) { if(s.size()!=2||s[0]<'a'||s[0]>'h'||s[1]<'1'||s[1]>'8') return -1; return (8-(s[1]-'0'))*16+(s[0]-'a'); }
char lower(char c) { return (char)std::tolower((unsigned char)c); }

bool loadFen(Board& b, const std::string& fen) {
    std::istringstream in(fen); std::string placement, turn, castle, eps; int half=0, full=1;
    if(!(in>>placement>>turn>>castle>>eps)) return false; in>>half>>full;
    Board n; n.sq.fill('.'); int r=0,f=0;
    for(char c:placement) {
        if(c=='/') { if(f!=8) return false; ++r; f=0; continue; }
        if(c>='1'&&c<='8') { f+=c-'0'; if(f>8) return false; }
        else if(std::string("pnbrqkPNBRQK").find(c)!=std::string::npos) { if(f>=8||r>=8) return false; n.sq[r*16+f++]=c; }
        else return false;
    }
    if(r!=7||f!=8||(turn!="w"&&turn!="b")) return false;
    n.stm=turn=="w"?1:-1; n.castling=0;
    if(castle!="-") for(char c:castle) { if(c=='K')n.castling|=1; else if(c=='Q')n.castling|=2; else if(c=='k')n.castling|=4; else if(c=='q')n.castling|=8; else return false; }
    n.ep=eps=="-"?-1:parseSq(eps); if(eps!="-"&&n.ep<0)return false;
    n.halfmove=half; n.fullmove=full; b=n; return true;
}
std::string toFen(const Board& b) {
    std::string out;
    for(int r=0;r<8;r++){ int empty=0; for(int f=0;f<8;f++){char p=b.sq[r*16+f]; if(p=='.')++empty; else {if(empty){out+=char('0'+empty);empty=0;}out+=p;}} if(empty)out+=char('0'+empty); if(r!=7)out+='/'; }
    out += b.stm==1?" w ":" b "; std::string c; if(b.castling&1)c+='K';if(b.castling&2)c+='Q';if(b.castling&4)c+='k';if(b.castling&8)c+='q';out+=c.empty()?"-":c;out+=' ';out+=b.ep<0?"-":nameSq(b.ep);out+=' ';out+=std::to_string(b.halfmove)+" "+std::to_string(b.fullmove);return out;
}
int kingSq(const Board& b,int side){char k=side==1?'K':'k';for(int s=0;s<128;s++)if(on(s)&&b.sq[s]==k)return s;return -1;}
bool attacked(const Board& b,int target,int by) {
    // Pawns attack diagonally forward: white moves toward decreasing mailbox ranks.
    int pawn=by==1?'P':'p'; int from1=target+16*by-1, from2=target+16*by+1;
    if(on(from1)&&b.sq[from1]==pawn)return true; if(on(from2)&&b.sq[from2]==pawn)return true;
    for(int d:KNIGHT_DIRS){int s=target+d;if(on(s)&&color(b.sq[s])==by&&lower(b.sq[s])=='n')return true;}
    for(int d:BISHOP_DIRS){for(int s=target+d;on(s);s+=d){char p=b.sq[s];if(p!='.'){if(color(p)==by&&(lower(p)=='b'||lower(p)=='q'))return true;break;}}}
    for(int d:ROOK_DIRS){for(int s=target+d;on(s);s+=d){char p=b.sq[s];if(p!='.'){if(color(p)==by&&(lower(p)=='r'||lower(p)=='q'))return true;break;}}}
    for(int d:KING_DIRS){int s=target+d;if(on(s)&&color(b.sq[s])==by&&lower(b.sq[s])=='k')return true;}
    return false;
}
bool inCheck(const Board& b,int side){int k=kingSq(b,side);return k<0||attacked(b,k,-side);}
std::vector<Move> pseudo(const Board& b) {
    std::vector<Move> out;
    for(int from=0;from<128;from++){if(!on(from)||color(b.sq[from])!=b.stm)continue;char p=lower(b.sq[from]);
        auto add=[&](int to,int flags=0){if(color(b.sq[to])==-b.stm&&lower(b.sq[to])=='k')return; if(p=='p'&&(rankOf(to)==1||rankOf(to)==8)){for(char q:std::string("qrbn"))out.push_back({from,to,q,flags});}else out.push_back({from,to,0,flags});};
        if(p=='p') {int to=from-16*b.stm;if(on(to)&&b.sq[to]=='.'){add(to);int two=to-16*b.stm;if(rankOf(from)==(b.stm==1?2:7)&&on(two)&&b.sq[two]=='.')add(two,PAWN2);}for(int df:{-1,1}){int t=to+df;if(!on(t))continue;if(color(b.sq[t])==-b.stm)add(t);else if(t==b.ep)add(t,EP);}}
        else {const int* ds=nullptr;int count=0;switch(p){case 'n':ds=KNIGHT_DIRS;count=8;break;case 'b':ds=BISHOP_DIRS;count=4;break;case 'r':ds=ROOK_DIRS;count=4;break;case 'q':ds=KING_DIRS;count=8;break;case 'k':ds=KING_DIRS;count=8;break;}
            for(int i=0;i<count;i++){int d=ds[i];for(int to=from+d;on(to);to+=d){if(color(b.sq[to])==b.stm)break;add(to);if(b.sq[to]!='.'||p=='n'||p=='k')break;}}
            if(p=='k'&&!inCheck(b,b.stm)){int home=b.stm==1?116:4;char rook=b.stm==1?'R':'r';int ks=b.stm==1?1:4,qs=b.stm==1?2:8;
                if(from==home&&(b.castling&ks)&&b.sq[home+3]==rook&&b.sq[home+1]=='.'&&b.sq[home+2]=='.'&&!attacked(b,home+1,-b.stm)&&!attacked(b,home+2,-b.stm))add(home+2,CASTLE);
                if(from==home&&(b.castling&qs)&&b.sq[home-4]==rook&&b.sq[home-1]=='.'&&b.sq[home-2]=='.'&&b.sq[home-3]=='.'&&!attacked(b,home-1,-b.stm)&&!attacked(b,home-2,-b.stm))add(home-2,CASTLE);
            }
        }
    }return out;
}
Undo makeMove(Board& b,const Move& m){Undo u{b.sq[m.to],b.castling,b.ep,b.halfmove,b.fullmove};char p=b.sq[m.from];b.sq[m.from]='.';b.sq[m.to]=m.promo?(b.stm==1?(char)std::toupper(m.promo):m.promo):p;
    if(m.flags&EP){b.sq[m.to+16*b.stm]='.';u.captured=b.stm==1?'p':'P';}
    if(m.flags&CASTLE){int rf=m.to>m.from?m.from+3:m.from-4,rt=m.to>m.from?m.from+1:m.from-1;b.sq[rt]=b.sq[rf];b.sq[rf]='.';}
    if(p=='K')b.castling&=~3;if(p=='k')b.castling&=~12;
    if(m.from==119||m.to==119)b.castling&=~1;if(m.from==112||m.to==112)b.castling&=~2;if(m.from==7||m.to==7)b.castling&=~4;if(m.from==0||m.to==0)b.castling&=~8;
    b.ep=(m.flags&PAWN2)?(m.from+m.to)/2:-1;b.halfmove=(lower(p)=='p'||u.captured!='.')?0:b.halfmove+1;if(b.stm==-1)++b.fullmove;b.stm=-b.stm;return u;
}
void unmakeMove(Board& b,const Move& m,const Undo& u){b.stm=-b.stm;if(b.stm==-1)--b.fullmove;b.sq[m.from]=b.sq[m.to];if(m.promo)b.sq[m.from]=b.stm==1?'P':'p';b.sq[m.to]=u.captured;if(m.flags&EP)b.sq[m.to+16*b.stm]=b.stm==1?'p':'P';if(m.flags&CASTLE){int rf=m.to>m.from?m.from+3:m.from-4,rt=m.to>m.from?m.from+1:m.from-1;b.sq[rf]=b.sq[rt];b.sq[rt]='.';}b.castling=u.castling;b.ep=u.ep;b.halfmove=u.halfmove;b.fullmove=u.fullmove;}
std::vector<Move> legalMoves(Board& b){std::vector<Move> out;for(const Move&m:pseudo(b)){Undo u=makeMove(b,m);bool ok=!inCheck(b,-b.stm);unmakeMove(b,m,u);if(ok)out.push_back(m);}return out;}
std::string uci(const Move&m){std::string s=nameSq(m.from)+nameSq(m.to);if(m.promo)s+=m.promo;return s;}
bool parseMove(Board& b,const std::string& s,Move& result){for(const Move&m:legalMoves(b))if(uci(m)==s){result=m;return true;}return false;}
int pieceValue(char p){switch(lower(p)){case 'p':return 100;case 'n':return 320;case 'b':return 330;case 'r':return 500;case 'q':return 900;default:return 0;}}
int evaluate(const Board& b){int score=0;for(int s=0;s<128;s++)if(on(s)&&b.sq[s]!='.'){char p=b.sq[s];int v=pieceValue(p);int advance=6-std::abs(3-fileOf(s));if(lower(p)=='p')v+= (color(p)==1?7-rankOf(s):rankOf(s))*5;if(lower(p)=='n'||lower(p)=='b')v+=advance*3;score+=color(p)*v;}return score*b.stm;}
long long nodes=0;
int search(Board& b,int depth,int alpha,int beta,int ply){++nodes;if(depth<=0)return evaluate(b);auto moves=legalMoves(b);if(moves.empty())return inCheck(b,b.stm)?-MATE+ply:0;
    // Capture/promotion ordering is enough for the first baseline.
    std::stable_sort(moves.begin(),moves.end(),[&](const Move&a,const Move&c){int va=pieceValue(b.sq[a.to])+(a.promo?pieceValue(a.promo):0),vc=pieceValue(b.sq[c.to])+(c.promo?pieceValue(c.promo):0);return va>vc;});
    int best=-INF;for(const Move&m:moves){Undo u=makeMove(b,m);int score=-search(b,depth-1,-beta,-alpha,ply+1);unmakeMove(b,m,u);best=std::max(best,score);alpha=std::max(alpha,score);if(alpha>=beta)break;}return best;
}
Move bestMove(Board& b,int depth){auto moves=legalMoves(b);Move best=moves.empty()?Move{}:moves[0];int bestScore=-INF;nodes=0;for(const Move&m:moves){Undo u=makeMove(b,m);int score=-search(b,depth-1,-INF,INF,1);unmakeMove(b,m,u);if(score>bestScore){bestScore=score;best=m;}}return best;}
long long perft(Board& b,int depth){if(depth==0)return 1;long long n=0;for(const Move&m:legalMoves(b)){Undo u=makeMove(b,m);n+=perft(b,depth-1);unmakeMove(b,m,u);}return n;}
}

int main(int argc,char**argv){
    if(argc>1&&std::string(argv[1])=="--perft"){int d=argc>2?std::stoi(argv[2]):3;Board b;if(!loadFen(b,START_FEN))return 2;std::cout<<"perft("<<d<<") = "<<perft(b,d)<<"\n";return 0;}
    Board board;loadFen(board,START_FEN);std::string line;
    while(std::getline(std::cin,line)){std::istringstream in(line);std::string cmd;in>>cmd;
        if(cmd=="uci"){std::cout<<"id name Astra Chess 0.1\nid author Astra Chess Project\nuciok\n"<<std::flush;}
        else if(cmd=="isready"){std::cout<<"readyok\n"<<std::flush;}
        else if(cmd=="ucinewgame"){loadFen(board,START_FEN);}
        else if(cmd=="position"){std::string kind;in>>kind;Board next=board;bool ok=true;if(kind=="startpos")loadFen(next,START_FEN);else if(kind=="fen"){std::string fen,token;for(int i=0;i<6&&in>>token;i++){if(i)fen+=' ';fen+=token;}ok=loadFen(next,fen);}else ok=false;
            std::string token;while(ok&&in>>token){if(token=="moves")continue;Move m;if(!parseMove(next,token,m)){ok=false;break;}makeMove(next,m);}if(ok)board=next;
        }
        else if(cmd=="go"){int depth=4;std::string token;while(in>>token)if(token=="depth")in>>depth;depth=std::clamp(depth,1,8);Move m=bestMove(board,depth);if(m.from<0)std::cout<<"bestmove 0000\n";else std::cout<<"info depth "<<depth<<" nodes "<<nodes<<" score cp "<<evaluate(board)<<"\nbestmove "<<uci(m)<<"\n";std::cout<<std::flush;}
        else if(cmd=="quit")break;
        else if(cmd=="stop"){}
        else if(cmd=="d"){std::cout<<toFen(board)<<"\n"<<std::flush;}
    }
    return 0;
}
