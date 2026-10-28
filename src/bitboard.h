// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
// Astra Chess - bitboards and attack tables (magic bitboards for sliders)
#pragma once
#include "types.h"

namespace astra {

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = FILE_A << 7;
constexpr Bitboard RANK_1 = 0xFFULL;
constexpr Bitboard RANK_2 = RANK_1 << 8;
constexpr Bitboard RANK_3 = RANK_1 << 16;
constexpr Bitboard RANK_6 = RANK_1 << 40;
constexpr Bitboard RANK_7 = RANK_1 << 48;
constexpr Bitboard RANK_8 = RANK_1 << 56;

constexpr Bitboard bit(Square s) { return 1ULL << s; }
constexpr Bitboard fileBB(int f) { return FILE_A << f; }
constexpr Bitboard rankBB(int r) { return RANK_1 << (8 * r); }

inline int popcount(Bitboard b) { return __builtin_popcountll(b); }
inline Square lsb(Bitboard b) { return __builtin_ctzll(b); }
inline Square popLsb(Bitboard& b) { Square s = lsb(b); b &= b - 1; return s; }
constexpr bool moreThanOne(Bitboard b) { return b & (b - 1); }

template <int D>
constexpr Bitboard shiftBB(Bitboard b) {
    if constexpr (D == 8) return b << 8;
    else if constexpr (D == -8) return b >> 8;
    else if constexpr (D == 9) return (b & ~FILE_H) << 9;
    else if constexpr (D == 7) return (b & ~FILE_A) << 7;
    else if constexpr (D == -7) return (b & ~FILE_H) >> 7;
    else return (b & ~FILE_A) >> 9;  // D == -9
}

struct Magic {
    Bitboard mask = 0;
    Bitboard magic = 0;
    Bitboard* attacks = nullptr;
    unsigned shift = 0;
};

extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];
extern Bitboard PawnAttacks[2][64];
extern Bitboard BetweenBB[64][64];  // squares strictly between two aligned squares
extern Bitboard LineBB[64][64];     // full line through two aligned squares
extern Magic RookMagics[64];
extern Magic BishopMagics[64];

inline Bitboard rookAttacks(Square s, Bitboard occ) {
    const Magic& m = RookMagics[s];
    return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
}
inline Bitboard bishopAttacks(Square s, Bitboard occ) {
    const Magic& m = BishopMagics[s];
    return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
}
inline Bitboard queenAttacks(Square s, Bitboard occ) {
    return rookAttacks(s, occ) | bishopAttacks(s, occ);
}
inline bool aligned(Square a, Square b, Square c) { return LineBB[a][b] & bit(c); }

// Slow reference implementation (used to build tables and for self-tests).
Bitboard slidingAttacksSlow(Square s, Bitboard occ, bool rook);

void initBitboards();

}  // namespace astra
