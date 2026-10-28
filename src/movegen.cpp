#include "movegen.h"

namespace astra {

namespace {

inline void addPromotions(MoveList& ml, Square from, Square to, bool capture, bool all) {
    const int base = capture ? F_PROMO_CAP_N : F_PROMO_N;
    ml.add(makeMove(from, to, base + 3));  // queen
    if (all) {
        ml.add(makeMove(from, to, base + 2));  // rook
        ml.add(makeMove(from, to, base + 1));  // bishop
        ml.add(makeMove(from, to, base));      // knight
    }
}

template <Color Us, GenType GT>
void genPawns(const Position& pos, MoveList& ml) {
    constexpr Color Them = ~Us;
    constexpr int Up = Us == WHITE ? 8 : -8;
    constexpr int UpL = Us == WHITE ? 7 : -9;
    constexpr int UpR = Us == WHITE ? 9 : -7;
    constexpr Bitboard Rank7BB = Us == WHITE ? RANK_7 : RANK_2;
    constexpr Bitboard Rank3BB = Us == WHITE ? RANK_3 : RANK_6;
    constexpr bool All = GT == GEN_ALL;

    const Bitboard pawns = pos.pieces(Us, PAWN);
    const Bitboard empty = ~pos.occupied();
    const Bitboard enemies = pos.pieces(Them);
    const Bitboard promo = pawns & Rank7BB;
    const Bitboard rest = pawns & ~Rank7BB;

    if constexpr (All) {
        Bitboard b1 = shiftBB<Up>(rest) & empty;
        Bitboard b2 = shiftBB<Up>(b1 & Rank3BB) & empty;
        while (b1) { const Square to = popLsb(b1); ml.add(makeMove(to - Up, to)); }
        while (b2) { const Square to = popLsb(b2); ml.add(makeMove(to - 2 * Up, to, F_DOUBLE_PUSH)); }
    }

    if (promo) {
        Bitboard b = shiftBB<Up>(promo) & empty;
        while (b) { const Square to = popLsb(b); addPromotions(ml, to - Up, to, false, All); }
        Bitboard l = shiftBB<UpL>(promo) & enemies;
        while (l) { const Square to = popLsb(l); addPromotions(ml, to - UpL, to, true, true); }
        Bitboard r = shiftBB<UpR>(promo) & enemies;
        while (r) { const Square to = popLsb(r); addPromotions(ml, to - UpR, to, true, true); }
    }

    Bitboard l = shiftBB<UpL>(rest) & enemies;
    Bitboard r = shiftBB<UpR>(rest) & enemies;
    while (l) { const Square to = popLsb(l); ml.add(makeMove(to - UpL, to, F_CAPTURE)); }
    while (r) { const Square to = popLsb(r); ml.add(makeMove(to - UpR, to, F_CAPTURE)); }

    if (pos.epSquare() != SQ_NONE) {
        Bitboard b = PawnAttacks[Them][pos.epSquare()] & pawns;
        while (b) ml.add(makeMove(popLsb(b), pos.epSquare(), F_EP));
    }
}

template <PieceType Pt>
inline Bitboard attacksOf(Square s, Bitboard occ) {
    if constexpr (Pt == KNIGHT) return KnightAttacks[s];
    else if constexpr (Pt == BISHOP) return bishopAttacks(s, occ);
    else if constexpr (Pt == ROOK) return rookAttacks(s, occ);
    else if constexpr (Pt == QUEEN) return queenAttacks(s, occ);
    else return KingAttacks[s];
}

template <Color Us, PieceType Pt, GenType GT>
void genPieces(const Position& pos, MoveList& ml) {
    const Bitboard occ = pos.occupied();
    const Bitboard enemies = pos.pieces(~Us);
    const Bitboard target = GT == GEN_ALL ? ~pos.pieces(Us) : enemies;
    Bitboard b = pos.pieces(Us, Pt);
    while (b) {
        const Square from = popLsb(b);
        Bitboard att = attacksOf<Pt>(from, occ) & target;
        while (att) {
            const Square to = popLsb(att);
            ml.add(makeMove(from, to, (enemies & bit(to)) ? F_CAPTURE : F_QUIET));
        }
    }
}

template <Color Us>
void genCastling(const Position& pos, MoveList& ml) {
    constexpr Color Them = ~Us;
    constexpr int OO = Us == WHITE ? WHITE_OO : BLACK_OO;
    constexpr int OOO = Us == WHITE ? WHITE_OOO : BLACK_OOO;
    constexpr Square e = Us == WHITE ? 4 : 60;
    if (!(pos.castlingRights() & (OO | OOO)) || pos.inCheck()) return;
    const Bitboard occ = pos.occupied();
    if ((pos.castlingRights() & OO) && !(occ & (bit(e + 1) | bit(e + 2))) &&
        !pos.attackedBy(e + 1, Them, occ) && !pos.attackedBy(e + 2, Them, occ))
        ml.add(makeMove(e, e + 2, F_KING_CASTLE));
    if ((pos.castlingRights() & OOO) && !(occ & (bit(e - 1) | bit(e - 2) | bit(e - 3))) &&
        !pos.attackedBy(e - 1, Them, occ) && !pos.attackedBy(e - 2, Them, occ))
        ml.add(makeMove(e, e - 2, F_QUEEN_CASTLE));
}

template <Color Us, GenType GT>
void generateFor(const Position& pos, MoveList& ml) {
    genPawns<Us, GT>(pos, ml);
    genPieces<Us, KNIGHT, GT>(pos, ml);
    genPieces<Us, BISHOP, GT>(pos, ml);
    genPieces<Us, ROOK, GT>(pos, ml);
    genPieces<Us, QUEEN, GT>(pos, ml);
    genPieces<Us, KING, GT>(pos, ml);
    if constexpr (GT == GEN_ALL) genCastling<Us>(pos, ml);
}

}  // namespace

void generate(const Position& pos, MoveList& ml, GenType gt) {
    ml.size = 0;
    if (pos.sideToMove() == WHITE) {
        if (gt == GEN_ALL) generateFor<WHITE, GEN_ALL>(pos, ml);
        else generateFor<WHITE, GEN_CAPTURES>(pos, ml);
    } else {
        if (gt == GEN_ALL) generateFor<BLACK, GEN_ALL>(pos, ml);
        else generateFor<BLACK, GEN_CAPTURES>(pos, ml);
    }
}

int generateLegal(const Position& pos, MoveList& out) {
    MoveList pseudo;
    generate(pos, pseudo, GEN_ALL);
    out.size = 0;
    for (int i = 0; i < pseudo.size; ++i)
        if (pos.legal(pseudo.moves[i])) out.add(pseudo.moves[i]);
    return out.size;
}

std::uint64_t perft(Position& pos, int depth) {
    MoveList ml;
    generate(pos, ml, GEN_ALL);
    std::uint64_t n = 0;
    for (int i = 0; i < ml.size; ++i) {
        const Move m = ml.moves[i];
        if (!pos.legal(m)) continue;
        if (depth <= 1) { ++n; continue; }  // bulk counting at the leaves
        pos.makeMove(m);
        n += perft(pos, depth - 1);
        pos.unmakeMove();
    }
    return n;
}

}  // namespace astra
