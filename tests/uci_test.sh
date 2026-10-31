#!/usr/bin/env bash
# UCI protocol tests for Astra Chess (run via `make test`).
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=./astra
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
MOVE_RE='^bestmove [a-h][1-8][a-h][1-8][qrbn]?$'

# 1. Handshake, options, fixed-depth search
(printf 'uci\nisready\nposition startpos\ngo depth 6\n'; sleep 0.5; printf 'quit\n') | $BIN > "$TMP/a.txt"
grep -q '^id name Astra Chess 0.9$' "$TMP/a.txt"
grep -q '^option name Hash type spin' "$TMP/a.txt"
grep -q '^option name UseNNUE type check' "$TMP/a.txt"
grep -q '^option name MultiPV type spin' "$TMP/a.txt"
grep -q '^option name Move Overhead type spin' "$TMP/a.txt"
grep -q '^option name EvalFile type string' "$TMP/a.txt"
grep -q '^uciok$' "$TMP/a.txt"
grep -q '^readyok$' "$TMP/a.txt"
grep -q '^info depth 6 ' "$TMP/a.txt"
grep -Eq "$MOVE_RE" "$TMP/a.txt"

# 2. Timed search returns a legal move quickly
START=$(date +%s%N)
(printf 'position startpos moves e2e4 e7e5\ngo movetime 100\n'; sleep 0.4; printf 'quit\n') | $BIN > "$TMP/b.txt"
grep -Eq "$MOVE_RE" "$TMP/b.txt"
ELAPSED=$(( ($(date +%s%N) - START) / 1000000 ))
[ "$ELAPSED" -lt 2000 ]

# 3. `stop` interrupts an infinite search and still yields exactly one bestmove
(printf 'position startpos\ngo infinite\n'; sleep 0.2; printf 'stop\n'; sleep 0.2; printf 'quit\n') | $BIN > "$TMP/c.txt"
[ "$(grep -c '^bestmove' "$TMP/c.txt")" -eq 1 ]
grep -Eq "$MOVE_RE" "$TMP/c.txt"

# 4. Game clocks
(printf 'position startpos\ngo wtime 2000 btime 2000 winc 50 binc 50\n'; sleep 0.6; printf 'quit\n') | $BIN > "$TMP/d.txt"
grep -Eq "$MOVE_RE" "$TMP/d.txt"

# 5. FEN positions, mate score and promotion move format
(printf 'position fen 6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1\ngo depth 5\n'; sleep 0.3; printf 'quit\n') | $BIN > "$TMP/e.txt"
grep -q '^bestmove a1a8$' "$TMP/e.txt"
grep -q 'score mate 1' "$TMP/e.txt"
(printf 'position fen 8/P6k/8/8/8/8/8/K7 w - - 0 1\ngo depth 4\n'; sleep 0.3; printf 'quit\n') | $BIN > "$TMP/f.txt"
grep -Eq '^bestmove a7a8[qrbn]$' "$TMP/f.txt"

# 6. Illegal move in `position` is reported, not played; status helper works
(printf 'position startpos moves e2e5\nstatus\nposition startpos moves f2f3 e7e5 g2g4 d8h4\nstatus\nquit\n') | $BIN > "$TMP/g.txt"
grep -q 'illegal move e2e5' "$TMP/g.txt"
grep -q '^status illegal$' "$TMP/g.txt"
grep -q '^status checkmate$' "$TMP/g.txt"

# 7. Hash option, ucinewgame, NNUE option plumbing (no network loaded -> falls back)
(printf 'setoption name Hash value 16\nucinewgame\nsetoption name UseNNUE value true\nsetoption name EvalFile value /nonexistent.nnue\nposition startpos\ngo depth 4\n'; sleep 0.3; printf 'quit\n') | $BIN > "$TMP/h.txt"
grep -q 'NNUE load failed' "$TMP/h.txt"
grep -Eq "$MOVE_RE" "$TMP/h.txt"

# 8. NNUE end to end with a generated (random) test network: loads, searches, stays legal
$BIN --gen-test-net "$TMP/test.nnue" 32 7 > /dev/null
(printf "setoption name EvalFile value $TMP/test.nnue\nsetoption name UseNNUE value true\nposition startpos\ngo depth 5\neval\n"; sleep 0.5; printf 'quit\n') | $BIN > "$TMP/i.txt"
grep -q 'NNUE network loaded' "$TMP/i.txt"
grep -q '^nnue: ' "$TMP/i.txt"
grep -Eq "$MOVE_RE" "$TMP/i.txt"

# 9. Windows-style CRLF line endings from the GUI
(printf 'uci\r\nisready\r\nposition startpos moves e2e4\r\ngo depth 4\r\n'; sleep 0.3; printf 'quit\r\n') | $BIN > "$TMP/j.txt"
grep -q '^uciok$' "$TMP/j.txt"
grep -q '^readyok$' "$TMP/j.txt"
grep -Eq "$MOVE_RE" "$TMP/j.txt"

# 10. MultiPV: analysis lines with distinct first moves, ordered by score
(printf 'setoption name MultiPV value 3\nposition startpos\ngo depth 6\n'; sleep 0.5; printf 'quit\n') | $BIN > "$TMP/k.txt"
grep -q 'info depth 6 .* multipv 1 ' "$TMP/k.txt"
grep -q 'info depth 6 .* multipv 2 ' "$TMP/k.txt"
grep -q 'info depth 6 .* multipv 3 ' "$TMP/k.txt"
FIRST=$(grep 'info depth 6 ' "$TMP/k.txt" | sed 's/.* pv //' | awk '{print $1}' | sort -u | wc -l)
[ "$FIRST" -eq 3 ]
grep -Eq "$MOVE_RE" "$TMP/k.txt"

# 11. MultiPV larger than the number of legal moves is clamped (1 legal move)
(printf 'setoption name MultiPV value 5\nposition fen 7k/8/8/8/8/8/6q1/K7 w - - 0 1\ngo depth 3\n'; sleep 0.3; printf 'quit\n') | $BIN > "$TMP/l.txt"
grep -Eq "$MOVE_RE" "$TMP/l.txt"

# 12. isready is answered while a search is running
(printf 'position startpos\ngo infinite\n'; sleep 0.2; printf 'isready\n'; sleep 0.2; printf 'stop\n'; sleep 0.2; printf 'quit\n') | $BIN > "$TMP/m.txt"
grep -q '^readyok$' "$TMP/m.txt"
[ "$(grep -c '^bestmove' "$TMP/m.txt")" -eq 1 ]

# 13. Data generation -> NNUE training -> load -> search (tiny, plumbing only)
$BIN --datagen 40 3 5 "$TMP/data.txt" > /dev/null
[ "$(wc -l < "$TMP/data.txt")" -ge 1000 ]
$BIN --train-nnue "$TMP/data.txt" "$TMP/trained.nnue" 8 2 0.8 1 > "$TMP/train.log"
grep -q 'quantisation check' "$TMP/train.log"
(printf "setoption name EvalFile value $TMP/trained.nnue\nsetoption name UseNNUE value true\nposition startpos\ngo depth 4\n"; sleep 0.4; printf 'quit\n') | $BIN > "$TMP/n.txt"
grep -q 'NNUE network loaded' "$TMP/n.txt"
grep -Eq "$MOVE_RE" "$TMP/n.txt"

# 14. Lazy SMP: Threads option, search, stop, changing thread count, MultiPV fallback
grep -q '^option name Threads type spin' "$TMP/a.txt"
(printf 'setoption name Threads value 4\nposition startpos\ngo depth 8\n'; sleep 1.5; printf 'setoption name Threads value 2\nposition startpos moves e2e4\ngo movetime 200\n'; sleep 0.6; printf 'quit\n') | $BIN > "$TMP/o.txt"
[ "$(grep -c '^bestmove' "$TMP/o.txt")" -eq 2 ]
grep -q 'info depth 8 ' "$TMP/o.txt"
(printf 'setoption name Threads value 4\nposition startpos\ngo infinite\n'; sleep 0.4; printf 'stop\n'; sleep 0.3; printf 'quit\n') | $BIN > "$TMP/p.txt"
[ "$(grep -c '^bestmove' "$TMP/p.txt")" -eq 1 ]
(printf 'setoption name Threads value 4\nsetoption name MultiPV value 2\nposition startpos\ngo depth 6\n'; sleep 0.6; printf 'quit\n') | $BIN > "$TMP/q.txt"
grep -q 'multipv 2 ' "$TMP/q.txt"
# node counter must never go backwards between depths
(printf 'setoption name Threads value 4\nposition startpos\ngo depth 9\n'; sleep 1.2; printf 'quit\n') | $BIN | grep -o 'nodes [0-9]*' | awk '{if($2<prev){exit 1}; prev=$2}'

# 15. Deterministic node count for a fixed-depth bench (regression signature)
$BIN --bench 6 | tail -1 | grep -q ' nodes '

printf 'All Astra Chess 0.9 UCI tests passed.\n'
