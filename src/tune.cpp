// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#include "tune.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "eval.h"
#include "movegen.h"
#include "search.h"

namespace astra {

namespace {
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 1) {}
    std::uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ULL; }
    std::uint64_t below(std::uint64_t n) { return next() % n; }
};
}  // namespace

// ---------------------------------------------------------------------------------------
// Data generation
// ---------------------------------------------------------------------------------------
int runDatagen(int games, int depth, std::uint64_t seed, const std::string& outFile) {
    std::ofstream out(outFile, std::ios::app);
    if (!out) { std::cerr << "cannot open " << outFile << "\n"; return 1; }
    Searcher searcher;
    searcher.setHashMb(16);
    Rng rng(seed);
    long long positions = 0;
    int finished = 0;
    std::cout << "datagen: " << games << " games at depth " << depth << " -> " << outFile << std::endl;

    for (int g = 0; g < games; ++g) {
        Position pos;
        pos.setFen(Position::startFen());
        searcher.clear();
        // Random opening for diversity
        bool ok = true;
        for (int i = 0; i < 8 && ok; ++i) {
            MoveList ml;
            const int n = generateLegal(pos, ml);
            if (n == 0) { ok = false; break; }
            pos.makeMove(ml.moves[rng.below(std::uint64_t(n))]);
        }
        if (!ok) continue;
        {
            Limits lim; lim.depth = 6;
            const SearchResult r = searcher.think(pos, lim, false);
            if (r.best == MOVE_NONE || std::abs(r.score) > 300) { --g; continue; }  // unbalanced opening: redo
        }

        std::vector<std::pair<std::string, int>> fens;  // FEN + search score (White's view, cp)
        double result = 0.5;
        int streak = 0, streakSign = 0, flat = 0;
        bool decided = false;
        for (int ply = 0; ply < 400 && !decided; ++ply) {
            MoveList ml;
            if (generateLegal(pos, ml) == 0) { result = pos.inCheck() ? (pos.sideToMove() == WHITE ? 0.0 : 1.0) : 0.5; decided = true; break; }
            if (pos.isDraw(0)) { result = 0.5; decided = true; break; }
            Limits lim; lim.depth = depth;
            const SearchResult r = searcher.think(pos, lim, false);
            const int white = pos.sideToMove() == WHITE ? r.score : -r.score;
            if (!pos.inCheck() && !isTactical(r.best) && std::abs(r.score) < 2000)
                fens.push_back({pos.fen(), white});
            // Adjudication
            const int sign = white >= 1000 ? 1 : (white <= -1000 ? -1 : 0);
            streak = (sign != 0 && sign == streakSign) ? streak + 1 : (sign != 0 ? 1 : 0);
            streakSign = sign;
            if (streak >= 6) { result = sign > 0 ? 1.0 : 0.0; decided = true; break; }
            flat = std::abs(white) <= 8 ? flat + 1 : 0;
            if (flat >= 24 && ply > 80) { result = 0.5; decided = true; break; }
            pos.makeMove(r.best);
        }
        for (const auto& f : fens) { out << f.first << ';' << result << ';' << f.second << '\n'; ++positions; }
        ++finished;
        if (finished % 50 == 0) { out.flush(); std::cout << "  " << finished << " games, " << positions << " positions" << std::endl; }
    }
    std::cout << "done: " << finished << " games, " << positions << " positions" << std::endl;
    return 0;
}

// ---------------------------------------------------------------------------------------
// Texel tuning
// ---------------------------------------------------------------------------------------
bool loadParamFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string name; int v;
    while (in >> name >> v)
        for (int i = 0; i < eval::paramCount(); ++i)
            if (name == eval::paramName(i)) eval::param(i) = v;
    eval::rebuildTables();
    return true;
}

namespace {

struct Sample { std::string fen; float result; };

double sigmoid(double e, double k) { return 1.0 / (1.0 + std::pow(10.0, -k * e / 400.0)); }

double meanError(const std::vector<Sample>& data, std::size_t from, std::size_t to, double k, Position& pos) {
    double sum = 0;
    for (std::size_t i = from; i < to; ++i) {
        pos.setFen(data[i].fen);
        int e = eval::evaluateClassical(pos);
        if (pos.sideToMove() == BLACK) e = -e;
        const double d = data[i].result - sigmoid(e, k);
        sum += d * d;
    }
    return sum / double(to - from);
}

void saveParams(const std::string& path) {
    std::ofstream out(path);
    for (int i = 0; i < eval::paramCount(); ++i) out << eval::paramName(i) << ' ' << eval::param(i) << '\n';
}

}  // namespace

int runTune(const std::string& dataFile, int maxIter, const std::string& paramFile) {
    std::ifstream in(dataFile);
    if (!in) { std::cerr << "cannot open " << dataFile << "\n"; return 1; }
    std::vector<Sample> data;
    std::string line;
    while (std::getline(in, line)) {
        const auto sep = line.find(';');
        if (sep == std::string::npos) continue;
        data.push_back({line.substr(0, sep), std::stof(line.substr(sep + 1))});
    }
    if (data.size() < 1000) { std::cerr << "need at least 1000 positions (got " << data.size() << ")\n"; return 1; }
    Rng rng(12345);
    for (std::size_t i = data.size() - 1; i > 0; --i) std::swap(data[i], data[rng.below(i + 1)]);
    const std::size_t split = data.size() * 9 / 10;
    std::cout << "loaded " << data.size() << " positions (" << split << " train / " << data.size() - split << " test)\n";

    if (loadParamFile(paramFile)) std::cout << "resumed parameters from " << paramFile << "\n";
    Position pos;

    // Fit the sigmoid scale K on the training set (golden-section search)
    double lo = 0.3, hi = 3.0;
    const double gr = 0.6180339887;
    for (int i = 0; i < 18; ++i) {
        const double a = hi - gr * (hi - lo), b = lo + gr * (hi - lo);
        if (meanError(data, 0, split, a, pos) < meanError(data, 0, split, b, pos)) hi = b; else lo = a;
    }
    const double K = (lo + hi) / 2;
    double best = meanError(data, 0, split, K, pos);
    std::cout << "K = " << K << "  train error " << best << "  test error " << meanError(data, split, data.size(), K, pos) << std::endl;

    for (int iter = 1; iter <= maxIter; ++iter) {
        int changed = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < eval::paramCount(); ++i) {
            for (int dir : {+1, -1}) {
                bool moved = false;
                while (true) {
                    const int old = eval::param(i);
                    const int next = old + dir * eval::paramStep(i);
                    if (std::string(eval::paramName(i)) == "KS_DIV" && next < 1) break;
                    eval::param(i) = next;
                    eval::rebuildTables();
                    const double e = meanError(data, 0, split, K, pos);
                    if (e < best - 1e-9) { best = e; moved = true; ++changed; }
                    else { eval::param(i) = old; eval::rebuildTables(); break; }
                }
                if (moved) break;  // improved in this direction; skip the other
            }
        }
        saveParams(paramFile);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "iteration " << iter << ": " << changed << " parameters changed, train error " << best
                  << ", test error " << meanError(data, split, data.size(), K, pos) << "  (" << secs << " s)" << std::endl;
        if (changed == 0) break;
    }
    std::cout << "\nfinal parameters (also in " << paramFile << "):\n";
    for (int i = 0; i < eval::paramCount(); ++i) std::cout << "  " << eval::paramName(i) << " = " << eval::param(i) << "\n";
    return 0;
}

}  // namespace astra
