// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
// Astra Chess - evaluation (hand-crafted, with optional NNUE backend)
#pragma once
#include <string>

#include "position.h"

namespace astra {
namespace eval {

void init();

// Tunable evaluation parameters (used by the Texel tuner, `--tune`).
int paramCount();
int& param(int i);
const char* paramName(int i);
int paramStep(int i);
void rebuildTables();  // call after changing parameters

// Score in centipawns from the side-to-move's point of view.
// Uses NNUE if a network is attached to the position, otherwise the classical evaluation.
int evaluate(const Position& pos);
int evaluateClassical(const Position& pos);

// Global NNUE configuration (set from UCI options).
bool loadNetwork(const std::string& path, std::string& err);
void unloadNetwork();
const nnue::Network* activeNetwork();  // nullptr when disabled or not loaded
const nnue::Network* loadedNetwork();  // nullptr when not loaded (ignores UseNNUE)
void setUseNnue(bool on);
bool useNnue();
bool networkLoaded();

// Piece values (middlegame-ish) used by move ordering and pruning.
extern const int PieceValue[PIECE_TYPE_NB];

}  // namespace eval
}  // namespace astra
