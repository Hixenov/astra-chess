#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
./astra --perft 1 | grep -q 'perft(1) = 20'
./astra --perft 2 | grep -q 'perft(2) = 400'
./astra --perft 3 | grep -q 'perft(3) = 8902'
./astra --perft 4 | grep -q 'perft(4) = 197281'
# UCI handshake and a completed fixed-depth search.
(printf 'uci\nisready\nposition startpos\ngo depth 3\n'; sleep 0.2; printf 'quit\n') | ./astra > /tmp/astra-uci-test.txt
grep -q 'id name Astra Chess 0.3' /tmp/astra-uci-test.txt
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
printf 'All Astra Chess 0.3 tests passed.\n'
