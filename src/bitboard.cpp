// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#include "bitboard.h"

#include <cstdlib>

namespace astra {

Bitboard KnightAttacks[64];
Bitboard KingAttacks[64];
Bitboard PawnAttacks[2][64];
Bitboard BetweenBB[64][64];
Bitboard LineBB[64][64];
Magic RookMagics[64];
Magic BishopMagics[64];

namespace {

Bitboard rookTable[0x19000];
Bitboard bishopTable[0x1480];

struct Prng {
    std::uint64_t s;
    explicit Prng(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    std::uint64_t sparse() { return next() & next() & next(); }
};

void initMagics(Bitboard* table, Magic* magics, bool rook) {
    static const std::uint64_t seeds[8] = {728, 10316, 55013, 32803, 12281, 15100, 16645, 255};
    Bitboard occupancy[4096], reference[4096];
    int epoch[4096] = {}, cnt = 0, size = 0;

    for (Square s = 0; s < 64; ++s) {
        const Bitboard edges = ((RANK_1 | RANK_8) & ~rankBB(rankOf(s))) |
                               ((FILE_A | FILE_H) & ~fileBB(fileOf(s)));
        Magic& m = magics[s];
        m.mask = slidingAttacksSlow(s, 0, rook) & ~edges;
        m.shift = 64 - popcount(m.mask);
        m.attacks = (s == 0) ? table : magics[s - 1].attacks + size;

        size = 0;
        Bitboard b = 0;
        do {  // Carry-Rippler enumeration of all subsets of the mask
            occupancy[size] = b;
            reference[size] = slidingAttacksSlow(s, b, rook);
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);

        Prng rng(seeds[rankOf(s)]);
        for (int i = 0; i < size;) {
            m.magic = 0;
            while (popcount((m.magic * m.mask) >> 56) < 6) m.magic = rng.sparse();
            for (++cnt, i = 0; i < size; ++i) {
                const unsigned idx = unsigned((occupancy[i] & m.mask) * m.magic >> m.shift);
                if (epoch[idx] < cnt) {
                    epoch[idx] = cnt;
                    m.attacks[idx] = reference[i];
                } else if (m.attacks[idx] != reference[i]) {
                    break;
                }
            }
        }
    }
}

}  // namespace

Bitboard slidingAttacksSlow(Square s, Bitboard occ, bool rook) {
    static const int rookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static const int bishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const auto& dirs = rook ? rookDirs : bishopDirs;
    Bitboard result = 0;
    for (const auto& d : dirs) {
        int f = fileOf(s) + d[0], r = rankOf(s) + d[1];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            const Bitboard b = bit(makeSquare(f, r));
            result |= b;
            if (occ & b) break;
            f += d[0];
            r += d[1];
        }
    }
    return result;
}

void initBitboards() {
    static bool done = false;
    if (done) return;
    done = true;

    for (Square s = 0; s < 64; ++s) {
        const int f = fileOf(s), r = rankOf(s);
        static const int kn[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        for (const auto& d : kn) {
            const int nf = f + d[0], nr = r + d[1];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) KnightAttacks[s] |= bit(makeSquare(nf, nr));
        }
        for (int df = -1; df <= 1; ++df)
            for (int dr = -1; dr <= 1; ++dr) {
                if (!df && !dr) continue;
                const int nf = f + df, nr = r + dr;
                if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) KingAttacks[s] |= bit(makeSquare(nf, nr));
            }
        for (int df : {-1, 1}) {
            if (f + df < 0 || f + df > 7) continue;
            if (r < 7) PawnAttacks[WHITE][s] |= bit(makeSquare(f + df, r + 1));
            if (r > 0) PawnAttacks[BLACK][s] |= bit(makeSquare(f + df, r - 1));
        }
    }

    initMagics(rookTable, RookMagics, true);
    initMagics(bishopTable, BishopMagics, false);

    for (Square a = 0; a < 64; ++a)
        for (Square b = 0; b < 64; ++b) {
            if (a == b) continue;
            if (rookAttacks(a, 0) & bit(b)) {
                LineBB[a][b] = (rookAttacks(a, 0) & rookAttacks(b, 0)) | bit(a) | bit(b);
                BetweenBB[a][b] = rookAttacks(a, bit(b)) & rookAttacks(b, bit(a));
            } else if (bishopAttacks(a, 0) & bit(b)) {
                LineBB[a][b] = (bishopAttacks(a, 0) & bishopAttacks(b, 0)) | bit(a) | bit(b);
                BetweenBB[a][b] = bishopAttacks(a, bit(b)) & bishopAttacks(b, bit(a));
            }
        }
}

}  // namespace astra
