// Astra Chess - search
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

#include "movegen.h"
#include "position.h"

namespace astra {

struct Limits {
    int depth = MAX_PLY - 2;
    long long movetime = -1;
    long long wtime = -1, btime = -1, winc = 0, binc = 0;
    int movestogo = 0;
    std::uint64_t nodes = 0;  // 0 = unlimited
    bool infinite = false;
};

struct SearchResult {
    Move best = MOVE_NONE;
    int score = 0;
    int depth = 0;
    std::uint64_t nodes = 0;
};

class Searcher {
public:
    Searcher();
    void setHashMb(std::size_t mb);
    void clear();  // new game: clears TT, history, killers
    void stop() { stop_.store(true, std::memory_order_relaxed); }

    // Blocking search. `pos` is taken by value-reference and is restored on return.
    SearchResult think(Position& pos, const Limits& limits, bool printInfo = true);

private:
    enum Bound : std::uint8_t { BOUND_NONE, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT };
    struct TTEntry {
        Key key = 0;
        std::int16_t score = 0;
        std::int16_t eval = 0;
        Move move = MOVE_NONE;
        std::int8_t depth = 0;
        std::uint8_t bound = BOUND_NONE;
    };

    int search(Position& pos, int depth, int alpha, int beta, int ply, bool allowNull);
    int qsearch(Position& pos, int alpha, int beta, int ply);
    void checkLimits();
    int scoreMove(const Position& pos, Move m, Move ttMove, int ply) const;
    void updateQuietStats(const Position& pos, Move best, const Move* quiets, int nQuiets, int depth, int ply);
    void printInfo(const Position& pos, int depth, int score, long long elapsedMs) const;

    TTEntry* probe(Key key);
    int hashfull() const;

    std::vector<TTEntry> tt_;
    std::size_t ttMask_ = 0;

    std::atomic<bool> stop_{false};
    std::uint64_t nodes_ = 0;
    int rootDepth_ = 0;
    int selDepth_ = 0;
    Move rootBest_ = MOVE_NONE;

    std::chrono::steady_clock::time_point start_;
    long long softMs_ = -1, hardMs_ = -1;
    std::uint64_t nodeLimit_ = 0;
    bool verbose_ = true;

    Move pv_[MAX_PLY + 1][MAX_PLY + 1];
    int pvLen_[MAX_PLY + 1];
    Move killers_[MAX_PLY + 2][2];
    int history_[2][64][64];
    int evalStack_[MAX_PLY + 2];
    int lmrTable_[64][64];
};

}  // namespace astra
