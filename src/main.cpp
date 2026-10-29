// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#include <iostream>
#include <string>

#include "eval.h"
#include "movegen.h"
#include "position.h"
#include "tests.h"
#include "train.h"
#include "tune.h"
#include "uci.h"
#include "version.h"

using namespace astra;

static std::string joinArgs(int argc, char** argv, int from) {
    std::string s;
    for (int i = from; i < argc; ++i) s += (i > from ? " " : "") + std::string(argv[i]);
    return s;
}

int main(int argc, char** argv) {
    Position::init();
    eval::init();
    const std::string a1 = argc > 1 ? argv[1] : "";

    if (a1 == "--version") { std::cout << ASTRA_NAME << " " << ASTRA_VERSION << "\n"; return 0; }
    if (a1 == "--selftest") return runSelfTest();
    if (a1 == "--bench") return runBench(argc > 2 ? std::stoi(argv[2]) : 9);
    if (a1 == "--perft" || a1 == "--perft-fen" || a1 == "--divide-fen") {
        const bool useStart = a1 == "--perft";
        if (argc < 3) { std::cerr << "usage: astra " << a1 << " <depth> [fen]\n"; return 2; }
        const int depth = std::stoi(argv[2]);
        Position pos;
        if (!pos.setFen(useStart ? Position::startFen() : joinArgs(argc, argv, 3))) { std::cerr << "Invalid FEN\n"; return 2; }
        if (a1 == "--divide-fen") {
            std::uint64_t total = 0;
            MoveList ml;
            generateLegal(pos, ml);
            for (int i = 0; i < ml.size; ++i) {
                pos.makeMove(ml.moves[i]);
                const std::uint64_t n = depth > 1 ? perft(pos, depth - 1) : 1;
                pos.unmakeMove();
                std::cout << moveToUci(ml.moves[i]) << ": " << n << "\n";
                total += n;
            }
            std::cout << "Total: " << total << "\n";
        } else {
            std::cout << "perft(" << depth << ") = " << perft(pos, depth) << "\n";
        }
        return 0;
    }
    if (a1 == "--datagen") {
        if (argc < 6) { std::cerr << "usage: astra --datagen <games> <depth> <seed> <outfile>\n"; return 2; }
        return runDatagen(std::stoi(argv[2]), std::stoi(argv[3]), std::stoull(argv[4]), argv[5]);
    }
    if (a1 == "--train-nnue") {
        if (argc < 4) {
            std::cerr << "usage: astra --train-nnue <data> <out.nnue> [hidden=64] [epochs=40] [lambda=0.5] [threads=auto] [lr=0.004]\n";
            return 2;
        }
        return runTrain(argv[2], argv[3], argc > 4 ? std::stoi(argv[4]) : 64, argc > 5 ? std::stoi(argv[5]) : 40,
                        argc > 6 ? std::stod(argv[6]) : 0.5, argc > 7 ? std::stoi(argv[7]) : 0,
                        argc > 8 ? std::stod(argv[8]) : 0.004);
    }
    if (a1 == "--tune") {
        if (argc < 3) { std::cerr << "usage: astra --tune <datafile> [iterations=30] [paramfile=tuned_params.txt]\n"; return 2; }
        return runTune(argv[2], argc > 3 ? std::stoi(argv[3]) : 30, argc > 4 ? argv[4] : "tuned_params.txt");
    }
    if (a1 == "--gen-test-net") {
        if (argc < 3) { std::cerr << "usage: astra --gen-test-net <file> [hidden=32] [seed=1]\n"; return 2; }
        std::string err;
        const int hidden = argc > 3 ? std::stoi(argv[3]) : 32;
        const std::uint64_t seed = argc > 4 ? std::stoull(argv[4]) : 1;
        if (!nnue::writeRandom(argv[2], hidden, seed, err)) { std::cerr << "error: " << err << "\n"; return 1; }
        std::cout << "wrote random test network (hidden=" << hidden << ") to " << argv[2]
                  << " - for plumbing tests only, it plays nonsense\n";
        return 0;
    }
    uciLoop();
    return 0;
}
