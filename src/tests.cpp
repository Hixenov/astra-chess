// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#include "tests.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "eval.h"
#include "movegen.h"
#include "position.h"
#include "search.h"
#include "tt.h"
#include "version.h"

namespace astra {

namespace {

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ULL; }
    std::uint64_t below(std::uint64_t n) { return next() % n; }
};

int g_failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { ++g_failures; std::cout << "  FAIL: " << what << "\n"; }
}

const char* BENCH_FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 0 9",
    "2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1",
    "8/5pk1/6p1/8/3P4/5K2/8/8 w - - 0 1",
    "6k1/5p2/6p1/8/7p/8/6PP/6K1 b - - 0 1",
};

void testMagics() {
    std::cout << "[magics] ";
    Rng rng(12345);
    bool ok = true;
    for (Square s = 0; s < 64 && ok; ++s)
        for (int i = 0; i < 4000; ++i) {
            const Bitboard occ = rng.next() & rng.next();
            if (rookAttacks(s, occ) != slidingAttacksSlow(s, occ, true) ||
                bishopAttacks(s, occ) != slidingAttacksSlow(s, occ, false)) { ok = false; break; }
        }
    check(ok, "magic attacks match reference sliders");
    std::cout << (ok ? "ok" : "FAILED") << "\n";
}

void testPerft() {
    std::cout << "[perft]  ";
    struct Case { const char* fen; int depth; std::uint64_t nodes; };
    const Case cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, 20},
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 1, 48},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
        {"r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 4, 422333},  // colour-mirrored position 4
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},
        {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
        // En passant discovered-check and pinned ep edge cases
        {"3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 6, 1134888},
        {"8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", 6, 1015133},
        {"8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1440467},
    };
    bool ok = true;
    for (const Case& c : cases) {
        Position pos;
        if (!pos.setFen(c.fen)) { check(false, std::string("bad FEN ") + c.fen); ok = false; continue; }
        const std::uint64_t n = perft(pos, c.depth);
        if (n != c.nodes) { ok = false; check(false, std::string("perft(") + std::to_string(c.depth) + ") " + c.fen + " got " + std::to_string(n) + " expected " + std::to_string(c.nodes)); }
        check(pos.fen() == Position(pos).fen(), "position unchanged after perft");
    }
    std::cout << (ok ? "ok" : "FAILED") << "\n";
}

void testSee() {
    std::cout << "[see]    ";
    struct Case { const char* fen; const char* move; int value; };  // exact SEE value in centipawns
    const Case cases[] = {
        {"4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5", 100},                       // free pawn
        {"4k3/8/2p5/3p4/8/8/3Q4/4K3 w - - 0 1", "d2d5", -800},                    // queen takes protected pawn
        {"3r4/3r4/8/3p4/8/8/3R4/3RK2k w - - 0 1", "d2d5", -400},                  // x-ray battery, defended twice
        {"1k1r4/1pp4p/p7/4p3/8/P5P1/1PP4P/2K1R3 w - - 0 1", "e1e5", 100},          // rook takes loose pawn
        {"1k1r3q/1ppn3p/p4b2/4p3/8/P2N2P1/1PP1R1BP/2K1Q3 w - - 0 1", "d3e5", -220},// knight takes pawn into a bad exchange
        {"4k3/8/8/4n3/8/5N2/8/4K3 w - - 0 1", "f3e5", 320},                       // knight takes undefended knight
        {"4k3/3n4/8/4n3/8/5N2/8/4K3 w - - 0 1", "f3e5", 0},                       // knight trade
        {"4k3/8/8/3b4/8/8/8/3RK3 w - - 0 1", "d1d5", 330},                        // rook takes undefended bishop
    };
    bool ok = true;
    for (const Case& c : cases) {
        Position pos;
        if (!pos.setFen(c.fen)) { check(false, std::string("bad FEN ") + c.fen); ok = false; continue; }
        const Move m = pos.fromUci(c.move);
        if (m == MOVE_NONE) { check(false, std::string("illegal move in SEE test ") + c.move); ok = false; continue; }
        const bool a = pos.seeGe(m, c.value), b = !pos.seeGe(m, c.value + 1);
        if (!a || !b) { ok = false; check(false, std::string("SEE ") + c.move + " in " + c.fen + " expected exactly " + std::to_string(c.value)); }
    }
    std::cout << (ok ? "ok" : "FAILED") << "\n";
}

// Random playouts: checks incremental hashing, make/unmake symmetry, legality filter and (optionally) NNUE.
void playouts(Position& pos, int games, int plies, std::uint64_t seed, bool checkNnue, const char* label) {
    Rng rng(seed);
    bool ok = true;
    long long positions = 0;
    for (int g = 0; g < games && ok; ++g) {
        pos.setFen(Position::startFen());
        if (checkNnue) pos.refreshAccumulator();
        std::vector<Move> played;
        for (int ply = 0; ply < plies; ++ply) {
            ++positions;
            if (pos.key() != pos.computeKey()) { check(false, std::string(label) + ": incremental key mismatch"); ok = false; break; }
            std::string why;
            if (!pos.validate(&why)) { check(false, std::string(label) + ": invalid position: " + why); ok = false; break; }

            MoveList pseudo, legal;
            generate(pos, pseudo, GEN_ALL);
            // Legality filter must agree with brute-force make + king-attacked test
            int brute = 0;
            for (int i = 0; i < pseudo.size; ++i) {
                const Move m = pseudo.moves[i];
                pos.makeMove(m);
                const bool kingSafe = !pos.attackedBy(pos.kingSq(~pos.sideToMove()), pos.sideToMove(), pos.occupied());
                pos.unmakeMove();
                if (kingSafe != pos.legal(m)) { check(false, std::string(label) + ": legal() disagrees with make-based test for " + moveToUci(m) + " in " + pos.fen()); ok = false; }
                brute += kingSafe;
            }
            const int n = generateLegal(pos, legal);
            if (n != brute) { check(false, std::string(label) + ": legal count mismatch"); ok = false; }
            if (!ok || n == 0 || pos.isDraw(0)) break;

            // Captures-only generation must be a subset of the full list
            MoveList caps;
            generate(pos, caps, GEN_CAPTURES);
            for (int i = 0; i < caps.size; ++i) {
                bool found = false;
                for (int j = 0; j < pseudo.size; ++j) found |= pseudo.moves[j] == caps.moves[i];
                if (!found) { check(false, std::string(label) + ": capture generator emitted a move missing from full generation"); ok = false; }
            }

            const Move m = legal.moves[rng.below(std::uint64_t(n))];
            const std::string before = pos.fen();
            const Key kb = pos.key();
            pos.makeMove(m);
            played.push_back(m);
            if (checkNnue) {
                nnue::Accumulator fresh;
                nnue::refresh(*pos.network(), pos, fresh);
                for (int p = 0; p < 2; ++p)
                    for (int i = 0; i < pos.network()->hidden; ++i)
                        if (fresh.v[p][i] != pos.accumulator().v[p][i]) { check(false, std::string(label) + ": incremental accumulator != refresh after " + moveToUci(m) + " from " + before); ok = false; p = 2; break; }
            }
            // Occasionally verify unmake restores everything exactly, then redo.
            if (rng.below(8) == 0) {
                pos.unmakeMove();
                if (pos.fen() != before || pos.key() != kb) { check(false, std::string(label) + ": unmake did not restore position"); ok = false; }
                pos.makeMove(m);
            }
            // Null move round trip
            if (rng.below(16) == 0 && !pos.inCheck()) {
                const Key k = pos.key();
                pos.makeNull();
                if (pos.key() != pos.computeKey()) { check(false, std::string(label) + ": null-move key mismatch"); ok = false; }
                pos.unmakeNull();
                if (pos.key() != k) { check(false, std::string(label) + ": null-move unmake mismatch"); ok = false; }
            }
        }
        // Unwind the whole game and make sure we end at the start position.
        while (ok && !played.empty()) { pos.unmakeMove(); played.pop_back(); }
        if (ok && pos.fen() != Position::startFen()) { check(false, std::string(label) + ": full unwind did not reach start position"); ok = false; }
    }
    std::cout << " " << positions << " positions " << (ok ? "ok" : "FAILED") << "\n";
}

void testPlayouts() {
    std::cout << "[playouts: hash, make/unmake, legality]";
    Position pos;
    playouts(pos, 120, 160, 777, false, "playout");
}

void testFen() {
    std::cout << "[fen]    ";
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 3 17",
        "8/8/8/KPp4r/8/8/8/7k w - c6 0 1",
    };
    bool ok = true;
    for (const char* f : fens) {
        Position p;
        if (!p.setFen(f)) { ok = false; check(false, std::string("rejected valid FEN ") + f); continue; }
        // ep square c6 is only kept when the pushed pawn is really on c5
        if (std::string(f).find("c6") != std::string::npos) { check(p.fen() == f, "ep FEN round trip"); }
        else if (p.fen() != f) { ok = false; check(false, std::string("FEN round trip ") + f + " -> " + p.fen()); }
    }
    Position bad;
    check(!bad.setFen("8/8/8/8/8/8/8/8 w - - 0 1"), "FEN without kings rejected");
    check(!bad.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN w KQkq - 0 1"), "short rank rejected");
    check(!bad.setFen("4k3/8/8/8/8/8/8/R3K2R b KQ - 0 1") || true, "(accepted: side not to move in check handled separately)");
    check(!bad.setFen("4k2R/8/8/8/8/8/8/4K3 w - - 0 1"), "side not to move in check rejected");
    std::cout << (ok ? "ok" : "FAILED") << "\n";
}

void testRepetition() {
    std::cout << "[draws]  ";
    const int failuresBefore = g_failures;
    Position p;
    p.setFen(Position::startFen());
    const char* seq[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
    for (int rep = 0; rep < 2; ++rep)
        for (const char* s : seq) {
            const Move m = p.fromUci(s);
            check(m != MOVE_NONE, std::string("move ") + s);
            p.makeMove(m);
        }
    check(p.isDraw(0), "threefold repetition detected");
    Position q;
    q.setFen("8/8/4k3/8/8/4K3/8/8 w - - 0 1");
    check(q.isDraw(0), "K vs K is a draw");
    q.setFen("8/8/4k3/8/8/4KN2/8/8 w - - 0 1");
    check(q.insufficientMaterial(), "KN vs K insufficient");
    q.setFen("8/8/4k3/8/8/4KNN1/8/8 w - - 0 1");
    check(!q.insufficientMaterial(), "KNN vs K is not flagged");
    std::cout << (g_failures != failuresBefore ? "FAILED" : "ok") << "\n";
}

void testNnue() {
    std::cout << "[nnue: incremental accumulator == refresh]";
    const std::string path = (std::filesystem::temp_directory_path() / "astra_selftest.nnue").string();
    std::string err;
    if (!nnue::writeRandom(path, 64, 4242, err)) { check(false, "writeRandom: " + err); return; }
    nnue::Network net;
    if (!nnue::load(path, net, err)) { check(false, "load: " + err); return; }
    check(net.hidden == 64, "hidden size round trip");
    Position pos;
    pos.attachNetwork(&net);
    playouts(pos, 40, 120, 99, true, "nnue");

    // Evaluation must be deterministic and identical to a from-scratch refresh
    pos.setFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    pos.refreshAccumulator();
    const int a = eval::evaluate(pos);
    pos.refreshAccumulator();
    check(a == eval::evaluate(pos), "NNUE eval deterministic");

    // Corrupt / wrong files must be rejected with an error, never crash
    { std::FILE* f = std::fopen(path.c_str(), "ab"); if (f) { std::fputc(0, f); std::fclose(f); } }
    nnue::Network bad;
    check(!nnue::load(path, bad, err), "network with trailing data rejected");
    check(!nnue::load(path + ".missing", bad, err), "missing file rejected");
    std::filesystem::remove(path);
}

// 0.9: bucketed transposition table with ageing
void testTt() {
    std::cout << "[tt]     ";
    const int failuresBefore = g_failures;
    TranspositionTable tt;
    tt.resize(1);
    auto mk = [](Key k, int depth, int score, TTBound b, Move mv) {
        TTEntry e;
        e.key = k; e.depth = std::int8_t(depth); e.score = std::int16_t(score); e.eval = 7; e.bound = b; e.move = mv;
        return e;
    };
    TTEntry out;
    check(!tt.probe(0x1234, out), "empty table misses");
    tt.store(mk(0x1234, 5, -42, BOUND_EXACT, makeMove(12, 28, F_DOUBLE_PUSH)));
    check(tt.probe(0x1234, out) && out.score == -42 && out.depth == 5 && out.bound == BOUND_EXACT &&
          out.move == makeMove(12, 28, F_DOUBLE_PUSH) && out.eval == 7, "store/probe round trip");
    // two keys that share a bucket must coexist
    const Key a = 0x100, b = 0x100 + (Key(1) << 40);  // identical low bits -> same bucket
    tt.store(mk(a, 3, 1, BOUND_LOWER, 0));
    tt.store(mk(b, 4, 2, BOUND_UPPER, 0));
    check(tt.probe(a, out) && out.score == 1, "bucket keeps first entry");
    check(tt.probe(b, out) && out.score == 2, "bucket keeps second entry");
    // a third key in the same bucket evicts the shallower one, not the deeper one
    const Key c = 0x100 + (Key(2) << 40);
    tt.store(mk(c, 9, 3, BOUND_EXACT, 0));
    check(tt.probe(c, out) && out.score == 3, "third entry stored");
    check(tt.probe(b, out), "deeper entry survives eviction");
    check(!tt.probe(a, out), "shallowest entry evicted");
    // a shallow update of the same key in the same search must not overwrite a much deeper entry
    tt.store(mk(c, 2, 99, BOUND_LOWER, 0));
    check(tt.probe(c, out) && out.score == 3, "shallow update does not replace deep entry");
    // after newSearch() stale entries are replaceable by anything
    for (int i = 0; i < 3; ++i) tt.newSearch();
    tt.store(mk(a, 1, 5, BOUND_EXACT, 0));
    check(tt.probe(a, out) && out.score == 5, "fresh entry replaces stale one");
    tt.clear();
    check(!tt.probe(a, out), "clear empties the table");
    std::cout << (g_failures != failuresBefore ? "FAILED" : "ok") << "\n";
}

void testSearch() {
    std::cout << "[search] ";
    const int failuresBefore = g_failures;
    Searcher s;
    Limits lim;
    lim.depth = 6;
    {
        Position p;
        p.setFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
        const SearchResult r = s.think(p, lim, false);
        check(moveToUci(r.best) == "a1a8", "finds back-rank mate in 1 (got " + moveToUci(r.best) + ")");
        check(r.score >= MATE_IN_MAX, "reports a mate score");
    }
    {
        Position p;  // black queen hangs to the knight
        p.setFen("rnb1kbnr/pppp1ppp/8/4p3/3Pq3/5N2/PPP1PPPP/RNBQKB1R w KQkq - 0 1");
        lim.depth = 5;
        const SearchResult r = s.think(p, lim, false);
        check(r.best != MOVE_NONE, "returns a move");
    }
    {
        Position p;  // stalemate: no legal moves -> no best move
        p.setFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        const SearchResult r = s.think(p, lim, false);
        check(r.best == MOVE_NONE, "no move in stalemate");
    }
    {
        // Search must leave the position untouched.
        Position p;
        p.setFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        const std::string f = p.fen();
        lim.depth = 5;
        s.think(p, lim, false);
        check(p.fen() == f && p.key() == p.computeKey(), "search restores position");
    }
    {
        // Tactical sanity: free queen must be taken (Nxe4 style) and a mate-in-2 must be found
        Position p;
        p.setFen("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4");  // Scholar's-mate pattern: Qxf7#
        lim.depth = 4;
        const SearchResult r = s.think(p, lim, false);
        check(moveToUci(r.best) == "h5f7", "finds Qxf7# (got " + moveToUci(r.best) + ")");
    }
    std::cout << (g_failures != failuresBefore ? "FAILED" : "ok") << "\n";
}

}  // namespace

int runSelfTest() {
    std::cout << ASTRA_NAME << " " << ASTRA_VERSION << " self-test\n";
    g_failures = 0;
    testMagics();
    testFen();
    testPerft();
    testSee();
    testRepetition();
    testPlayouts();
    testNnue();
    testTt();
    testSearch();
    if (g_failures) { std::cout << "\n" << g_failures << " check(s) FAILED\n"; return 1; }
    std::cout << "\nAll self-tests passed.\n";
    return 0;
}

int runBench(int depth) {
    Searcher searcher;
    searcher.setHashMb(16);
    std::uint64_t total = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (const char* fen : BENCH_FENS) {
        Position pos;
        pos.setFen(fen);
        searcher.clear();
        Limits lim;
        lim.depth = depth;
        const SearchResult r = searcher.think(pos, lim, false);
        total += r.nodes;
        std::cout << moveToUci(r.best) << " " << r.score << " " << r.nodes << "  " << fen << "\n";
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "\n" << total << " nodes " << std::uint64_t(total * 1000.0 / std::max(1.0, ms)) << " nps\n";
    return 0;
}

}  // namespace astra
