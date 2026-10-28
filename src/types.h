// Astra Chess - core types
#pragma once
#include <cstdint>
#include <string>

namespace astra {

using Bitboard = std::uint64_t;
using Key = std::uint64_t;
using Square = int;  // 0 = a1, 7 = h1, 56 = a8, 63 = h8
using Move = std::uint16_t;

constexpr Square SQ_NONE = 64;
constexpr Move MOVE_NONE = 0;
constexpr int MAX_PLY = 128;
constexpr int INF = 32000;
constexpr int MATE = 31000;
constexpr int MATE_IN_MAX = MATE - MAX_PLY;
constexpr int VALUE_NONE = 32001;

enum Color : int { WHITE, BLACK, COLOR_NB };
enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_TYPE_NB, NO_PIECE_TYPE = 6 };
enum Piece : int {
    W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    NO_PIECE = 12, PIECE_NB = 12
};

constexpr Color operator~(Color c) { return Color(c ^ 1); }
constexpr Piece makePiece(Color c, PieceType t) { return Piece(c * 6 + t); }
constexpr PieceType typeOf(Piece p) { return PieceType(p % 6); }
constexpr Color colorOf(Piece p) { return Color(p / 6); }
constexpr int fileOf(Square s) { return s & 7; }
constexpr int rankOf(Square s) { return s >> 3; }
constexpr Square makeSquare(int file, int rank) { return rank * 8 + file; }

// Castling rights bits
enum : int { WHITE_OO = 1, WHITE_OOO = 2, BLACK_OO = 4, BLACK_OOO = 8 };

// Move encoding: bits 0-5 from, 6-11 to, 12-15 flag.
enum MoveFlag : int {
    F_QUIET = 0, F_DOUBLE_PUSH = 1, F_KING_CASTLE = 2, F_QUEEN_CASTLE = 3,
    F_CAPTURE = 4, F_EP = 5,
    F_PROMO_N = 8, F_PROMO_B = 9, F_PROMO_R = 10, F_PROMO_Q = 11,
    F_PROMO_CAP_N = 12, F_PROMO_CAP_B = 13, F_PROMO_CAP_R = 14, F_PROMO_CAP_Q = 15
};

constexpr Move makeMove(Square from, Square to, int flag = F_QUIET) {
    return Move(from | (to << 6) | (flag << 12));
}
constexpr Square fromSq(Move m) { return m & 63; }
constexpr Square toSq(Move m) { return (m >> 6) & 63; }
constexpr int flagOf(Move m) { return m >> 12; }
constexpr bool isCapture(Move m) { return flagOf(m) & 4; }
constexpr bool isPromotion(Move m) { return flagOf(m) & 8; }
constexpr bool isEnPassant(Move m) { return flagOf(m) == F_EP; }
constexpr bool isCastle(Move m) { return flagOf(m) == F_KING_CASTLE || flagOf(m) == F_QUEEN_CASTLE; }
constexpr PieceType promoType(Move m) { return PieceType((flagOf(m) & 3) + KNIGHT); }
// "Tactical" = capture or promotion (used for ordering and pruning decisions)
constexpr bool isTactical(Move m) { return flagOf(m) & 12; }

inline std::string squareName(Square s) {
    return std::string{char('a' + fileOf(s)), char('1' + rankOf(s))};
}

inline std::string moveToUci(Move m) {
    if (m == MOVE_NONE) return "0000";
    std::string s = squareName(fromSq(m)) + squareName(toSq(m));
    if (isPromotion(m)) s += "nbrq"[promoType(m) - KNIGHT];
    return s;
}

}  // namespace astra
