#include <iostream>
#include <string>

#include "eval.h"
#include "movegen.h"
#include "position.h"
#include "tests.h"
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
