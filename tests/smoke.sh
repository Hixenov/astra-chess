#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
./astra --perft 1 | grep -q 'perft(1) = 20'
./astra --perft 2 | grep -q 'perft(2) = 400'
./astra --perft 3 | grep -q 'perft(3) = 8902'
./astra --perft 4 | grep -q 'perft(4) = 197281'
printf 'uci\nisready\nposition startpos\ngo depth 2\nquit\n' | ./astra > /tmp/astra-uci-test.txt
grep -q 'id name Astra Chess 0.2' /tmp/astra-uci-test.txt
grep -q 'uciok' /tmp/astra-uci-test.txt
grep -q 'readyok' /tmp/astra-uci-test.txt
grep -Eq '^info depth 2 .*score cp -?[0-9]+' /tmp/astra-uci-test.txt
grep -Eq '^bestmove [a-h][1-8][a-h][1-8][qrbn]?$' /tmp/astra-uci-test.txt
printf 'All Astra Chess 0.2 tests passed.\n'
