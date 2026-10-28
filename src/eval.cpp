// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#include "eval.h"

#include <algorithm>
#include <cstdlib>

namespace astra {
namespace eval {

const int PieceValue[PIECE_TYPE_NB] = {100, 320, 330, 500, 900, 0};

// ---------------------------------------------------------------------------------------
// Tunable parameters (all evaluation weights live here so tools/tune can optimise them)
// ---------------------------------------------------------------------------------------
namespace {

struct ParamDef { const char* name; int value; int step; };

enum ParamId {
    MG_N, MG_B, MG_R, MG_Q, EG_P, EG_N, EG_B, EG_R, EG_Q,
    PAWN_ADV_MG, PAWN_ADV_EG, PAWN_CTR_MG,
    KN_MG, KN_EG, BI_MG, BI_EG, QU_MG, QU_EG,
    RK_CTR_MG, RK7_MG, RK7_EG,
    K_RANK_MG, K_CTR_MG, K_SIDE_MG, K_CTR_EG,
    MOB_N_MG, MOB_N_EG, MOB_B_MG, MOB_B_EG, MOB_R_MG, MOB_R_EG, MOB_Q_MG, MOB_Q_EG,
    PASS_MG1, PASS_MG2, PASS_MG3, PASS_MG4, PASS_MG5, PASS_MG6,
    PASS_EG1, PASS_EG2, PASS_EG3, PASS_EG4, PASS_EG5, PASS_EG6,
    ISO_MG, ISO_EG, DBL_MG, DBL_EG,
    ROOK_OPEN_MG, ROOK_OPEN_EG, ROOK_SEMI_MG, ROOK_SEMI_EG,
    BPAIR_MG, BPAIR_EG, SHELTER_MG, TEMPO_V,
    THREAT_PAWN_MG, THREAT_PAWN_EG, THREAT_MINOR_MG, THREAT_MINOR_EG, OUTPOST_MG, OUTPOST_EG,
    KS_DIV, KS_MAX,
    PARAM_COUNT
};

ParamDef params[PARAM_COUNT] = {
    {"MG_N", 372, 5}, {"MG_B", 400, 5}, {"MG_R", 547, 5}, {"MG_Q", 1190, 5},
    {"EG_P", 88, 3}, {"EG_N", 251, 5}, {"EG_B", 292, 5}, {"EG_R", 482, 5}, {"EG_Q", 806, 5},
    {"PAWN_ADV_MG", 0, 1}, {"PAWN_ADV_EG", 3, 1}, {"PAWN_CTR_MG", 13, 1},
    {"KN_MG", 10, 1}, {"KN_EG", 6, 1}, {"BI_MG", 10, 1}, {"BI_EG", -2, 1}, {"QU_MG", 5, 1}, {"QU_EG", 4, 1},
    {"RK_CTR_MG", 20, 1}, {"RK7_MG", 68, 2}, {"RK7_EG", -12, 2},
    {"K_RANK_MG", -5, 1}, {"K_CTR_MG", 14, 1}, {"K_SIDE_MG", 14, 2}, {"K_CTR_EG", 3, 1},
    {"MOB_N_MG", 8, 1}, {"MOB_N_EG", -1, 1}, {"MOB_B_MG", 8, 1}, {"MOB_B_EG", 5, 1},
    {"MOB_R_MG", 2, 1}, {"MOB_R_EG", 7, 1}, {"MOB_Q_MG", 6, 1}, {"MOB_Q_EG", -8, 1},
    {"PASS_MG1", 2, 2}, {"PASS_MG2", -38, 2}, {"PASS_MG3", -16, 2}, {"PASS_MG4", 30, 3}, {"PASS_MG5", 110, 4}, {"PASS_MG6", 216, 5},
    {"PASS_EG1", 30, 2}, {"PASS_EG2", 34, 2}, {"PASS_EG3", 48, 3}, {"PASS_EG4", 60, 4}, {"PASS_EG5", 70, 5}, {"PASS_EG6", 102, 6},
    {"ISO_MG", 24, 2}, {"ISO_EG", 6, 2}, {"DBL_MG", -10, 2}, {"DBL_EG", 18, 2},
    {"ROOK_OPEN_MG", 56, 2}, {"ROOK_OPEN_EG", -10, 2}, {"ROOK_SEMI_MG", 32, 2}, {"ROOK_SEMI_EG", -4, 2},
    {"BPAIR_MG", 28, 3}, {"BPAIR_EG", 42, 3}, {"SHELTER_MG", 0, 1}, {"TEMPO", 8, 2},
    {"THREAT_PAWN_MG", 46, 3}, {"THREAT_PAWN_EG", 38, 3}, {"THREAT_MINOR_MG", 48, 2}, {"THREAT_MINOR_EG", 16, 2},
    {"OUTPOST_MG", 56, 2}, {"OUTPOST_EG", 28, 2},
    {"KS_DIV", 2, 1}, {"KS_MAX", 120, 10},
};

#define P(id) (params[id].value)

constexpr int MG_PAWN = 82;
constexpr int PHASE_W[6] = {0, 1, 1, 2, 4, 0};
constexpr int MAX_PHASE = 24;

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

int paramCount() { return PARAM_COUNT; }
int& param(int i) { return params[i].value; }
const char* paramName(int i) { return params[i].name; }
int paramStep(int i) { return params[i].step; }

void rebuildTables() {
    for (Square s = 0; s < 64; ++s) {
        const int f = fileOf(s), r = rankOf(s), cd = centerDist(s);
        pstMg[PAWN][s] = (r >= 1 && r <= 6) ? (r - 1) * P(PAWN_ADV_MG) + ((f == 3 || f == 4) ? P(PAWN_CTR_MG) : 0) : 0;
        pstEg[PAWN][s] = (r >= 1 && r <= 6) ? (r - 1) * P(PAWN_ADV_EG) : 0;
        pstMg[KNIGHT][s] = (cd - 3) * P(KN_MG);   pstEg[KNIGHT][s] = (cd - 3) * P(KN_EG);
        pstMg[BISHOP][s] = (cd - 3) * P(BI_MG);   pstEg[BISHOP][s] = (cd - 3) * P(BI_EG);
        pstMg[ROOK][s] = (f == 3 || f == 4) ? P(RK_CTR_MG) : 0;
        pstEg[ROOK][s] = 0;
        if (r == 6) { pstMg[ROOK][s] += P(RK7_MG); pstEg[ROOK][s] += P(RK7_EG); }
        pstMg[QUEEN][s] = (cd - 3) * P(QU_MG);    pstEg[QUEEN][s] = (cd - 3) * P(QU_EG);
        pstMg[KING][s] = 20 - P(K_RANK_MG) * r - P(K_CTR_MG) * (cd - 1) + ((f <= 2 || f >= 6) ? P(K_SIDE_MG) : 0);
        pstEg[KING][s] = (cd - 3) * P(K_CTR_EG);
    }
}

void init() {
    static bool done = false;
    if (done) return;
    done = true;
    initBitboards();
    rebuildTables();
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
    const int mgVal[6] = {MG_PAWN, P(MG_N), P(MG_B), P(MG_R), P(MG_Q), 0};
    const int egVal[6] = {P(EG_P), P(EG_N), P(EG_B), P(EG_R), P(EG_Q), 0};
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
            if (pt != KING) phase += popcount(b) * PHASE_W[pt];
            while (b) {
                const Square s = popLsb(b);
                const Square rel = us == WHITE ? s : (s ^ 56);
                mg[c] += mgVal[pt] + pstMg[pt][rel];
                eg[c] += egVal[pt] + pstEg[pt][rel];

                Bitboard att = 0;
                switch (pt) {
                    case KNIGHT: att = KnightAttacks[s]; break;
                    case BISHOP: att = bishopAttacks(s, occ); break;
                    case ROOK: att = rookAttacks(s, occ); break;
                    case QUEEN: att = queenAttacks(s, occ); break;
                    default: break;
                }
                if (pt == KNIGHT) { const int m = popcount(att & mobilityArea) - 4; mg[c] += P(MOB_N_MG) * m; eg[c] += P(MOB_N_EG) * m; }
                else if (pt == BISHOP) { const int m = popcount(att & mobilityArea) - 6; mg[c] += P(MOB_B_MG) * m; eg[c] += P(MOB_B_EG) * m; }
                else if (pt == ROOK) {
                    const int m = popcount(att & mobilityArea) - 7; mg[c] += P(MOB_R_MG) * m; eg[c] += P(MOB_R_EG) * m;
                    const Bitboard fb = fileBB(fileOf(s));
                    if (!(fb & ownPawns)) {
                        const bool open = !(fb & enemyPawns);
                        mg[c] += open ? P(ROOK_OPEN_MG) : P(ROOK_SEMI_MG);
                        eg[c] += open ? P(ROOK_OPEN_EG) : P(ROOK_SEMI_EG);
                    }
                } else if (pt == QUEEN) {
                    const int m = popcount(att & mobilityArea) - 14; mg[c] += P(MOB_Q_MG) * m; eg[c] += P(MOB_Q_EG) * m;
                } else if (pt == PAWN) {
                    const int f = fileOf(s);
                    const int relRank = us == WHITE ? rankOf(s) : 7 - rankOf(s);
                    if (!(passedMask[us][s] & enemyPawns) && relRank >= 1 && relRank <= 6) {
                        mg[c] += P(PASS_MG1 + relRank - 1);
                        eg[c] += P(PASS_EG1 + relRank - 1);
                    }
                    if (!(adjFiles[f] & ownPawns)) { mg[c] -= P(ISO_MG); eg[c] -= P(ISO_EG); }
                    if (popcount(fileBB(f) & ownPawns) > 1) { mg[c] -= P(DBL_MG); eg[c] -= P(DBL_EG); }
                } else if (pt == KING) {
                    const int f = fileOf(s), r = rankOf(s);
                    Bitboard shelter = 0;
                    const int dir = us == WHITE ? 1 : -1;
                    for (int step = 1; step <= 2; ++step) {
                        const int rr = r + dir * step;
                        if (rr < 0 || rr > 7) continue;
                        shelter |= rankBB(rr);
                    }
                    shelter &= (fileBB(f) | adjFiles[f]);
                    mg[c] += P(SHELTER_MG) * popcount(shelter & ownPawns);
                }
            }
        }
        if (popcount(pos.pieces(us, BISHOP)) >= 2) { mg[c] += P(BPAIR_MG); eg[c] += P(BPAIR_EG); }

        // --- Threats: pawns attacking minors/rooks/queens; minors attacking rooks/queens ---
        const Bitboard ownPawnAtt = (us == WHITE) ? (shiftBB<7>(ownPawns) | shiftBB<9>(ownPawns))
                                                  : (shiftBB<-7>(ownPawns) | shiftBB<-9>(ownPawns));
        const Bitboard enemyMinors = pos.pieces(them, KNIGHT) | pos.pieces(them, BISHOP);
        const Bitboard enemyHeavy = pos.pieces(them, ROOK) | pos.pieces(them, QUEEN);
        mg[c] += P(THREAT_PAWN_MG) * popcount(ownPawnAtt & (enemyMinors | enemyHeavy));
        eg[c] += P(THREAT_PAWN_EG) * popcount(ownPawnAtt & (enemyMinors | enemyHeavy));
        {
            Bitboard minors = pos.pieces(us, KNIGHT) | pos.pieces(us, BISHOP);
            Bitboard minorAtt = 0;
            while (minors) {
                const Square ms = popLsb(minors);
                minorAtt |= (typeOf(pos.pieceOn(ms)) == KNIGHT) ? KnightAttacks[ms] : bishopAttacks(ms, occ);
            }
            mg[c] += P(THREAT_MINOR_MG) * popcount(minorAtt & enemyHeavy);
            eg[c] += P(THREAT_MINOR_EG) * popcount(minorAtt & enemyHeavy);
        }

        // --- Knight outposts ---
        {
            Bitboard kn = pos.pieces(us, KNIGHT);
            while (kn) {
                const Square ks = popLsb(kn);
                const int rr = us == WHITE ? rankOf(ks) : 7 - rankOf(ks);
                if (rr >= 3 && rr <= 5 && (ownPawnAtt & bit(ks)) &&
                    !(passedMask[us][ks] & adjFiles[fileOf(ks)] & enemyPawns)) { mg[c] += P(OUTPOST_MG); eg[c] += P(OUTPOST_EG); }
            }
        }

        // --- King safety: weighted attackers on the enemy king zone (middlegame only) ---
        {
            const Square ksq = pos.kingSq(them);
            const Bitboard zone = KingAttacks[ksq] | bit(ksq);
            int units = 0, attackers = 0;
            static const int W[6] = {0, 2, 2, 3, 5, 0};
            for (int pt = KNIGHT; pt <= QUEEN; ++pt) {
                Bitboard b = pos.pieces(us, PieceType(pt));
                while (b) {
                    const Square s2 = popLsb(b);
                    Bitboard att = pt == KNIGHT ? KnightAttacks[s2] : pt == BISHOP ? bishopAttacks(s2, occ)
                                 : pt == ROOK ? rookAttacks(s2, occ) : queenAttacks(s2, occ);
                    if (att & zone) { ++attackers; units += W[pt] * popcount(att & zone); }
                }
            }
            if (attackers >= 2 && pos.pieces(us, QUEEN)) mg[c] += std::min(units * units / std::max(1, P(KS_DIV)), P(KS_MAX));
        }
    }

    phase = std::min(phase, MAX_PHASE);
    const int mgScore = mg[WHITE] - mg[BLACK];
    const int egScore = eg[WHITE] - eg[BLACK];
    int score = (mgScore * phase + egScore * (MAX_PHASE - phase)) / MAX_PHASE;
    if (pos.insufficientMaterial()) score = 0;
    return (pos.sideToMove() == WHITE ? score : -score) + P(TEMPO_V);
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
