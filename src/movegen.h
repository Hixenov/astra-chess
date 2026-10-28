// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
// Astra Chess - pseudo-legal move generation on bitboards
#pragma once
#include "position.h"

namespace astra {

struct MoveList {
    Move moves[256];
    int size = 0;
    void add(Move m) { moves[size++] = m; }
};

enum GenType { GEN_ALL, GEN_CAPTURES };  // GEN_CAPTURES: captures, en passant, promotions (queen-only if quiet)

// Pseudo-legal generation. Castling moves are fully validated (rights, empty squares,
// no attacked squares). Everything else must be filtered with Position::legal().
void generate(const Position& pos, MoveList& ml, GenType gt);

// Fills `ml` with strictly legal moves; returns the count.
int generateLegal(const Position& pos, MoveList& ml);

std::uint64_t perft(Position& pos, int depth);

}  // namespace astra
