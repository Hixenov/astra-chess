# Changelog

## 0.8
Lazy SMP (`Threads` option) with a lock-free shared transposition table, singular extensions, capture history,
`tools/make_net.py` (one-command NNUE pipeline), SMP tests in the UCI suite.

## 0.7
Built-in NNUE trainer (`--train-nnue`), self-play data now includes search scores, NNUE docs with training lessons
(validation must hold out whole games), trainer smoke test in the UCI test suite.

## 0.6
GUI compatibility (MultiPV, Move Overhead, CRLF-safe UCI, serialised output, release binaries in CI), GPL-3.0-or-later
licence, new evaluation terms (threats, outposts, king safety), Texel-tuned weights, TT in quiescence, countermove and
continuation history, IIR, `--datagen` / `--tune`.

## 0.5
Bitboards + magic bitboards, incremental Zobrist, SEE, NNUE architecture (inference, file format, incremental
accumulators), match runner with Elo/LOS/SPRT, expanded self-tests, CI, multi-file source layout.

## 0.4
Aspiration windows, 0x88 edge-wrap fixes, en-passant unmake fix, perft regression suite.

## 0.3 / 0.2 / 0.1
Search heuristics, timed asynchronous UCI search, initial engine.
