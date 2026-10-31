// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
// Astra Chess - search
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "movegen.h"
#include "position.h"
#include "tt.h"

namespace astra {

struct Limits {
    int depth = MAX_PLY - 2;
    long long movetime = -1;
    long long wtime = -1, btime = -1, winc = 0, binc = 0;
    int movestogo = 0;
    std::uint64_t nodes = 0;  // 0 = unlimited
    bool infinite = false;
    int multiPv = 1;
    int moveOverheadMs = 10;
};

struct SearchResult {
    Move best = MOVE_NONE;
    int score = 0;
    int depth = 0;
    std::uint64_t nodes = 0;
};

class Searcher {
public:
    Searcher();  // owns its transposition table and may own helper threads (see setThreads)
    Searcher(TranspositionTable* sharedTt, std::atomic<bool>* sharedStop);  // helper worker
    void setHashMb(std::size_t mb);
    void setThreads(int n);  // Lazy SMP: n-1 helper threads share the transposition table
    int threads() const { return threads_; }
    void clear();  // new game: clears TT, history, killers
    void stop() { stop_.store(true, std::memory_order_relaxed); }

    // Blocking search. `pos` is taken by value-reference and is restored on return.
    SearchResult think(Position& pos, const Limits& limits, bool printInfo = true);

private:
    int search(Position& pos, int depth, int alpha, int beta, int ply, bool allowNull);
    int qsearch(Position& pos, int alpha, int beta, int ply);
    void checkLimits();
    int scoreMove(const Position& pos, Move m, Move ttMove, int ply) const;
    void updateCaptureStats(const Position& pos, Move bestCapture, const Move* caps, int nCaps, int depth);
    void updateQuietStats(const Position& pos, Move best, const Move* quiets, int nQuiets, int depth, int ply);
    struct PvLine { int score = 0; int len = 0; Move moves[MAX_PLY + 1]; };
    void printInfo(int depth, int multipvIdx, bool multi, const PvLine& line, long long elapsedMs) const;

    // Pawn-structure correction history: learns the systematic error of the static evaluation.
    static constexpr int CORR_BITS = 14;
    static constexpr int CORR_GRAIN = 256;
    static constexpr int CORR_LIMIT = 48 * CORR_GRAIN;
    static int corrIndex(const Position& pos) { return int(pos.pawnKey() >> (64 - CORR_BITS)); }
    int correction(const Position& pos) const { return corrHist_[pos.sideToMove()][corrIndex(pos)] / CORR_GRAIN; }
    void updateCorrection(const Position& pos, int depth, int diff);

    void init();
    void clearLocal();
    void helperThink(Position pos, int id, int maxDepth);
    std::uint64_t totalNodes() const;
    bool stopped() const { return stopFlag_->load(std::memory_order_relaxed); }
    void requestStop() { stopFlag_->store(true, std::memory_order_relaxed); }
    int hashfull() const { return tt_->hashfull(); }

    std::unique_ptr<TranspositionTable> ownTt_;
    TranspositionTable* tt_ = nullptr;
    std::vector<std::unique_ptr<Searcher>> helpers_;
    int threads_ = 1;

    std::atomic<bool> stop_{false};
    std::atomic<bool>* stopFlag_ = &stop_;
    std::atomic<std::uint64_t> publishedNodes_{0};
    std::uint64_t nodes_ = 0;
    int rootDepth_ = 0;
    int selDepth_ = 0;
    Move rootBest_ = MOVE_NONE;
    Move rootExcluded_[256];
    int nRootExcluded_ = 0;

    std::chrono::steady_clock::time_point start_;
    long long softMs_ = -1, hardMs_ = -1;
    std::uint64_t nodeLimit_ = 0;
    bool verbose_ = true;

    Move pv_[MAX_PLY + 1][MAX_PLY + 1];
    int pvLen_[MAX_PLY + 1];
    Move killers_[MAX_PLY + 2][2];
    int history_[2][64][64];
    Move counter_[64][64];
    int capHist_[12][64][6];       // [moving piece][to][captured type]
    Move excludedMove_[MAX_PLY + 2];  // move excluded at this ply (singular extension search)
    std::vector<std::int16_t> contHist_;  // [prevPiece][prevTo][piece][to]
    static int contIndex(Piece pp, Square pt, Piece p, Square t) { return ((int(pp) * 64 + pt) * 12 + int(p)) * 64 + t; }
    int evalStack_[MAX_PLY + 2];
    int lmrTable_[64][64];
    std::int16_t corrHist_[2][1 << CORR_BITS];
};

}  // namespace astra
