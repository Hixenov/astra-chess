#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Starting position perft regression.
./astra --perft 1 | grep -q 'perft(1) = 20'
./astra --perft 2 | grep -q 'perft(2) = 400'
./astra --perft 3 | grep -q 'perft(3) = 8902'
./astra --perft 4 | grep -q 'perft(4) = 197281'
# Canonical Kiwipete: castling, checks, captures and pins.
KIWIPETE='r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1'
./astra --perft-fen 1 "$KIWIPETE" | grep -q 'perft(1) = 48'
./astra --perft-fen 2 "$KIWIPETE" | grep -q 'perft(2) = 2039'
./astra --perft-fen 3 "$KIWIPETE" | grep -q 'perft(3) = 97862'
# Endgame position with en-passant, rook and king tactics.
POS3='8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1'
./astra --perft-fen 1 "$POS3" | grep -q 'perft(1) = 14'
./astra --perft-fen 2 "$POS3" | grep -q 'perft(2) = 191'
./astra --perft-fen 3 "$POS3" | grep -q 'perft(3) = 2812'
# Promotions, castling rights and tactical checks.
POS4='r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1'
./astra --perft-fen 1 "$POS4" | grep -q 'perft(1) = 6'
./astra --perft-fen 2 "$POS4" | grep -q 'perft(2) = 264'
# UCI handshake and a completed fixed-depth search.
(printf 'uci\nisready\nposition startpos\ngo depth 3\n'; sleep 0.2; printf 'quit\n') | ./astra > /tmp/astra-uci-test.txt
grep -q 'id name Astra Chess 0.4' /tmp/astra-uci-test.txt
grep -q 'uciok' /tmp/astra-uci-test.txt
grep -q 'readyok' /tmp/astra-uci-test.txt
grep -q '^info depth 3 ' /tmp/astra-uci-test.txt
grep -Eq '^bestmove [a-h][1-8][a-h][1-8][qrbn]?$' /tmp/astra-uci-test.txt
# Timed search must return a legal UCI move.
(printf 'position startpos\ngo movetime 50\n'; sleep 0.2; printf 'quit\n') | ./astra > /tmp/astra-time-test.txt
grep -Eq '^bestmove [a-h][1-8][a-h][1-8][qrbn]?$' /tmp/astra-time-test.txt
# stop must interrupt a deep search and still produce bestmove.
(printf 'position startpos\ngo depth 60\n'; sleep 0.03; printf 'stop\nquit\n') | ./astra > /tmp/astra-stop-test.txt
grep -Eq '^bestmove [a-h][1-8][a-h][1-8][qrbn]?$' /tmp/astra-stop-test.txt
printf 'All Astra Chess 0.4 tests passed.\n'
