// Astra Chess - bitboard position with incremental Zobrist hashing and NNUE accumulators
#pragma once
#include <string>
#include <vector>

#include "bitboard.h"
#include "nnue.h"
#include "types.h"

namespace astra {

struct StateInfo {
    Key key;              // key of the position BEFORE the move (restored on unmake)
    Bitboard checkers;
    Bitboard pinned;
    int castling;
    Square ep;
    int halfmove;
    Piece captured;
    Move move;
};

class Position {
public:
    static void init();  // Zobrist keys; also calls initBitboards()

    Position();
    bool setFen(const std::string& fen);
    std::string fen() const;
    static const char* startFen();

    // --- accessors ---
    Color sideToMove() const { return stm_; }
    Piece pieceOn(Square s) const { return board_[s]; }
    Bitboard occupied() const { return byColor_[WHITE] | byColor_[BLACK]; }
    Bitboard pieces(Color c) const { return byColor_[c]; }
    Bitboard pieces(PieceType t) const { return byType_[t]; }
    Bitboard pieces(Color c, PieceType t) const { return byColor_[c] & byType_[t]; }
    Bitboard pieces(PieceType a, PieceType b) const { return byType_[a] | byType_[b]; }
    Square kingSq(Color c) const { return lsb(pieces(c, KING)); }
    int castlingRights() const { return castling_; }
    Square epSquare() const { return ep_; }
    int halfmoveClock() const { return halfmove_; }
    int fullmoveNumber() const { return fullmove_; }
    Key key() const { return key_; }
    Bitboard checkers() const { return checkers_; }
    Bitboard pinned() const { return pinned_; }
    bool inCheck() const { return checkers_ != 0; }
    int gamePly() const { return int(states_.size()); }
    Move lastMove() const { return states_.empty() ? MOVE_NONE : states_.back().move; }
    int nonPawnPieces(Color c) const {
        return popcount(byColor_[c] & ~byType_[PAWN] & ~byType_[KING]);
    }

    Bitboard attackersTo(Square s, Bitboard occ) const;
    bool attackedBy(Square s, Color by, Bitboard occ) const {
        return attackersTo(s, occ) & byColor_[by];
    }

    // --- moves ---
    void makeMove(Move m);
    void unmakeMove();
    void makeNull();
    void unmakeNull();
    bool legal(Move m) const;                 // m must be pseudo-legal for this position
    Move fromUci(const std::string& s) const; // MOVE_NONE if not legal here

    // Static exchange evaluation: true if the exchange sequence starting with m
    // gains at least `threshold` centipawns for the side to move.
    bool seeGe(Move m, int threshold) const;

    bool isDraw(int ply) const;  // 50-move rule, insufficient material, repetition
    bool insufficientMaterial() const;
    Key computeKey() const;
    bool validate(std::string* why = nullptr) const;  // internal consistency check (tests)

    // --- NNUE ---
    void attachNetwork(const nnue::Network* net);  // nullptr detaches
    const nnue::Network* network() const { return net_; }
    const nnue::Accumulator& accumulator() const { return accs_.back(); }
    void refreshAccumulator();  // recompute from scratch for the current position

private:
    void putRaw(Piece p, Square s) {
        board_[s] = p;
        byType_[typeOf(p)] |= bit(s);
        byColor_[colorOf(p)] |= bit(s);
    }
    void removeRaw(Piece p, Square s) {
        board_[s] = NO_PIECE;
        byType_[typeOf(p)] &= ~bit(s);
        byColor_[colorOf(p)] &= ~bit(s);
    }
    void updateCheckInfo();

    Piece board_[64];
    Bitboard byType_[PIECE_TYPE_NB];
    Bitboard byColor_[COLOR_NB];
    Color stm_ = WHITE;
    int castling_ = 0;
    Square ep_ = SQ_NONE;
    int halfmove_ = 0;
    int fullmove_ = 1;
    Key key_ = 0;
    Bitboard checkers_ = 0;
    Bitboard pinned_ = 0;
    std::vector<StateInfo> states_;

    const nnue::Network* net_ = nullptr;
    std::vector<nnue::Accumulator> accs_;
};

}  // namespace astra
