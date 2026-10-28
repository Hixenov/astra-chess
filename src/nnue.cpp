// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#include "nnue.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "position.h"

namespace astra {
namespace nnue {

void addFeature(const Network& net, Accumulator& acc, Piece pc, Square sq) {
    const int h = net.hidden;
    for (int persp = 0; persp < 2; ++persp) {
        const std::int16_t* w = &net.featureWeights[std::size_t(featureIndex(Color(persp), pc, sq)) * h];
        std::int16_t* a = acc.v[persp];
        for (int i = 0; i < h; ++i) a[i] = std::int16_t(a[i] + w[i]);
    }
}

void subFeature(const Network& net, Accumulator& acc, Piece pc, Square sq) {
    const int h = net.hidden;
    for (int persp = 0; persp < 2; ++persp) {
        const std::int16_t* w = &net.featureWeights[std::size_t(featureIndex(Color(persp), pc, sq)) * h];
        std::int16_t* a = acc.v[persp];
        for (int i = 0; i < h; ++i) a[i] = std::int16_t(a[i] - w[i]);
    }
}

void refresh(const Network& net, const Position& pos, Accumulator& acc) {
    std::memset(&acc, 0, sizeof(acc));
    for (int persp = 0; persp < 2; ++persp)
        for (int i = 0; i < net.hidden; ++i) acc.v[persp][i] = net.featureBias[i];
    Bitboard occ = pos.occupied();
    while (occ) {
        const Square s = popLsb(occ);
        addFeature(net, acc, pos.pieceOn(s), s);
    }
}

int evaluate(const Network& net, const Accumulator& acc, Color stm) {
    const int h = net.hidden;
    const std::int16_t* us = acc.v[stm];
    const std::int16_t* them = acc.v[~stm];
    std::int64_t sum = net.outputBias;
    for (int i = 0; i < h; ++i) {
        sum += std::clamp<int>(us[i], 0, QA) * net.outputWeights[i];
        sum += std::clamp<int>(them[i], 0, QA) * net.outputWeights[h + i];
    }
    return int(sum * SCALE / (QA * QB));
}

namespace {
template <typename T>
bool readVec(std::ifstream& in, std::vector<T>& v, std::size_t n) {
    v.resize(n);
    in.read(reinterpret_cast<char*>(v.data()), std::streamsize(n * sizeof(T)));
    return bool(in);
}
template <typename T>
void writeVal(std::ofstream& out, T v) { out.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
}  // namespace

bool load(const std::string& path, Network& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "cannot open file"; return false; }
    char magic[4];
    std::uint32_t version = 0, featureSet = 0, hidden = 0;
    in.read(magic, 4);
    in.read(reinterpret_cast<char*>(&version), 4);
    in.read(reinterpret_cast<char*>(&featureSet), 4);
    in.read(reinterpret_cast<char*>(&hidden), 4);
    if (!in || std::memcmp(magic, "ASTN", 4) != 0) { err = "bad magic (not an ASTN network)"; return false; }
    if (version != FORMAT_VERSION) { err = "unsupported network version " + std::to_string(version); return false; }
    if (featureSet != FEATURE_SET_PS768) { err = "unsupported feature set"; return false; }
    if (hidden == 0 || hidden > std::uint32_t(MAX_HIDDEN)) { err = "hidden size out of range (1.." + std::to_string(MAX_HIDDEN) + ")"; return false; }

    Network n;
    n.hidden = int(hidden);
    if (!readVec(in, n.featureWeights, std::size_t(INPUTS) * hidden) || !readVec(in, n.featureBias, hidden) ||
        !readVec(in, n.outputWeights, 2 * std::size_t(hidden))) { err = "file truncated"; return false; }
    in.read(reinterpret_cast<char*>(&n.outputBias), 4);
    if (!in) { err = "file truncated"; return false; }
    in.peek();
    if (!in.eof()) { err = "trailing data after network"; return false; }
    out = std::move(n);
    return true;
}

bool writeRandom(const std::string& path, int hidden, std::uint64_t seed, std::string& err) {
    if (hidden <= 0 || hidden > MAX_HIDDEN) { err = "hidden size out of range"; return false; }
    std::ofstream out(path, std::ios::binary);
    if (!out) { err = "cannot create file"; return false; }
    std::uint64_t s = seed ? seed : 1;
    auto next = [&]() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ULL; };
    out.write("ASTN", 4);
    writeVal<std::uint32_t>(out, FORMAT_VERSION);
    writeVal<std::uint32_t>(out, FEATURE_SET_PS768);
    writeVal<std::uint32_t>(out, std::uint32_t(hidden));
    for (std::size_t i = 0; i < std::size_t(INPUTS) * hidden; ++i) writeVal<std::int16_t>(out, std::int16_t(int(next() % 41) - 20));
    for (int i = 0; i < hidden; ++i) writeVal<std::int16_t>(out, std::int16_t(int(next() % 61) - 30));
    for (int i = 0; i < 2 * hidden; ++i) writeVal<std::int16_t>(out, std::int16_t(int(next() % 41) - 20));
    writeVal<std::int32_t>(out, 0);
    return bool(out);
}

}  // namespace nnue
}  // namespace astra
