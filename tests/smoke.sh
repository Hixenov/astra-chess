#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
./astra --perft 1 | grep -q 'perft(1) = 20'
./astra --perft 2 | grep -q 'perft(2) = 400'
./astra --perft 3 | grep -q 'perft(3) = 8902'
printf 'uci\nisready\nposition startpos\ngo depth 2\nquit\n' | ./astra > /tmp/astra-uci-test.txt
grep -q 'uciok' /tmp/astra-uci-test.txt
grep -q 'readyok' /tmp/astra-uci-test.txt
grep -Eq '^bestmove [a-h][1-8][a-h][1-8][qrbn]?$' /tmp/astra-uci-test.txt
echo 'All Astra Chess smoke tests passed.'
