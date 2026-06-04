// =====================================================================
//  CHAOS ENGINE  -  a deliberately chaotic UCI chess engine in one file
// =====================================================================
//  Build:   g++ -O2 -std=c++17 -o chaos chaos.cpp
//  Run:     ./chaos   (then type UCI commands, or load into any GUI / board)
//
//  Personality: it plays *legal* chess, but with a wild streak.
//  A "chaos" slider mixes pure-random moves with a tiny material search.
//  chaos=0   -> always picks the best (shallow) move it can find
//  chaos=100 -> total madness, plays almost any legal move
//  Default   -> 60, spicy but not suicidal.
// =====================================================================

#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

// ---------------------------------------------------------------------
//  Board model
//  Pieces: uppercase = White, lowercase = Black, '.' = empty.
//  Square index 0..63, a8=0 ... h1=63 (rank 8 at top, standard FEN order).
// ---------------------------------------------------------------------
struct Move {
    int from, to;
    char promo; // 'q','r','b','n' or 0
    Move(int f = 0, int t = 0, char p = 0) : from(f), to(t), promo(p) {}
};

struct Board {
    std::array<char, 64> sq;
    bool whiteToMove = true;
    // castling rights KQkq
    bool wK = true, wQ = true, bK = true, bQ = true;
    int  ep = -1; // en-passant target square, -1 if none

    Board() { setStartpos(); }

    void setStartpos() {
        const std::string s =
            "rnbqkbnr"
            "pppppppp"
            "........"
            "........"
            "........"
            "........"
            "PPPPPPPP"
            "RNBQKBNR";
        for (int i = 0; i < 64; ++i) sq[i] = s[i];
        whiteToMove = true;
        wK = wQ = bK = bQ = true;
        ep = -1;
    }

    void setFromFen(const std::string& fen) {
        std::istringstream is(fen);
        std::string board, stm, castle, epStr;
        is >> board >> stm >> castle >> epStr;
        int idx = 0;
        for (char c : board) {
            if (c == '/') continue;
            if (isdigit(c)) { for (int k = 0; k < c - '0'; ++k) sq[idx++] = '.'; }
            else sq[idx++] = c;
        }
        whiteToMove = (stm == "w");
        wK = castle.find('K') != std::string::npos;
        wQ = castle.find('Q') != std::string::npos;
        bK = castle.find('k') != std::string::npos;
        bQ = castle.find('q') != std::string::npos;
        ep = -1;
        if (epStr != "-" && epStr.size() == 2) {
            int file = epStr[0] - 'a';
            int rank = epStr[1] - '1';      // 0..7 from bottom
            ep = (7 - rank) * 8 + file;     // convert to a8=0 indexing
        }
    }
};

// ---------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------
static inline bool isWhite(char p) { return p != '.' && isupper((unsigned char)p); }
static inline bool isBlack(char p) { return p != '.' && islower((unsigned char)p); }
static inline int  rankOf(int s)   { return s / 8; }
static inline int  fileOf(int s)   { return s % 8; }
static inline bool onBoard(int r, int f) { return r >= 0 && r < 8 && f >= 0 && f < 8; }

static std::string sqName(int s) {
    std::string r;
    r += char('a' + fileOf(s));
    r += char('1' + (7 - rankOf(s)));
    return r;
}

static std::string moveStr(const Move& m) {
    std::string r = sqName(m.from) + sqName(m.to);
    if (m.promo) r += m.promo;
    return r;
}

// ---------------------------------------------------------------------
//  Move generation (pseudo-legal, then filtered for king-in-check)
// ---------------------------------------------------------------------
static void slide(const Board& b, int s, const int dirs[][2], int n,
                  bool white, std::vector<Move>& out) {
    int r = rankOf(s), f = fileOf(s);
    for (int d = 0; d < n; ++d) {
        int rr = r + dirs[d][0], ff = f + dirs[d][1];
        while (onBoard(rr, ff)) {
            int t = rr * 8 + ff;
            char tp = b.sq[t];
            if (tp == '.') out.emplace_back(s, t);
            else { if (white ? isBlack(tp) : isWhite(tp)) out.emplace_back(s, t); break; }
            rr += dirs[d][0]; ff += dirs[d][1];
        }
    }
}

static void genPseudo(const Board& b, std::vector<Move>& out) {
    bool white = b.whiteToMove;
    const int bishopDirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    const int rookDirs[4][2]   = {{1,0},{-1,0},{0,1},{0,-1}};
    const int knightD[8][2]    = {{2,1},{2,-1},{-2,1},{-2,-1},{1,2},{1,-2},{-1,2},{-1,-2}};
    const int kingD[8][2]      = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};

    for (int s = 0; s < 64; ++s) {
        char p = b.sq[s];
        if (p == '.') continue;
        if (white && !isWhite(p)) continue;
        if (!white && !isBlack(p)) continue;
        char up = toupper((unsigned char)p);
        int r = rankOf(s), f = fileOf(s);

        if (up == 'P') {
            int dir = white ? -1 : 1;          // white moves up (toward rank 8 = lower idx)
            int startRank = white ? 6 : 1;
            int promoRank = white ? 0 : 7;
            int oneR = r + dir;
            if (onBoard(oneR, f) && b.sq[oneR*8+f] == '.') {
                int t = oneR*8+f;
                if (oneR == promoRank) for (char pr : {'q','r','b','n'}) out.emplace_back(s,t,pr);
                else {
                    out.emplace_back(s,t);
                    if (r == startRank && b.sq[(r+2*dir)*8+f] == '.')
                        out.emplace_back(s,(r+2*dir)*8+f);
                }
            }
            for (int df : {-1, 1}) {
                int cr = r + dir, cf = f + df;
                if (!onBoard(cr, cf)) continue;
                int t = cr*8+cf;
                char tp = b.sq[t];
                bool cap = white ? isBlack(tp) : isWhite(tp);
                if (cap || t == b.ep) {
                    if (cr == promoRank) for (char pr : {'q','r','b','n'}) out.emplace_back(s,t,pr);
                    else out.emplace_back(s,t);
                }
            }
        } else if (up == 'N') {
            for (auto& d : knightD) {
                int rr = r+d[0], ff = f+d[1];
                if (!onBoard(rr,ff)) continue;
                char tp = b.sq[rr*8+ff];
                if (tp=='.' || (white?isBlack(tp):isWhite(tp))) out.emplace_back(s, rr*8+ff);
            }
        } else if (up == 'B') {
            slide(b, s, bishopDirs, 4, white, out);
        } else if (up == 'R') {
            slide(b, s, rookDirs, 4, white, out);
        } else if (up == 'Q') {
            slide(b, s, bishopDirs, 4, white, out);
            slide(b, s, rookDirs,   4, white, out);
        } else if (up == 'K') {
            for (auto& d : kingD) {
                int rr = r+d[0], ff = f+d[1];
                if (!onBoard(rr,ff)) continue;
                char tp = b.sq[rr*8+ff];
                if (tp=='.' || (white?isBlack(tp):isWhite(tp))) out.emplace_back(s, rr*8+ff);
            }
            // Castling (squares only; legality re-checked after)
            if (white && r==7 && f==4) {
                if (b.wK && b.sq[63]=='R' && b.sq[61]=='.' && b.sq[62]=='.') out.emplace_back(60,62);
                if (b.wQ && b.sq[56]=='R' && b.sq[57]=='.' && b.sq[58]=='.' && b.sq[59]=='.') out.emplace_back(60,58);
            }
            if (!white && r==0 && f==4) {
                if (b.bK && b.sq[7]=='r' && b.sq[5]=='.' && b.sq[6]=='.') out.emplace_back(4,6);
                if (b.bQ && b.sq[0]=='r' && b.sq[1]=='.' && b.sq[2]=='.' && b.sq[3]=='.') out.emplace_back(4,2);
            }
        }
    }
}

// Is square `s` attacked by the given side?
static bool attacked(const Board& b, int s, bool byWhite) {
    int r = rankOf(s), f = fileOf(s);
    // pawns
    int pd = byWhite ? 1 : -1; // white pawns attack from rank below (higher idx)
    for (int df : {-1,1}) {
        int rr = r+pd, ff = f+df;
        if (onBoard(rr,ff)) { char p=b.sq[rr*8+ff]; if (p==(byWhite?'P':'p')) return true; }
    }
    const int kn[8][2]={{2,1},{2,-1},{-2,1},{-2,-1},{1,2},{1,-2},{-1,2},{-1,-2}};
    for (auto&d:kn){int rr=r+d[0],ff=f+d[1];if(onBoard(rr,ff)){char p=b.sq[rr*8+ff];if(p==(byWhite?'N':'n'))return true;}}
    const int kd[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for (auto&d:kd){int rr=r+d[0],ff=f+d[1];if(onBoard(rr,ff)){char p=b.sq[rr*8+ff];if(p==(byWhite?'K':'k'))return true;}}
    const int bd[4][2]={{1,1},{1,-1},{-1,1},{-1,-1}};
    for (auto&d:bd){int rr=r+d[0],ff=f+d[1];while(onBoard(rr,ff)){char p=b.sq[rr*8+ff];if(p!='.'){if(p==(byWhite?'B':'b')||p==(byWhite?'Q':'q'))return true;break;}rr+=d[0];ff+=d[1];}}
    const int rd[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for (auto&d:rd){int rr=r+d[0],ff=f+d[1];while(onBoard(rr,ff)){char p=b.sq[rr*8+ff];if(p!='.'){if(p==(byWhite?'R':'r')||p==(byWhite?'Q':'q'))return true;break;}rr+=d[0];ff+=d[1];}}
    return false;
}

static int kingSquare(const Board& b, bool white) {
    char k = white ? 'K' : 'k';
    for (int i = 0; i < 64; ++i) if (b.sq[i] == k) return i;
    return -1;
}

// Apply a move (assumes pseudo-legal). Returns new board.
static Board doMove(const Board& b, const Move& m) {
    Board n = b;
    char p = n.sq[m.from];
    char up = toupper((unsigned char)p);
    bool white = isWhite(p);

    n.ep = -1;
    // en-passant capture
    if (up == 'P' && m.to == b.ep && b.sq[m.to] == '.') {
        int capSq = white ? m.to + 8 : m.to - 8;
        n.sq[capSq] = '.';
    }
    // double push sets ep
    if (up == 'P' && abs(m.to - m.from) == 16)
        n.ep = (m.from + m.to) / 2;

    // move piece
    n.sq[m.to] = p;
    n.sq[m.from] = '.';

    // promotion
    if (m.promo) n.sq[m.to] = white ? toupper(m.promo) : m.promo;

    // castling rook move
    if (up == 'K' && abs(m.to - m.from) == 2) {
        if (m.to == 62) { n.sq[63]='.'; n.sq[61]='R'; }
        if (m.to == 58) { n.sq[56]='.'; n.sq[59]='R'; }
        if (m.to == 6)  { n.sq[7]='.';  n.sq[5]='r'; }
        if (m.to == 2)  { n.sq[0]='.';  n.sq[3]='r'; }
    }
    // update castling rights
    if (up == 'K') { if (white){n.wK=n.wQ=false;} else {n.bK=n.bQ=false;} }
    if (m.from==63||m.to==63) n.wK=false;
    if (m.from==56||m.to==56) n.wQ=false;
    if (m.from==7 ||m.to==7 ) n.bK=false;
    if (m.from==0 ||m.to==0 ) n.bQ=false;

    n.whiteToMove = !b.whiteToMove;
    return n;
}

// Legal moves: filter pseudo-legal for king safety + castling-through-check.
static std::vector<Move> legalMoves(const Board& b) {
    std::vector<Move> pseudo, legal;
    genPseudo(b, pseudo);
    bool white = b.whiteToMove;
    for (auto& m : pseudo) {
        char p = b.sq[m.from];
        // castling: king must not be in check, nor pass through attacked squares
        if (toupper((unsigned char)p)=='K' && abs(m.to-m.from)==2) {
            int mid = (m.from+m.to)/2;
            if (attacked(b, m.from, !white)) continue;
            if (attacked(b, mid,   !white)) continue;
        }
        Board n = doMove(b, m);
        int ks = kingSquare(n, white);
        if (ks != -1 && !attacked(n, ks, !white)) legal.push_back(m);
    }
    return legal;
}

// ---------------------------------------------------------------------
//  Evaluation (tiny material count, white-positive)
// ---------------------------------------------------------------------
static int pieceVal(char p) {
    switch (toupper((unsigned char)p)) {
        case 'P': return 100;
        case 'N': return 320;
        case 'B': return 330;
        case 'R': return 500;
        case 'Q': return 900;
        case 'K': return 20000;
    }
    return 0;
}
static int evaluate(const Board& b) {
    int s = 0;
    for (char p : b.sq) {
        if (p=='.') continue;
        s += isWhite(p) ? pieceVal(p) : -pieceVal(p);
    }
    return s;
}

// Shallow negamax (used only to keep chaos from total self-destruction)
static int negamax(const Board& b, int depth, int alpha, int beta) {
    if (depth == 0) {
        int e = evaluate(b);
        return b.whiteToMove ? e : -e;
    }
    auto moves = legalMoves(b);
    if (moves.empty()) {
        int ks = kingSquare(b, b.whiteToMove);
        if (ks!=-1 && attacked(b, ks, !b.whiteToMove)) return -30000 + (3-depth); // mated
        return 0; // stalemate
    }
    int best = -1000000;
    for (auto& m : moves) {
        Board n = doMove(b, m);
        int v = -negamax(n, depth-1, -beta, -alpha);
        if (v > best) best = v;
        if (best > alpha) alpha = best;
        if (alpha >= beta) break;
    }
    return best;
}

// ---------------------------------------------------------------------
//  CHAOS move selection
// ---------------------------------------------------------------------
static std::mt19937 rng((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
static int chaosLevel = 60; // 0..100

static Move pickMove(const Board& b) {
    auto moves = legalMoves(b);
    if (moves.empty()) return Move(0,0);

    std::uniform_int_distribution<int> roll(0, 99);

    // With probability = chaosLevel%, just yeet a random legal move.
    if (roll(rng) < chaosLevel) {
        std::uniform_int_distribution<int> pick(0, (int)moves.size()-1);
        return moves[pick(rng)];
    }

    // Otherwise: shallow search, but add a little noise so it's never boring.
    int bestVal = -1000000;
    std::vector<Move> bestSet;
    std::uniform_int_distribution<int> noise(-40, 40); // centipawn jitter
    for (auto& m : moves) {
        Board n = doMove(b, m);
        int v = -negamax(n, 2, -1000000, 1000000) + noise(rng);
        if (v > bestVal) { bestVal = v; bestSet.clear(); bestSet.push_back(m); }
        else if (v == bestVal) bestSet.push_back(m);
    }
    std::uniform_int_distribution<int> pick(0, (int)bestSet.size()-1);
    return bestSet[pick(rng)];
}

// ---------------------------------------------------------------------
//  UCI loop
// ---------------------------------------------------------------------
static void applyMovesFromTokens(Board& b, std::istringstream& is) {
    std::string tok;
    while (is >> tok) {
        if (tok.size() < 4) continue;
        int ff = tok[0]-'a', fr = tok[1]-'1';
        int tf = tok[2]-'a', tr = tok[3]-'1';
        int from = (7-fr)*8 + ff;
        int to   = (7-tr)*8 + tf;
        char promo = (tok.size()>=5) ? tok[4] : 0;
        // find matching legal move (handles ep/castle/promo correctly)
        auto moves = legalMoves(b);
        for (auto& m : moves) {
            if (m.from==from && m.to==to &&
                (promo==0 || tolower((unsigned char)m.promo)==promo)) {
                b = doMove(b, m);
                break;
            }
        }
    }
}

int main() {
    std::ios::sync_with_stdio(false);
    Board board;
    std::string line;

    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string cmd; is >> cmd;

        if (cmd == "uci") {
            std::cout << "id name ChaosEngine 1.0\n";
            std::cout << "id author R3MU5x\n";
            std::cout << "option name Chaos type spin default 60 min 0 max 100\n";
            std::cout << "uciok\n" << std::flush;
        } else if (cmd == "isready") {
            std::cout << "readyok\n" << std::flush;
        } else if (cmd == "setoption") {
            // setoption name Chaos value 80
            std::string w; std::string name; int val=chaosLevel;
            while (is >> w) {
                if (w == "name") is >> name;
                else if (w == "value") is >> val;
            }
            if (name == "Chaos") chaosLevel = std::max(0, std::min(100, val));
        } else if (cmd == "ucinewgame") {
            board.setStartpos();
        } else if (cmd == "position") {
            std::string sub; is >> sub;
            if (sub == "startpos") {
                board.setStartpos();
                std::string maybe; if (is >> maybe && maybe == "moves")
                    applyMovesFromTokens(board, is);
            } else if (sub == "fen") {
                std::string fen, p;
                for (int i = 0; i < 6 && is >> p; ++i) { fen += p; fen += ' '; }
                board.setFromFen(fen);
                std::string maybe; if (is >> maybe && maybe == "moves")
                    applyMovesFromTokens(board, is);
            }
        } else if (cmd == "go") {
            Move m = pickMove(board);
            std::cout << "bestmove " << moveStr(m) << "\n" << std::flush;
        } else if (cmd == "quit") {
            break;
        }
    }
    return 0;
}
