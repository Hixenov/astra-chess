// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#include "uci.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "eval.h"
#include "io.h"
#include "movegen.h"
#include "search.h"
#include "tests.h"
#include "version.h"

namespace astra {

namespace {

class UciEngine {
public:
    UciEngine() { pos_.setFen(Position::startFen()); }
    ~UciEngine() { stopSearch(); }

    void loop() {
        std::string line;
        while (std::getline(std::cin, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
            std::istringstream in(line);
            std::string cmd;
            in >> cmd;
            if (cmd == "uci") cmdUci();
            else if (cmd == "isready") say("readyok");
            else if (cmd == "ucinewgame") { stopSearch(); searcher_.clear(); pos_.setFen(Position::startFen()); positionOk_ = true; }
            else if (cmd == "position") cmdPosition(in);
            else if (cmd == "go") cmdGo(in);
            else if (cmd == "stop") stopSearch();
            else if (cmd == "setoption") cmdSetOption(in);
            else if (cmd == "d") say(pos_.fen());
            else if (cmd == "eval") cmdEval();
            else if (cmd == "status") cmdStatus();
            else if (cmd == "perft") { int d = 1; in >> d; stopSearch(); say("perft(" + std::to_string(d) + ") = " + std::to_string(perft(pos_, d))); }
            else if (cmd == "bench") { int d = 8; in >> d; stopSearch(); runBench(d); }
            else if (cmd == "quit") break;
        }
        stopSearch();
    }

private:
    void say(const std::string& s) {
        std::lock_guard<std::mutex> lock(outMutex());
        std::cout << s << std::endl;
    }

    void cmdUci() {
        say(std::string("id name ") + ASTRA_NAME + " " + ASTRA_VERSION);
        say("id author Astra Chess Project");
        say("option name Hash type spin default 64 min 1 max 4096");
        say("option name Clear Hash type button");
        say("option name MultiPV type spin default 1 min 1 max 256");
        say("option name Move Overhead type spin default 10 min 0 max 5000");
        say("option name UCI_AnalyseMode type check default false");
        say("option name UseNNUE type check default false");
        say("option name EvalFile type string default <empty>");
        say("uciok");
    }

    void cmdSetOption(std::istringstream& in) {
        stopSearch();
        std::string tok, name, value;
        in >> tok;  // "name"
        while (in >> tok && tok != "value") name += (name.empty() ? "" : " ") + tok;
        std::getline(in, value);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        if (name == "Hash") {
            try { searcher_.setHashMb(std::size_t(std::max(1, std::min(4096, std::stoi(value))))); } catch (...) {}
        } else if (name == "MultiPV") {
            try { multiPv_ = std::max(1, std::min(256, std::stoi(value))); } catch (...) {}
        } else if (name == "Move Overhead") {
            try { overheadMs_ = std::max(0, std::min(5000, std::stoi(value))); } catch (...) {}
        } else if (name == "Clear Hash") {
            searcher_.clear();
        } else if (name == "UseNNUE") {
            const bool on = value == "true" || value == "1";
            eval::setUseNnue(on);
            if (on && !eval::networkLoaded()) say("info string UseNNUE is on but no network is loaded (set EvalFile); using classical evaluation");
        } else if (name == "EvalFile") {
            if (value.empty() || value == "<empty>") { eval::unloadNetwork(); return; }
            std::string err;
            if (eval::loadNetwork(value, err)) say("info string NNUE network loaded: " + value);
            else say("info string NNUE load failed: " + err);
        }
    }

    void cmdPosition(std::istringstream& in) {
        stopSearch();
        std::string kind;
        in >> kind;
        Position next;
        bool ok = true;
        if (kind == "startpos") {
            ok = next.setFen(Position::startFen());
        } else if (kind == "fen") {
            std::string fen, token;
            for (int i = 0; i < 6 && in >> token; ++i) {
                if (token == "moves") break;
                fen += (i ? " " : "") + token;
            }
            ok = next.setFen(fen);
            if (!ok) say("info string invalid FEN");
        } else {
            ok = false;
        }
        std::string token;
        // "moves" may already have been consumed by a short FEN.
        while (ok && in >> token) {
            if (token == "moves") continue;
            const Move m = next.fromUci(token);
            if (m == MOVE_NONE) { say("info string illegal move " + token); ok = false; break; }
            next.makeMove(m);
        }
        positionOk_ = ok;
        if (ok) pos_ = next;
    }

    void cmdGo(std::istringstream& in) {
        stopSearch();
        if (!positionOk_) { say("bestmove 0000"); return; }
        Limits lim;
        lim.multiPv = multiPv_;
        lim.moveOverheadMs = overheadMs_;
        std::string tok;
        long long v;
        while (in >> tok) {
            if (tok == "depth") { in >> lim.depth; }
            else if (tok == "movetime") { in >> lim.movetime; }
            else if (tok == "wtime") { in >> lim.wtime; }
            else if (tok == "btime") { in >> lim.btime; }
            else if (tok == "winc") { in >> lim.winc; }
            else if (tok == "binc") { in >> lim.binc; }
            else if (tok == "movestogo") { in >> lim.movestogo; }
            else if (tok == "nodes") { in >> v; lim.nodes = std::uint64_t(std::max(0LL, v)); }
            else if (tok == "infinite" || tok == "ponder") { lim.infinite = true; }
            else if (tok == "mate") { int n = 0; in >> n; lim.depth = std::min(lim.depth, std::max(1, 2 * n)); }
        }
        Position copy = pos_;
        worker_ = std::thread([this, copy, lim]() mutable {
            const SearchResult r = searcher_.think(copy, lim, true);
            if (lim.infinite) {  // UCI: do not report a move before "stop"
                while (!stopFlag_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            say("bestmove " + moveToUci(r.best));
        });
    }

    void stopSearch() {
        stopFlag_.store(true);
        searcher_.stop();
        if (worker_.joinable()) worker_.join();
        stopFlag_.store(false);
    }

    void cmdEval() {
        say("classical: " + std::to_string(eval::evaluateClassical(pos_)) + " cp (side to move)");
        if (const nnue::Network* net = eval::loadedNetwork()) {
            Position p = pos_;
            p.attachNetwork(net);
            say("nnue: " + std::to_string(eval::evaluate(p)) + " cp (side to move)");
        }
    }

    // Non-standard helper used by tools/match.py: reports the game state of the current position.
    void cmdStatus() {
        if (!positionOk_) { say("status illegal"); return; }
        MoveList ml;
        if (generateLegal(pos_, ml) == 0) { say(pos_.inCheck() ? "status checkmate" : "status stalemate"); return; }
        if (pos_.halfmoveClock() >= 100) { say("status draw50"); return; }
        if (pos_.insufficientMaterial()) { say("status insufficient"); return; }
        if (pos_.isDraw(0)) { say("status threefold"); return; }
        say("status ongoing");
    }

    Position pos_;
    bool positionOk_ = true;
    int multiPv_ = 1;
    int overheadMs_ = 10;
    Searcher searcher_;
    std::thread worker_;
    std::atomic<bool> stopFlag_{false};
};

}  // namespace

void uciLoop() {
    UciEngine engine;
    engine.loop();
}

}  // namespace astra
