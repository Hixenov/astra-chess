#include "eval.h"

#include <algorithm>
#include <cstdlib>

namespace astra {
namespace eval {

const int PieceValue[PIECE_TYPE_NB] = {100, 320, 330, 500, 900, 0};

namespace {

constexpr int MG_VAL[6] = {82, 337, 365, 477, 1025, 0};
constexpr int EG_VAL[6] = {94, 281, 297, 512, 936, 0};
constexpr int PHASE_W[6] = {0, 1, 1, 2, 4, 0};
constexpr int MAX_PHASE = 24;
constexpr int TEMPO = 12;

int pstMg[6][64], pstEg[6][64];  // indexed from White's point of view
Bitboard passedMask[2][64];
Bitboard adjFiles[8];

// Network state
nnue::Network g_net;
bool g_loaded = false;
bool g_use = false;

int centerDist(Square s) {  // 0 in a corner .. 6 in the centre
    const int f = fileOf(s), r = rankOf(s);
    return std::min(f, 7 - f) + std::min(r, 7 - r);
}

}  // namespace

void init() {
    static bool done = false;
    if (done) return;
    done = true;
    initBitboards();
    for (Square s = 0; s < 64; ++s) {
        const int f = fileOf(s), r = rankOf(s), cd = centerDist(s);
        // Pawns
        pstMg[PAWN][s] = (r >= 1 && r <= 6) ? (r - 1) * 4 + ((f == 3 || f == 4) ? 6 : 0) : 0;
        pstEg[PAWN][s] = (r >= 1 && r <= 6) ? (r - 1) * 9 : 0;
        // Knights, bishops, rooks, queens: centralisation
        pstMg[KNIGHT][s] = (cd - 3) * 8;   pstEg[KNIGHT][s] = (cd - 3) * 6;
        pstMg[BISHOP][s] = (cd - 3) * 4;   pstEg[BISHOP][s] = (cd - 3) * 3;
        pstMg[ROOK][s] = (f == 3 || f == 4) ? 4 : 0;
        pstEg[ROOK][s] = 0;
        if (r == 6) { pstMg[ROOK][s] += 12; pstEg[ROOK][s] += 22; }
        pstMg[QUEEN][s] = (cd - 3) * 2;    pstEg[QUEEN][s] = (cd - 3) * 4;
        // King: hide in the corner in the middlegame, centralise in the endgame
        pstMg[KING][s] = 20 - 14 * r - 4 * (cd - 1) + ((f <= 2 || f >= 6) ? 10 : 0);
        pstEg[KING][s] = (cd - 3) * 12;
    }
    for (int f = 0; f < 8; ++f) {
        adjFiles[f] = 0;
        if (f > 0) adjFiles[f] |= fileBB(f - 1);
        if (f < 7) adjFiles[f] |= fileBB(f + 1);
    }
    for (Square s = 0; s < 64; ++s) {
        const int f = fileOf(s), r = rankOf(s);
        Bitboard w = 0, b = 0;
        for (int rr = r + 1; rr < 8; ++rr) w |= rankBB(rr);
        for (int rr = r - 1; rr >= 0; --rr) b |= rankBB(rr);
        const Bitboard files = fileBB(f) | adjFiles[f];
        passedMask[WHITE][s] = w & files;
        passedMask[BLACK][s] = b & files;
    }
}

int evaluateClassical(const Position& pos) {
    int mg[2] = {0, 0}, eg[2] = {0, 0}, phase = 0;
    const Bitboard occ = pos.occupied();

    for (int c = 0; c < 2; ++c) {
        const Color us = Color(c), them = ~us;
        const Bitboard ownPawns = pos.pieces(us, PAWN), enemyPawns = pos.pieces(them, PAWN);
        const Bitboard enemyPawnAttacks = (them == WHITE)
            ? (shiftBB<7>(enemyPawns) | shiftBB<9>(enemyPawns))
            : (shiftBB<-7>(enemyPawns) | shiftBB<-9>(enemyPawns));
        const Bitboard mobilityArea = ~pos.pieces(us) & ~enemyPawnAttacks;

        for (int pt = PAWN; pt <= KING; ++pt) {
            Bitboard b = pos.pieces(us, PieceType(pt));
            if (pt != KING) {
                const int n = popcount(b);
                phase += n * PHASE_W[pt];
            }
            while (b) {
                const Square s = popLsb(b);
                const Square rel = us == WHITE ? s : (s ^ 56);
                mg[c] += MG_VAL[pt] + pstMg[pt][rel];
                eg[c] += EG_VAL[pt] + pstEg[pt][rel];

                Bitboard att = 0;
                switch (pt) {
                    case KNIGHT: att = KnightAttacks[s]; break;
                    case BISHOP: att = bishopAttacks(s, occ); break;
                    case ROOK: att = rookAttacks(s, occ); break;
                    case QUEEN: att = queenAttacks(s, occ); break;
                    default: break;
                }
                if (pt == KNIGHT) { const int m = popcount(att & mobilityArea) - 4; mg[c] += 4 * m; eg[c] += 4 * m; }
                else if (pt == BISHOP) { const int m = popcount(att & mobilityArea) - 6; mg[c] += 4 * m; eg[c] += 5 * m; }
                else if (pt == ROOK) {
                    const int m = popcount(att & mobilityArea) - 7; mg[c] += 2 * m; eg[c] += 4 * m;
                    const Bitboard fb = fileBB(fileOf(s));
                    if (!(fb & ownPawns)) { const int open = (fb & enemyPawns) ? 0 : 1; mg[c] += open ? 22 : 10; eg[c] += open ? 10 : 6; }
                } else if (pt == QUEEN) {
                    const int m = popcount(att & mobilityArea) - 14; mg[c] += 1 * m; eg[c] += 3 * m;
                } else if (pt == PAWN) {
                    const int f = fileOf(s);
                    const int relRank = us == WHITE ? rankOf(s) : 7 - rankOf(s);
                    if (!(passedMask[us][s] & enemyPawns)) {
                        static const int passMg[8] = {0, 2, 6, 14, 30, 58, 96, 0};
                        static const int passEg[8] = {0, 6, 14, 30, 60, 100, 150, 0};
                        mg[c] += passMg[relRank]; eg[c] += passEg[relRank];
                    }
                    if (!(adjFiles[f] & ownPawns)) { mg[c] -= 10; eg[c] -= 12; }  // isolated
                    if (popcount(fileBB(f) & ownPawns) > 1) { mg[c] -= 8; eg[c] -= 14; } // doubled (counted per pawn)
                } else if (pt == KING) {
                    // Pawn shelter: own pawns on the two ranks in front of the king.
                    const int f = fileOf(s), r = rankOf(s);
                    Bitboard shelter = 0;
                    const int dir = us == WHITE ? 1 : -1;
                    for (int step = 1; step <= 2; ++step) {
                        const int rr = r + dir * step;
                        if (rr < 0 || rr > 7) continue;
                        shelter |= rankBB(rr);
                    }
                    shelter &= (fileBB(f) | adjFiles[f]);
                    mg[c] += 7 * popcount(shelter & ownPawns);
                }
            }
        }
        if (popcount(pos.pieces(us, BISHOP)) >= 2) { mg[c] += 28; eg[c] += 48; }
    }

    phase = std::min(phase, MAX_PHASE);
    const int mgScore = mg[WHITE] - mg[BLACK];
    const int egScore = eg[WHITE] - eg[BLACK];
    int score = (mgScore * phase + egScore * (MAX_PHASE - phase)) / MAX_PHASE;
    if (pos.insufficientMaterial()) score = 0;
    return (pos.sideToMove() == WHITE ? score : -score) + TEMPO;
}

int evaluate(const Position& pos) {
    if (const nnue::Network* net = pos.network()) {
        if (pos.insufficientMaterial()) return 0;
        return nnue::evaluate(*net, pos.accumulator(), pos.sideToMove());
    }
    return evaluateClassical(pos);
}

bool loadNetwork(const std::string& path, std::string& err) {
    nnue::Network n;
    if (!nnue::load(path, n, err)) return false;
    g_net = std::move(n);
    g_loaded = true;
    return true;
}
void unloadNetwork() { g_loaded = false; g_net = nnue::Network{}; }
const nnue::Network* activeNetwork() { return (g_use && g_loaded) ? &g_net : nullptr; }
const nnue::Network* loadedNetwork() { return g_loaded ? &g_net : nullptr; }
void setUseNnue(bool on) { g_use = on; }
bool useNnue() { return g_use; }
bool networkLoaded() { return g_loaded; }

}  // namespace eval
}  // namespace astra
