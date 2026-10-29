// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#include "train.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

#include "eval.h"
#include "nnue.h"
#include "position.h"

namespace astra {

namespace {

constexpr double CP_SCALE = 198.8;  // sigmoid(cp / 198.8) ~ expected score (K = 0.874 from the Texel fit)

struct Sample {
    std::uint16_t ps[32];  // piece * 64 + square
    std::uint8_t n;
    std::uint8_t stm;
    float res;    // game result from the side to move's view (1 / 0.5 / 0)
    float score;  // search score as expected score from the side to move's view (or -1 if missing)
};

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 7) {}
    std::uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ULL; }
    double uniform() { return double(next() >> 11) / double(1ULL << 53); }  // [0,1)
    double normal() {
        const double u1 = std::max(1e-12, uniform()), u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }
    std::uint64_t below(std::uint64_t n) { return next() % n; }
};

int fi[2][768];  // feature index per perspective for (piece*64+square)

struct Layout {
    int H;
    std::size_t w1, b1, w2, b2, total;
    explicit Layout(int hidden) : H(hidden) {
        w1 = 0;
        b1 = std::size_t(768) * H;
        w2 = b1 + H;
        b2 = w2 + 2 * std::size_t(H);
        total = b2 + 1;
    }
};

inline float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

// Forward pass: returns the network output o (eval_cp = o * SCALE). au/at receive pre-activations.
inline float forward(const float* P, const Layout& L, const Sample& s, float* au, float* at) {
    const int H = L.H;
    const float* b1 = P + L.b1;
    for (int i = 0; i < H; ++i) au[i] = at[i] = b1[i];
    const int us = s.stm, th = us ^ 1;
    for (int k = 0; k < s.n; ++k) {
        const float* wu = P + std::size_t(fi[us][s.ps[k]]) * H;
        const float* wt = P + std::size_t(fi[th][s.ps[k]]) * H;
        for (int i = 0; i < H; ++i) { au[i] += wu[i]; at[i] += wt[i]; }
    }
    const float* w2 = P + L.w2;
    float o = P[L.b2];
    for (int i = 0; i < H; ++i) {
        o += std::min(std::max(au[i], 0.0f), 1.0f) * w2[i];
        o += std::min(std::max(at[i], 0.0f), 1.0f) * w2[H + i];
    }
    return o;
}

void trainRange(const std::vector<Sample>& data, const std::vector<std::uint32_t>& order, std::size_t from, std::size_t to,
                const float* P, const Layout& L, float lambda, float* grad, double* lossOut) {
    const int H = L.H;
    float au[nnue::MAX_HIDDEN], at[nnue::MAX_HIDDEN], du[nnue::MAX_HIDDEN], dt[nnue::MAX_HIDDEN];
    const float* w2 = P + L.w2;
    double loss = 0;
    for (std::size_t idx = from; idx < to; ++idx) {
        const Sample& s = data[order[idx]];
        const float o = forward(P, L, s, au, at);
        const float p = sigmoid(2.0f * o);
        const float target = (s.score >= 0.0f) ? lambda * s.score + (1.0f - lambda) * s.res : s.res;
        const float err = p - target;
        loss += double(err) * err;
        const float g = 4.0f * err * p * (1.0f - p);
        grad[L.b2] += g;
        for (int i = 0; i < H; ++i) {
            const float hu = std::min(std::max(au[i], 0.0f), 1.0f), ht = std::min(std::max(at[i], 0.0f), 1.0f);
            grad[L.w2 + i] += g * hu;
            grad[L.w2 + H + i] += g * ht;
            du[i] = (au[i] > 0.0f && au[i] < 1.0f) ? g * w2[i] : 0.0f;
            dt[i] = (at[i] > 0.0f && at[i] < 1.0f) ? g * w2[H + i] : 0.0f;
            grad[L.b1 + i] += du[i] + dt[i];
        }
        const int us = s.stm, th = us ^ 1;
        for (int k = 0; k < s.n; ++k) {
            float* gu = grad + std::size_t(fi[us][s.ps[k]]) * H;
            float* gt = grad + std::size_t(fi[th][s.ps[k]]) * H;
            for (int i = 0; i < H; ++i) { gu[i] += du[i]; gt[i] += dt[i]; }
        }
    }
    *lossOut = loss;
}

// Writes the quantised network in ASTN v1 format (see nnue.h).
bool exportNet(const std::vector<float>& P, const Layout& L, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    auto w32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto q16 = [&](float v, float scale) {
        const long r = std::lround(double(v) * scale);
        const std::int16_t q = std::int16_t(std::max(-32767L, std::min(32767L, r)));
        out.write(reinterpret_cast<const char*>(&q), 2);
    };
    out.write("ASTN", 4);
    w32(nnue::FORMAT_VERSION);
    w32(nnue::FEATURE_SET_PS768);
    w32(std::uint32_t(L.H));
    for (std::size_t i = 0; i < std::size_t(768) * L.H; ++i) q16(P[L.w1 + i], nnue::QA);
    for (int i = 0; i < L.H; ++i) q16(P[L.b1 + i], nnue::QA);
    for (int i = 0; i < 2 * L.H; ++i) q16(P[L.w2 + i], nnue::QB);
    const std::int32_t ob = std::int32_t(std::lround(double(P[L.b2]) * nnue::QA * nnue::QB));
    out.write(reinterpret_cast<const char*>(&ob), 4);
    return bool(out);
}

}  // namespace

int runTrain(const std::string& dataFile, const std::string& outFile, int hidden, int epochs, double lambda,
             int threads, double lr) {
    if (hidden < 1 || hidden > nnue::MAX_HIDDEN) { std::cerr << "hidden must be in 1.." << nnue::MAX_HIDDEN << "\n"; return 2; }
    Position::init();
    eval::init();
    for (int persp = 0; persp < 2; ++persp)
        for (int ps = 0; ps < 768; ++ps) fi[persp][ps] = nnue::featureIndex(Color(persp), Piece(ps / 64), ps % 64);
    if (threads < 1) threads = std::max(1u, std::thread::hardware_concurrency());

    // ---- load data ----
    std::ifstream in(dataFile);
    if (!in) { std::cerr << "cannot open " << dataFile << "\n"; return 1; }
    std::vector<Sample> train, val;
    std::vector<std::string> valFens;
    std::string line;
    // Validation = the LAST 5% of the file. Games are written contiguously, so this holds out whole games;
    // sampling every n-th line would leak neighbouring positions of the same game (same result) into training.
    std::size_t totalLines = 0;
    while (std::getline(in, line)) ++totalLines;
    in.clear();
    in.seekg(0);
    const std::size_t valStart = totalLines - totalLines / 20;
    Position pos;
    std::size_t lineNo = 0, skipped = 0;
    bool anyScore = false;
    while (std::getline(in, line)) {
        const auto a = line.find(';');
        if (a == std::string::npos) continue;
        const auto b = line.find(';', a + 1);
        const std::string fen = line.substr(0, a);
        const double res = std::atof(line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1).c_str());
        if (!pos.setFen(fen)) { ++skipped; continue; }
        Sample s;
        s.n = 0;
        Bitboard occ = pos.occupied();
        while (occ) {
            const Square sq = popLsb(occ);
            s.ps[s.n++] = std::uint16_t(int(pos.pieceOn(sq)) * 64 + sq);
        }
        s.stm = std::uint8_t(pos.sideToMove());
        s.res = float(pos.sideToMove() == WHITE ? res : 1.0 - res);
        s.score = -1.0f;
        if (b != std::string::npos) {
            const double cpWhite = std::atof(line.substr(b + 1).c_str());
            const double cpStm = pos.sideToMove() == WHITE ? cpWhite : -cpWhite;
            s.score = float(1.0 / (1.0 + std::exp(-cpStm / CP_SCALE)));
            anyScore = true;
        }
        if (lineNo++ >= valStart) { val.push_back(s); valFens.push_back(fen); }
        else train.push_back(s);
    }
    if (train.size() < 1000) { std::cerr << "need at least ~1000 positions\n"; return 1; }
    if (!anyScore && lambda > 0) { std::cout << "note: data has no score column, using game results only\n"; lambda = 0; }
    std::cout << "loaded " << train.size() << " train / " << val.size() << " validation positions"
              << (skipped ? " (" + std::to_string(skipped) + " invalid skipped)" : "") << ", hidden=" << hidden
              << ", threads=" << threads << ", lambda=" << lambda << std::endl;

    // ---- classical baseline on the validation set (same labels) ----
    {
        double err = 0;
        for (std::size_t i = 0; i < val.size(); ++i) {
            pos.setFen(valFens[i]);
            const double p = 1.0 / (1.0 + std::exp(-double(eval::evaluateClassical(pos)) / CP_SCALE));
            err += (p - val[i].res) * (p - val[i].res);
        }
        std::cout << "classical evaluation: validation error vs game result = " << err / val.size() << "  (baseline to beat)" << std::endl;
    }

    // ---- init ----
    const Layout L(hidden);
    Rng rng(2024);
    std::vector<float> P(L.total, 0.0f), best;
    for (std::size_t i = 0; i < std::size_t(768) * hidden; ++i) P[L.w1 + i] = float(rng.normal() * 0.08);
    for (int i = 0; i < hidden; ++i) P[L.b1 + i] = 0.05f;
    for (int i = 0; i < 2 * hidden; ++i) P[L.w2 + i] = float(rng.normal() * 0.1);
    std::vector<float> m(L.total, 0.0f), v(L.total, 0.0f);
    std::vector<std::vector<float>> grads(threads, std::vector<float>(L.total));
    std::vector<double> losses(threads);
    std::vector<std::uint32_t> order(train.size());
    std::iota(order.begin(), order.end(), 0u);
    std::vector<std::uint32_t> valOrder(val.size());
    std::iota(valOrder.begin(), valOrder.end(), 0u);

    const std::size_t batch = 16384;
    long step = 0;
    double bestVal = 1e9;
    for (int epoch = 1; epoch <= epochs; ++epoch) {
        const auto t0 = std::chrono::steady_clock::now();
        const double lrNow = lr * (0.03 + 0.97 * 0.5 * (1.0 + std::cos(3.14159265358979 * (epoch - 1) / std::max(1, epochs))));
        for (std::size_t i = train.size() - 1; i > 0; --i) std::swap(order[i], order[rng.below(i + 1)]);
        double trainLoss = 0;
        for (std::size_t start = 0; start < train.size(); start += batch) {
            const std::size_t end = std::min(train.size(), start + batch);
            const std::size_t per = (end - start + threads - 1) / threads;
            std::vector<std::thread> pool;
            for (int t = 0; t < threads; ++t) {
                const std::size_t a = std::min(end, start + t * per), b = std::min(end, a + per);
                std::fill(grads[t].begin(), grads[t].end(), 0.0f);
                losses[t] = 0;
                if (a >= b) continue;
                pool.emplace_back(trainRange, std::cref(train), std::cref(order), a, b, P.data(), std::cref(L),
                                  float(lambda), grads[t].data(), &losses[t]);
            }
            for (auto& th : pool) th.join();
            ++step;
            const float inv = 1.0f / float(end - start);
            const double c1 = 1.0 - std::pow(0.9, double(step)), c2 = 1.0 - std::pow(0.999, double(step));
            for (std::size_t j = 0; j < L.total; ++j) {
                float g = 0;
                for (int t = 0; t < threads; ++t) g += grads[t][j];
                g *= inv;
                m[j] = 0.9f * m[j] + 0.1f * g;
                v[j] = 0.999f * v[j] + 0.001f * g * g;
                P[j] -= float(lrNow * (m[j] / c1) / (std::sqrt(v[j] / c2) + 1e-8));
            }
            // keep weights in a range where the int16 accumulators cannot overflow
            for (std::size_t j = 0; j < L.w2; ++j) P[j] = std::min(1.9f, std::max(-1.9f, P[j]));
            for (std::size_t j = L.w2; j < L.b2; ++j) P[j] = std::min(120.0f, std::max(-120.0f, P[j]));
            for (int t = 0; t < threads; ++t) trainLoss += losses[t];
        }
        // validation
        double vLoss = 0, vRes = 0;
        float au[nnue::MAX_HIDDEN], at[nnue::MAX_HIDDEN];
        for (const Sample& s : val) {
            const float p = sigmoid(2.0f * forward(P.data(), L, s, au, at));
            const float target = (s.score >= 0.0f) ? float(lambda) * s.score + (1.0f - float(lambda)) * s.res : s.res;
            vLoss += double(p - target) * (p - target);
            vRes += double(p - s.res) * (p - s.res);
        }
        vLoss /= val.size();
        vRes /= val.size();
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const bool improved = vLoss < bestVal;
        if (improved) { bestVal = vLoss; best = P; }
        std::cout << "epoch " << epoch << "/" << epochs << "  lr " << lrNow << "  train loss " << trainLoss / train.size()
                  << "  val loss " << vLoss << "  val error vs result " << vRes << (improved ? "  *" : "") << "  (" << secs << " s)" << std::endl;
        if (improved && !exportNet(best, L, outFile)) { std::cerr << "cannot write " << outFile << "\n"; return 1; }
    }

    // ---- cross-check: quantised network through the engine's own inference code ----
    nnue::Network net;
    std::string err;
    if (!nnue::load(outFile, net, err)) { std::cerr << "exported network failed to load: " << err << "\n"; return 1; }
    double diff = 0, maxDiff = 0;
    const std::size_t checks = std::min<std::size_t>(val.size(), 20000);
    float au[nnue::MAX_HIDDEN], at[nnue::MAX_HIDDEN];
    for (std::size_t i = 0; i < checks; ++i) {
        pos.setFen(valFens[i]);
        pos.attachNetwork(&net);
        const int q = nnue::evaluate(net, pos.accumulator(), pos.sideToMove());
        const double f = double(forward(best.data(), L, val[i], au, at)) * nnue::SCALE;
        diff += std::abs(q - f);
        maxDiff = std::max(maxDiff, std::abs(q - f));
    }
    std::cout << "\nquantisation check (engine inference vs float model, " << checks << " positions): mean |diff| "
              << diff / checks << " cp, max " << maxDiff << " cp\n";
    std::cout << "network written to " << outFile << "\n"
              << "use it:  setoption name EvalFile value " << outFile << "  then  setoption name UseNNUE value true\n";
    return 0;
}

}  // namespace astra
