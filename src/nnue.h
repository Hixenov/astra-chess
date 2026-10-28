// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
// Astra Chess - NNUE architecture (inference side)
//
// Architecture "PS768 -> H -> 1" (single hidden layer, perspective network):
//   * 768 input features per perspective: (own/enemy) x (6 piece types) x 64 squares,
//     squares are mirrored vertically for the black perspective.
//   * Hidden layer of H neurons (H <= MAX_HIDDEN), one accumulator per perspective,
//     updated incrementally on make/unmake (add/sub of feature columns).
//   * Output: CReLU on [accumulator(stm) | accumulator(not stm)] and a dot product with
//     2*H output weights.
//
// Quantisation: feature weights/biases are int16 scaled by QA, output weights are int16
// scaled by QB, output bias is int32 scaled by QA*QB.
//   eval_cp = (sum + outputBias) * SCALE / (QA * QB)
//
// File format (little endian), "ASTN" v1:
//   char[4] magic = "ASTN"; u32 version = 1; u32 featureSet = 1 (PS768); u32 hidden;
//   i16 featureWeights[768][hidden]; i16 featureBias[hidden];
//   i16 outputWeights[2*hidden]; i32 outputBias;
//
// NOTE: no *trained* network ships with Astra Chess 0.5. This module provides the
// inference code, file format, incremental update machinery and tests so that a trained
// net can be dropped in (see docs/NNUE.md).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "types.h"

namespace astra {
class Position;

namespace nnue {

constexpr int INPUTS = 768;
constexpr int MAX_HIDDEN = 256;
constexpr int QA = 255;
constexpr int QB = 64;
constexpr int SCALE = 400;
constexpr std::uint32_t FORMAT_VERSION = 1;
constexpr std::uint32_t FEATURE_SET_PS768 = 1;

struct Accumulator {
    alignas(32) std::int16_t v[2][MAX_HIDDEN];
};

struct Network {
    int hidden = 0;
    std::vector<std::int16_t> featureWeights;  // [INPUTS][hidden]
    std::vector<std::int16_t> featureBias;     // [hidden]
    std::vector<std::int16_t> outputWeights;   // [2 * hidden]
    std::int32_t outputBias = 0;
};

inline int featureIndex(Color perspective, Piece pc, Square sq) {
    const int rel = colorOf(pc) == perspective ? 0 : 1;
    const int s = perspective == WHITE ? sq : (sq ^ 56);
    return (rel * 6 + typeOf(pc)) * 64 + s;
}

void addFeature(const Network& net, Accumulator& acc, Piece pc, Square sq);
void subFeature(const Network& net, Accumulator& acc, Piece pc, Square sq);
void refresh(const Network& net, const Position& pos, Accumulator& acc);
int evaluate(const Network& net, const Accumulator& acc, Color stm);

bool load(const std::string& path, Network& out, std::string& err);
// Writes a deterministic pseudo-random network (for tests / plumbing checks only).
bool writeRandom(const std::string& path, int hidden, std::uint64_t seed, std::string& err);

}  // namespace nnue
}  // namespace astra
