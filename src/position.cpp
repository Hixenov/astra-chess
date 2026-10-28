// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#include "position.h"

#include <cctype>
#include <sstream>

#include "movegen.h"

namespace astra {

namespace {

Key zobPiece[PIECE_NB][64];
Key zobCastle[16];
Key zobEp[8];
Key zobSide;
int castleMask[64];

struct Rng {
    std::uint64_t s = 0xA57A5EEDC0FFEE05ULL;
    std::uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
};

constexpr int SEE_VALUE[PIECE_TYPE_NB] = {100, 320, 330, 500, 900, 0};

}  // namespace

void Position::init() {
    static bool done = false;
    if (done) return;
    done = true;
    initBitboards();
    Rng rng;
    for (auto& row : zobPiece)
        for (auto& k : row) k = rng.next();
    for (auto& k : zobCastle) k = rng.next();
    for (auto& k : zobEp) k = rng.next();
    zobSide = rng.next();
    for (int& m : castleMask) m = 15;
    castleMask[4] = 15 & ~(WHITE_OO | WHITE_OOO);
    castleMask[0] = 15 & ~WHITE_OOO;
    castleMask[7] = 15 & ~WHITE_OO;
    castleMask[60] = 15 & ~(BLACK_OO | BLACK_OOO);
    castleMask[56] = 15 & ~BLACK_OOO;
    castleMask[63] = 15 & ~BLACK_OO;
}

const char* Position::startFen() {
    return "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
}

Position::Position() {
    init();
    for (auto& p : board_) p = NO_PIECE;
    for (auto& b : byType_) b = 0;
    for (auto& b : byColor_) b = 0;
    states_.reserve(1024);
    accs_.reserve(1024);
}

Key Position::computeKey() const {
    Key k = 0;
    for (Square s = 0; s < 64; ++s)
        if (board_[s] != NO_PIECE) k ^= zobPiece[board_[s]][s];
    if (stm_ == BLACK) k ^= zobSide;
    k ^= zobCastle[castling_];
    if (ep_ != SQ_NONE) k ^= zobEp[fileOf(ep_)];
    return k;
}

bool Position::setFen(const std::string& fen) {
    std::istringstream in(fen);
    std::string placement, turn, castle, eps;
    int half = 0, full = 1;
    if (!(in >> placement >> turn >> castle >> eps)) return false;
    in >> half >> full;

    Piece board[64];
    for (auto& p : board) p = NO_PIECE;
    int r = 7, f = 0;
    for (char c : placement) {
        if (c == '/') {
            if (f != 8 || r == 0) return false;
            --r; f = 0;
        } else if (c >= '1' && c <= '8') {
            f += c - '0';
            if (f > 8) return false;
        } else {
            const std::string names = "PNBRQKpnbrqk";
            const auto idx = names.find(c);
            if (idx == std::string::npos || f >= 8) return false;
            board[makeSquare(f++, r)] = Piece(idx);
        }
    }
    if (r != 0 || f != 8) return false;
    if (turn != "w" && turn != "b") return false;

    // Commit
    for (auto& b : byType_) b = 0;
    for (auto& b : byColor_) b = 0;
    for (Square s = 0; s < 64; ++s) {
        board_[s] = NO_PIECE;
        if (board[s] != NO_PIECE) putRaw(board[s], s);
    }
    stm_ = turn == "w" ? WHITE : BLACK;

    castling_ = 0;
    if (castle != "-") {
        for (char c : castle) {
            if (c == 'K') castling_ |= WHITE_OO;
            else if (c == 'Q') castling_ |= WHITE_OOO;
            else if (c == 'k') castling_ |= BLACK_OO;
            else if (c == 'q') castling_ |= BLACK_OOO;
            else return false;
        }
    }
    // Drop castling rights that cannot be exercised.
    if (board_[4] != W_KING || board_[7] != W_ROOK) castling_ &= ~WHITE_OO;
    if (board_[4] != W_KING || board_[0] != W_ROOK) castling_ &= ~WHITE_OOO;
    if (board_[60] != B_KING || board_[63] != B_ROOK) castling_ &= ~BLACK_OO;
    if (board_[60] != B_KING || board_[56] != B_ROOK) castling_ &= ~BLACK_OOO;

    ep_ = SQ_NONE;
    if (eps != "-") {
        if (eps.size() != 2 || eps[0] < 'a' || eps[0] > 'h' || eps[1] < '1' || eps[1] > '8') return false;
        ep_ = makeSquare(eps[0] - 'a', eps[1] - '1');
        // En passant square must be on the 3rd/6th rank behind the pushed pawn.
        const int wantRank = stm_ == WHITE ? 5 : 2;
        const Piece pushed = stm_ == WHITE ? B_PAWN : W_PAWN;
        const Square pawnSq = ep_ + (stm_ == WHITE ? -8 : 8);
        if (rankOf(ep_) != wantRank || board_[ep_] != NO_PIECE || board_[pawnSq] != pushed) ep_ = SQ_NONE;
    }
    halfmove_ = half;
    fullmove_ = full < 1 ? 1 : full;
    states_.clear();
    accs_.clear();
    key_ = computeKey();
    std::string why;
    if (!validate(&why)) return false;
    updateCheckInfo();
    if (net_) refreshAccumulator();
    return true;
}

bool Position::validate(std::string* why) const {
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1) return fail("need exactly one king per side");
    if (pieces(PAWN) & (RANK_1 | RANK_8)) return fail("pawn on back rank");
    if (popcount(pieces(WHITE, PAWN)) > 8 || popcount(pieces(BLACK, PAWN)) > 8) return fail("too many pawns");
    // The side NOT to move must not be in check.
    if (attackedBy(kingSq(~stm_), stm_, occupied())) return fail("side not to move is in check");
    return true;
}

std::string Position::fen() const {
    std::string out;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            const Piece p = board_[makeSquare(f, r)];
            if (p == NO_PIECE) { ++empty; continue; }
            if (empty) { out += char('0' + empty); empty = 0; }
            out += "PNBRQKpnbrqk"[p];
        }
        if (empty) out += char('0' + empty);
        if (r) out += '/';
    }
    out += stm_ == WHITE ? " w " : " b ";
    std::string c;
    if (castling_ & WHITE_OO) c += 'K';
    if (castling_ & WHITE_OOO) c += 'Q';
    if (castling_ & BLACK_OO) c += 'k';
    if (castling_ & BLACK_OOO) c += 'q';
    out += c.empty() ? "-" : c;
    out += ' ';
    out += ep_ == SQ_NONE ? "-" : squareName(ep_);
    out += ' ' + std::to_string(halfmove_) + ' ' + std::to_string(fullmove_);
    return out;
}

Bitboard Position::attackersTo(Square s, Bitboard occ) const {
    return (PawnAttacks[BLACK][s] & pieces(WHITE, PAWN)) | (PawnAttacks[WHITE][s] & pieces(BLACK, PAWN)) |
           (KnightAttacks[s] & byType_[KNIGHT]) | (KingAttacks[s] & byType_[KING]) |
           (rookAttacks(s, occ) & (byType_[ROOK] | byType_[QUEEN])) |
           (bishopAttacks(s, occ) & (byType_[BISHOP] | byType_[QUEEN]));
}

void Position::updateCheckInfo() {
    const Color us = stm_, them = ~us;
    const Square ksq = kingSq(us);
    const Bitboard occ = occupied();
    checkers_ = attackersTo(ksq, occ) & byColor_[them];
    pinned_ = 0;
    Bitboard snipers = (rookAttacks(ksq, 0) & (pieces(them, ROOK) | pieces(them, QUEEN))) |
                       (bishopAttacks(ksq, 0) & (pieces(them, BISHOP) | pieces(them, QUEEN)));
    while (snipers) {
        const Square sq = popLsb(snipers);
        const Bitboard b = BetweenBB[ksq][sq] & occ;
        if (b && !moreThanOne(b) && (b & byColor_[us])) pinned_ |= b;
    }
}

// ---------------------------------------------------------------------------------------
// Make / unmake
// ---------------------------------------------------------------------------------------

void Position::makeMove(Move m) {
    StateInfo prev{key_, checkers_, pinned_, castling_, ep_, halfmove_, NO_PIECE, m};
    const Color us = stm_, them = ~us;
    const Square from = fromSq(m), to = toSq(m);
    const int fl = flagOf(m);
    const Piece pc = board_[from];

    if (net_) accs_.push_back(accs_.back());
    nnue::Accumulator* acc = net_ ? &accs_.back() : nullptr;

    auto put = [&](Piece p, Square s) {
        putRaw(p, s);
        key_ ^= zobPiece[p][s];
        if (acc) nnue::addFeature(*net_, *acc, p, s);
    };
    auto rem = [&](Piece p, Square s) {
        removeRaw(p, s);
        key_ ^= zobPiece[p][s];
        if (acc) nnue::subFeature(*net_, *acc, p, s);
    };

    if (ep_ != SQ_NONE) key_ ^= zobEp[fileOf(ep_)];
    key_ ^= zobCastle[castling_];
    ep_ = SQ_NONE;
    ++halfmove_;

    Piece cap = NO_PIECE;
    if (fl == F_EP) {
        const Square capSq = to + (us == WHITE ? -8 : 8);
        cap = makePiece(them, PAWN);
        rem(cap, capSq);
        halfmove_ = 0;
    } else if (isCapture(m)) {
        cap = board_[to];
        rem(cap, to);
        halfmove_ = 0;
    }
    prev.captured = cap;
    states_.push_back(prev);

    if (isCastle(m)) {
        const bool kingside = fl == F_KING_CASTLE;
        const Square rf = kingside ? to + 1 : to - 2;
        const Square rt = kingside ? to - 1 : to + 1;
        const Piece rook = makePiece(us, ROOK);
        rem(pc, from); put(pc, to);
        rem(rook, rf); put(rook, rt);
    } else if (isPromotion(m)) {
        rem(pc, from);
        put(makePiece(us, promoType(m)), to);
    } else {
        rem(pc, from);
        put(pc, to);
    }

    if (typeOf(pc) == PAWN) {
        halfmove_ = 0;
        if (fl == F_DOUBLE_PUSH) {
            ep_ = from + (us == WHITE ? 8 : -8);
            key_ ^= zobEp[fileOf(ep_)];
        }
    }
    castling_ &= castleMask[from] & castleMask[to];
    key_ ^= zobCastle[castling_];
    if (us == BLACK) ++fullmove_;
    stm_ = them;
    key_ ^= zobSide;
    updateCheckInfo();
}

void Position::unmakeMove() {
    const StateInfo s = states_.back();
    states_.pop_back();
    if (net_) accs_.pop_back();
    stm_ = ~stm_;
    const Color us = stm_;
    const Move m = s.move;
    const Square from = fromSq(m), to = toSq(m);
    const int fl = flagOf(m);
    if (us == BLACK) --fullmove_;

    if (isCastle(m)) {
        const bool kingside = fl == F_KING_CASTLE;
        const Square rf = kingside ? to + 1 : to - 2;
        const Square rt = kingside ? to - 1 : to + 1;
        const Piece king = makePiece(us, KING), rook = makePiece(us, ROOK);
        removeRaw(king, to); putRaw(king, from);
        removeRaw(rook, rt); putRaw(rook, rf);
    } else if (isPromotion(m)) {
        removeRaw(board_[to], to);
        putRaw(makePiece(us, PAWN), from);
        if (isCapture(m)) putRaw(s.captured, to);
    } else {
        const Piece pc = board_[to];
        removeRaw(pc, to);
        putRaw(pc, from);
        if (fl == F_EP) putRaw(s.captured, to + (us == WHITE ? -8 : 8));
        else if (isCapture(m)) putRaw(s.captured, to);
    }
    key_ = s.key;
    checkers_ = s.checkers;
    pinned_ = s.pinned;
    castling_ = s.castling;
    ep_ = s.ep;
    halfmove_ = s.halfmove;
}

void Position::makeNull() {
    states_.push_back(StateInfo{key_, checkers_, pinned_, castling_, ep_, halfmove_, NO_PIECE, MOVE_NONE});
    if (net_) accs_.push_back(accs_.back());
    if (ep_ != SQ_NONE) { key_ ^= zobEp[fileOf(ep_)]; ep_ = SQ_NONE; }
    key_ ^= zobSide;
    stm_ = ~stm_;
    ++halfmove_;
    updateCheckInfo();
}

void Position::unmakeNull() {
    const StateInfo s = states_.back();
    states_.pop_back();
    if (net_) accs_.pop_back();
    stm_ = ~stm_;
    key_ = s.key;
    checkers_ = s.checkers;
    pinned_ = s.pinned;
    castling_ = s.castling;
    ep_ = s.ep;
    halfmove_ = s.halfmove;
}

// ---------------------------------------------------------------------------------------
// Legality for pseudo-legal moves (uses checkers/pinned computed at make time)
// ---------------------------------------------------------------------------------------

bool Position::legal(Move m) const {
    const Color us = stm_, them = ~us;
    const Square from = fromSq(m), to = toSq(m);
    const Square ksq = kingSq(us);

    if (from == ksq) {
        if (isCastle(m)) return true;  // generator already verified the king path
        return !attackedBy(to, them, occupied() ^ bit(from));
    }
    if (isEnPassant(m)) {
        const Square capSq = to + (us == WHITE ? -8 : 8);
        const Bitboard occ = (occupied() ^ bit(from) ^ bit(capSq)) | bit(to);
        return !(attackersTo(ksq, occ) & byColor_[them] & ~bit(capSq));
    }
    if (checkers_) {
        if (moreThanOne(checkers_)) return false;
        const Square c = lsb(checkers_);
        if (!((BetweenBB[ksq][c] | bit(c)) & bit(to))) return false;
    }
    if (pinned_ & bit(from)) return aligned(from, ksq, to);
    return true;
}

Move Position::fromUci(const std::string& s) const {
    MoveList ml;
    generate(*this, ml, GEN_ALL);
    for (int i = 0; i < ml.size; ++i)
        if (moveToUci(ml.moves[i]) == s && legal(ml.moves[i])) return ml.moves[i];
    return MOVE_NONE;
}

// ---------------------------------------------------------------------------------------
// Static Exchange Evaluation (swap algorithm with x-ray discovery)
// ---------------------------------------------------------------------------------------

bool Position::seeGe(Move m, int threshold) const {
    // Castling, en passant and promotions are treated as neutral exchanges.
    if (isCastle(m) || isEnPassant(m) || isPromotion(m)) return 0 >= threshold;

    const Square from = fromSq(m), to = toSq(m);
    const Piece moving = board_[from];
    const Piece target = board_[to];

    int swap = (target == NO_PIECE ? 0 : SEE_VALUE[typeOf(target)]) - threshold;
    if (swap < 0) return false;
    swap = SEE_VALUE[typeOf(moving)] - swap;
    if (swap <= 0) return true;

    Bitboard occ = occupied() ^ bit(from) ^ bit(to);
    Color side = colorOf(moving);
    Bitboard attackers = attackersTo(to, occ);
    const Bitboard diagSliders = byType_[BISHOP] | byType_[QUEEN];
    const Bitboard lineSliders = byType_[ROOK] | byType_[QUEEN];
    int res = 1;

    while (true) {
        side = ~side;
        attackers &= occ;
        const Bitboard sideAttackers = attackers & byColor_[side];
        if (!sideAttackers) break;
        res ^= 1;

        Bitboard bb;
        if ((bb = sideAttackers & byType_[PAWN])) {
            if ((swap = SEE_VALUE[PAWN] - swap) < res) break;
            occ ^= bit(lsb(bb));
            attackers |= bishopAttacks(to, occ) & diagSliders;
        } else if ((bb = sideAttackers & byType_[KNIGHT])) {
            if ((swap = SEE_VALUE[KNIGHT] - swap) < res) break;
            occ ^= bit(lsb(bb));
        } else if ((bb = sideAttackers & byType_[BISHOP])) {
            if ((swap = SEE_VALUE[BISHOP] - swap) < res) break;
            occ ^= bit(lsb(bb));
            attackers |= bishopAttacks(to, occ) & diagSliders;
        } else if ((bb = sideAttackers & byType_[ROOK])) {
            if ((swap = SEE_VALUE[ROOK] - swap) < res) break;
            occ ^= bit(lsb(bb));
            attackers |= rookAttacks(to, occ) & lineSliders;
        } else if ((bb = sideAttackers & byType_[QUEEN])) {
            if ((swap = SEE_VALUE[QUEEN] - swap) < res) break;
            occ ^= bit(lsb(bb));
            attackers |= (bishopAttacks(to, occ) & diagSliders) | (rookAttacks(to, occ) & lineSliders);
        } else {
            // King recaptures only if the other side has no attacker left.
            return (attackers & ~byColor_[side]) ? bool(res ^ 1) : bool(res);
        }
    }
    return bool(res);
}

// ---------------------------------------------------------------------------------------
// Draws
// ---------------------------------------------------------------------------------------

bool Position::insufficientMaterial() const {
    if (byType_[PAWN] | byType_[ROOK] | byType_[QUEEN]) return false;
    return popcount(byType_[KNIGHT] | byType_[BISHOP]) <= 1;
}

bool Position::isDraw(int ply) const {
    if (halfmove_ >= 100) return true;
    if (insufficientMaterial()) return true;
    const int n = int(states_.size());
    int seen = 0;
    for (int d = 2; d <= halfmove_ && d <= n; d += 2) {
        if (states_[n - d].key == key_) {
            // Repeated inside the search tree: one repetition is enough.
            // Repeated before the root: require a threefold overall.
            if (d <= ply || ++seen >= 2) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------
// NNUE hooks
// ---------------------------------------------------------------------------------------

void Position::attachNetwork(const nnue::Network* net) {
    net_ = net;
    accs_.clear();
    if (net_) refreshAccumulator();
}

void Position::refreshAccumulator() {
    if (!net_) return;
    if (accs_.empty()) accs_.emplace_back();
    nnue::refresh(*net_, *this, accs_.back());
}

}  // namespace astra
