#include "search.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#include "eval.h"

namespace astra {

using Clock = std::chrono::steady_clock;

namespace {
constexpr int HISTORY_MAX = 16000;
int toTT(int score, int ply) {
    if (score >= MATE_IN_MAX) return score + ply;
    if (score <= -MATE_IN_MAX) return score - ply;
    return score;
}
int fromTT(int score, int ply) {
    if (score >= MATE_IN_MAX) return score - ply;
    if (score <= -MATE_IN_MAX) return score + ply;
    return score;
}
}  // namespace

Searcher::Searcher() {
    eval::init();
    for (int d = 1; d < 64; ++d)
        for (int m = 1; m < 64; ++m) lmrTable_[d][m] = int(0.75 + std::log(double(d)) * std::log(double(m)) / 2.25);
    for (int i = 0; i < 64; ++i) lmrTable_[0][i] = lmrTable_[i][0] = 0;
    setHashMb(64);
    clear();
}

void Searcher::setHashMb(std::size_t mb) {
    std::size_t entries = std::max<std::size_t>(1, mb * 1024 * 1024 / sizeof(TTEntry));
    std::size_t p = 1;
    while (p * 2 <= entries) p *= 2;
    tt_.assign(p, TTEntry{});
    ttMask_ = p - 1;
}

void Searcher::clear() {
    std::fill(tt_.begin(), tt_.end(), TTEntry{});
    std::memset(killers_, 0, sizeof(killers_));
    std::memset(history_, 0, sizeof(history_));
}

Searcher::TTEntry* Searcher::probe(Key key) { return &tt_[key & ttMask_]; }

int Searcher::hashfull() const {
    int used = 0;
    const std::size_t n = std::min<std::size_t>(1000, tt_.size());
    for (std::size_t i = 0; i < n; ++i) used += tt_[i].bound != BOUND_NONE;
    return int(used * 1000 / n);
}

void Searcher::checkLimits() {
    if (rootDepth_ <= 1) return;  // always finish depth 1 so we have a move
    if (nodeLimit_ && nodes_ >= nodeLimit_) stop_.store(true, std::memory_order_relaxed);
    if (hardMs_ >= 0 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_).count() >= hardMs_)
        stop_.store(true, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------------------
// Move ordering
// ---------------------------------------------------------------------------------------

int Searcher::scoreMove(const Position& pos, Move m, Move ttMove, int ply) const {
    if (m == ttMove) return 10'000'000;
    if (isTactical(m)) {
        int s = 0;
        if (isCapture(m)) {
            const Piece victim = isEnPassant(m) ? makePiece(~pos.sideToMove(), PAWN) : pos.pieceOn(toSq(m));
            s = eval::PieceValue[typeOf(victim)] * 16 - eval::PieceValue[typeOf(pos.pieceOn(fromSq(m)))];
        }
        if (isPromotion(m)) s += eval::PieceValue[promoType(m)] * 8;
        // Good/equal captures first, losing captures (by SEE) after quiet killers.
        return pos.seeGe(m, 0) ? 5'000'000 + s : -1'000'000 + s;
    }
    if (m == killers_[ply][0]) return 4'000'000;
    if (m == killers_[ply][1]) return 3'900'000;
    return history_[pos.sideToMove()][fromSq(m)][toSq(m)];
}

void Searcher::updateQuietStats(const Position& pos, Move best, const Move* quiets, int nQuiets, int depth, int ply) {
    if (killers_[ply][0] != best) {
        killers_[ply][1] = killers_[ply][0];
        killers_[ply][0] = best;
    }
    const int bonus = std::min(depth * depth, 400);
    auto upd = [&](Move m, int delta) {
        int& h = history_[pos.sideToMove()][fromSq(m)][toSq(m)];
        h += delta - h * std::abs(delta) / HISTORY_MAX;
    };
    upd(best, bonus);
    for (int i = 0; i < nQuiets; ++i)
        if (quiets[i] != best) upd(quiets[i], -bonus);
}

// ---------------------------------------------------------------------------------------
// Quiescence search
// ---------------------------------------------------------------------------------------

int Searcher::qsearch(Position& pos, int alpha, int beta, int ply) {
    ++nodes_;
    if ((nodes_ & 2047) == 0) checkLimits();
    if (stop_.load(std::memory_order_relaxed)) return 0;
    selDepth_ = std::max(selDepth_, ply);

    if (pos.isDraw(ply)) return 0;
    if (ply >= MAX_PLY - 1) return eval::evaluate(pos);

    const bool inCheck = pos.inCheck();
    int best = -INF;
    int stand = -INF;
    if (!inCheck) {
        stand = eval::evaluate(pos);
        best = stand;
        if (stand >= beta) return stand;
        alpha = std::max(alpha, stand);
    }

    MoveList ml;
    generate(pos, ml, inCheck ? GEN_ALL : GEN_CAPTURES);
    int scores[256];
    for (int i = 0; i < ml.size; ++i) scores[i] = scoreMove(pos, ml.moves[i], MOVE_NONE, ply);

    int legalCount = 0;
    for (int i = 0; i < ml.size; ++i) {
        int bi = i;
        for (int j = i + 1; j < ml.size; ++j) if (scores[j] > scores[bi]) bi = j;
        std::swap(ml.moves[i], ml.moves[bi]);
        std::swap(scores[i], scores[bi]);
        const Move m = ml.moves[i];
        if (!pos.legal(m)) continue;
        ++legalCount;

        if (!inCheck) {
            // Skip captures that lose material by SEE, and hopeless captures (delta pruning).
            if (isCapture(m) && !isPromotion(m)) {
                const Piece victim = isEnPassant(m) ? W_PAWN : pos.pieceOn(toSq(m));
                if (stand + eval::PieceValue[typeOf(victim)] + 200 <= alpha) continue;
            }
            if (!pos.seeGe(m, 0)) continue;
        }

        pos.makeMove(m);
        const int score = -qsearch(pos, -beta, -alpha, ply + 1);
        pos.unmakeMove();
        if (stop_.load(std::memory_order_relaxed)) return 0;

        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) break;
            }
        }
    }
    if (inCheck && legalCount == 0) return -MATE + ply;
    return best;
}

// ---------------------------------------------------------------------------------------
// Main search (PVS)
// ---------------------------------------------------------------------------------------

int Searcher::search(Position& pos, int depth, int alpha, int beta, int ply, bool allowNull) {
    const bool pvNode = beta - alpha > 1;
    const bool root = ply == 0;
    pvLen_[ply] = ply;

    const bool inCheck = pos.inCheck();
    if (depth <= 0 && !inCheck) return qsearch(pos, alpha, beta, ply);
    if (depth <= 0) depth = 1;

    ++nodes_;
    if ((nodes_ & 2047) == 0) checkLimits();
    if (stop_.load(std::memory_order_relaxed)) return 0;
    selDepth_ = std::max(selDepth_, ply);

    if (!root) {
        if (pos.isDraw(ply)) return 0;
        if (ply >= MAX_PLY - 1) return eval::evaluate(pos);
        // Mate distance pruning
        alpha = std::max(alpha, -MATE + ply);
        beta = std::min(beta, MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    // Transposition table
    const Key key = pos.key();
    TTEntry* e = probe(key);
    const bool ttHit = e->key == key && e->bound != BOUND_NONE;
    Move ttMove = ttHit ? e->move : MOVE_NONE;
    if (ttHit && !pvNode && e->depth >= depth) {
        const int s = fromTT(e->score, ply);
        if (e->bound == BOUND_EXACT || (e->bound == BOUND_LOWER && s >= beta) || (e->bound == BOUND_UPPER && s <= alpha))
            return s;
    }

    // Static evaluation
    int staticEval = VALUE_NONE;
    if (!inCheck) staticEval = (ttHit && e->eval != VALUE_NONE) ? e->eval : eval::evaluate(pos);
    evalStack_[ply] = staticEval;
    const bool improving = !inCheck && ply >= 2 && evalStack_[ply - 2] != VALUE_NONE && staticEval > evalStack_[ply - 2];

    if (!pvNode && !inCheck) {
        // Reverse futility pruning
        if (depth <= 6 && staticEval - 90 * depth + (improving ? 60 : 0) >= beta && std::abs(beta) < MATE_IN_MAX)
            return staticEval;

        // Null-move pruning
        if (allowNull && depth >= 3 && staticEval >= beta && pos.nonPawnPieces(pos.sideToMove()) > 0 &&
            beta > -MATE_IN_MAX) {
            const int R = 3 + depth / 4 + std::min(2, (staticEval - beta) / 200);
            pos.makeNull();
            const int ns = -search(pos, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
            pos.unmakeNull();
            if (stop_.load(std::memory_order_relaxed)) return 0;
            if (ns >= beta) return ns >= MATE_IN_MAX ? beta : ns;
        }
    }

    // Move loop
    MoveList ml;
    generate(pos, ml, GEN_ALL);
    int scores[256];
    for (int i = 0; i < ml.size; ++i) scores[i] = scoreMove(pos, ml.moves[i], ttMove, ply);

    const int origAlpha = alpha;
    int best = -INF;
    Move bestMove = MOVE_NONE;
    int moveCount = 0;
    Move quiets[64];
    int nQuiets = 0;
    bool skipQuiets = false;

    for (int i = 0; i < ml.size; ++i) {
        int bi = i;
        for (int j = i + 1; j < ml.size; ++j) if (scores[j] > scores[bi]) bi = j;
        std::swap(ml.moves[i], ml.moves[bi]);
        std::swap(scores[i], scores[bi]);
        const Move m = ml.moves[i];
        if (!pos.legal(m)) continue;
        ++moveCount;
        const bool tactical = isTactical(m);

        if (!root && best > -MATE_IN_MAX && pos.nonPawnPieces(pos.sideToMove()) > 0 && !inCheck) {
            if (!tactical) {
                if (skipQuiets) continue;
                // Late-move pruning and futility pruning for quiet moves
                if (!pvNode && depth <= 6 && moveCount > 4 + depth * depth) { skipQuiets = true; continue; }
                if (depth <= 4 && staticEval + 100 + 110 * depth <= alpha && moveCount > 1) { skipQuiets = true; continue; }
            }
            // SEE pruning: drop moves that lose too much material on the destination square
            if (depth <= 7 && moveCount > 1 && !pos.seeGe(m, tactical ? -90 * depth : -45 * depth)) continue;
        }

        pos.makeMove(m);
        const bool givesCheck = pos.inCheck();
        const int ext = (givesCheck && ply < 2 * rootDepth_) ? 1 : 0;
        const int newDepth = depth - 1 + ext;
        int score;

        if (moveCount == 1) {
            score = -search(pos, newDepth, -beta, -alpha, ply + 1, true);
        } else {
            int r = 0;
            if (depth >= 3 && moveCount > 2 && !(tactical && pos.seeGe(m, 0))) {
                r = lmrTable_[std::min(depth, 63)][std::min(moveCount, 63)];
                if (!pvNode) ++r;
                if (improving) --r;
                if (givesCheck || inCheck) --r;
                if (tactical) --r;
                r = std::clamp(r, 0, newDepth - 1);
            }
            score = -search(pos, newDepth - r, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha && r > 0) score = -search(pos, newDepth, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha && score < beta) score = -search(pos, newDepth, -beta, -alpha, ply + 1, true);
        }
        pos.unmakeMove();
        if (stop_.load(std::memory_order_relaxed)) return 0;

        if (score > best) {
            best = score;
            if (score > alpha) {
                bestMove = m;
                alpha = score;
                // Triangular PV update
                pv_[ply][ply] = m;
                for (int j = ply + 1; j < pvLen_[ply + 1]; ++j) pv_[ply][j] = pv_[ply + 1][j];
                pvLen_[ply] = pvLen_[ply + 1] > ply + 1 ? pvLen_[ply + 1] : ply + 1;
                if (root) rootBest_ = m;
                if (alpha >= beta) {
                    if (!tactical) updateQuietStats(pos, m, quiets, nQuiets, depth, ply);
                    break;
                }
            }
        }
        if (!tactical && nQuiets < 64) quiets[nQuiets++] = m;
    }

    if (moveCount == 0) return inCheck ? -MATE + ply : 0;

    const std::uint8_t bound = best >= beta ? BOUND_LOWER : (best > origAlpha ? BOUND_EXACT : BOUND_UPPER);
    if (!stop_.load(std::memory_order_relaxed)) {
        const bool sameKey = e->key == key;
        if (!sameKey || depth >= e->depth - 3 || bound == BOUND_EXACT) {
            // Keep the old move for this position if we have no better one to store.
            if (bestMove != MOVE_NONE || !sameKey) e->move = bestMove;
            e->key = key;
            e->score = std::int16_t(toTT(best, ply));
            e->eval = std::int16_t(staticEval);
            e->depth = std::int8_t(depth);
            e->bound = bound;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------
// Iterative deepening driver
// ---------------------------------------------------------------------------------------

void Searcher::printInfo(const Position& pos, int depth, int score, long long elapsedMs) const {
    (void)pos;
    std::cout << "info depth " << depth << " seldepth " << selDepth_ << " score ";
    if (std::abs(score) >= MATE_IN_MAX) {
        const int moves = (MATE - std::abs(score) + 1) / 2;
        std::cout << "mate " << (score > 0 ? moves : -moves);
    } else {
        std::cout << "cp " << score;
    }
    const long long nps = elapsedMs > 0 ? (long long)(nodes_) * 1000 / elapsedMs : (long long)nodes_ * 1000;
    std::cout << " nodes " << nodes_ << " nps " << nps << " hashfull " << hashfull() << " time " << elapsedMs << " pv";
    for (int i = 0; i < pvLen_[0]; ++i) std::cout << ' ' << moveToUci(pv_[0][i]);
    std::cout << std::endl;
}

SearchResult Searcher::think(Position& pos, const Limits& lim, bool verbose) {
    verbose_ = verbose;
    stop_.store(false);
    nodes_ = 0;
    selDepth_ = 0;
    rootBest_ = MOVE_NONE;
    nodeLimit_ = lim.nodes;
    start_ = Clock::now();
    std::memset(killers_, 0, sizeof(killers_));
    // History is aged rather than cleared between moves.
    for (auto& a : history_) for (auto& b : a) for (int& v : b) v /= 2;

    // Time management
    softMs_ = hardMs_ = -1;
    if (!lim.infinite) {
        if (lim.movetime >= 0) {
            hardMs_ = std::max(1LL, lim.movetime - 10);
            softMs_ = hardMs_;
        } else {
            const long long remain = pos.sideToMove() == WHITE ? lim.wtime : lim.btime;
            const long long inc = pos.sideToMove() == WHITE ? lim.winc : lim.binc;
            if (remain >= 0) {
                const int mtg = lim.movestogo > 0 ? lim.movestogo : 30;
                const long long base = remain / (mtg + 1) + inc * 3 / 4;
                hardMs_ = std::max(1LL, std::min({base * 3, remain / 3, remain - 30}));
                softMs_ = std::max(1LL, std::min(base, hardMs_));
            }
        }
    }

    SearchResult res;
    MoveList legal;
    if (generateLegal(pos, legal) == 0) return res;
    res.best = legal.moves[0];

    pos.attachNetwork(eval::activeNetwork());
    for (int i = 0; i < MAX_PLY + 1; ++i) pvLen_[i] = i;

    int prevScore = 0;
    const int maxDepth = std::clamp(lim.depth, 1, MAX_PLY - 2);
    for (rootDepth_ = 1; rootDepth_ <= maxDepth; ++rootDepth_) {
        int window = 25;
        int alpha = rootDepth_ >= 5 ? std::max(-INF, prevScore - window) : -INF;
        int beta = rootDepth_ >= 5 ? std::min(INF, prevScore + window) : INF;
        int score = 0;
        Move iterBest = MOVE_NONE;
        while (true) {
            rootBest_ = MOVE_NONE;
            score = search(pos, rootDepth_, alpha, beta, 0, false);
            if (rootBest_ != MOVE_NONE) iterBest = rootBest_;
            if (stop_.load(std::memory_order_relaxed)) break;
            if (score <= alpha && alpha > -INF) {
                beta = (alpha + beta) / 2;
                alpha = std::max(-INF, score - window);
                window += window / 2 + 5;
            } else if (score >= beta && beta < INF) {
                beta = std::min(INF, score + window);
                window += window / 2 + 5;
            } else {
                break;
            }
        }
        const bool stopped = stop_.load(std::memory_order_relaxed);
        if (stopped) {
            // A partially searched iteration is only trusted if it already found a new best move.
            if (iterBest != MOVE_NONE && rootDepth_ > 1) res.best = iterBest;
            break;
        }
        res.best = pv_[0][0] != MOVE_NONE && pvLen_[0] > 0 ? pv_[0][0] : (iterBest != MOVE_NONE ? iterBest : res.best);
        res.score = score;
        res.depth = rootDepth_;
        prevScore = score;
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_).count();
        if (verbose_) printInfo(pos, rootDepth_, score, ms);
        if (!lim.infinite && softMs_ >= 0 && ms >= softMs_ * 6 / 10 && lim.movetime < 0) break;
        if (std::abs(score) >= MATE_IN_MAX && rootDepth_ >= 2 && (MATE - std::abs(score)) * 2 <= rootDepth_ && !lim.infinite) break;
    }
    res.nodes = nodes_;
    pos.attachNetwork(nullptr);
    return res;
}

}  // namespace astra
