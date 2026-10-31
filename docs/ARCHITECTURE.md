# Astra Chess architecture

```
src/types.h       squares, pieces, 16-bit move encoding (from | to<<6 | flag<<12)
src/bitboard.*    attack tables, magic bitboards (found at start-up in ~ms), between/line tables
src/position.*    Position: bitboards + mailbox, incremental Zobrist, make/unmake, null move,
                  fast legality (checkers/pinned), SEE, repetition/50-move/insufficient material
src/movegen.*     pseudo-legal generation (all / captures+promotions), legal filter, perft
src/nnue.*        NNUE network, file loader, accumulator add/sub/refresh, inference
src/eval.*        tapered hand-crafted eval; switches to NNUE when a network is attached
src/tt.h          lock-free 2-way bucketed transposition table with ageing (0.9)
src/search.*      iterative deepening, aspiration windows, PVS, TT, null move, LMR, RFP,
                  futility/LMP, SEE pruning, quiescence with SEE filter, killers + history,
                  singular extensions, pawn-structure correction history (0.9)
src/uci.*         UCI protocol, search thread, `status` helper for tools/match.py
src/tests.*       --selftest (perft, SEE, hashing, legality cross-check, NNUE, TT) and --bench
tools/match.py    automatic matches, Elo +- CI, LOS, SPRT
```

## Move encoding

16 bits: `from` (6) | `to` (6) | `flag` (4). Flag bit 2 = capture, bit 3 = promotion
(`0 quiet, 1 double push, 2/3 castle, 4 capture, 5 en passant, 8..11 promo N/B/R/Q, 12..15 promo+capture`).

## Legality

Move generation is pseudo-legal. `Position::legal` uses `checkers` and `pinned` (updated at the end of
every make): double check allows only king moves, single check requires capture/block, pinned pieces
must stay on the king line, king moves test the destination with the king removed from the occupancy,
and en passant re-tests all sliders after removing both pawns. `--selftest` cross-checks this against
brute force (make + king-attacked) on every generated move of ~18,000 random-game positions.

## SEE

`Position::seeGe(move, threshold)` is the swap algorithm with x-ray discovery. Values:
P 100, N 320, B 330, R 500, Q 900. Castling, en passant and promotions count as neutral (0).
Known limitation: absolutely pinned pieces are not excluded as recapturers.
Uses in search:
* move ordering: captures with SEE >= 0 are tried first; losing captures sort below killers/history
* quiescence: captures with SEE < 0 are skipped (unless in check)
* main search: low-depth pruning of moves whose SEE is below `-45*depth` (quiets) / `-90*depth` (captures)

## Transposition table (0.9)

Slots are two 64-bit words (`data`, `check = key ^ data`). Two slots form a bucket. `data` carries
score, static eval, move, depth, 2 bound bits and a 6-bit age. A probe checks both slots; a store updates the
matching slot (depth-preferred inside the same search) or replaces the slot with the lowest
`depth - 4 * age_difference`.

## Correction history (0.9)

`corrHist_[stm][pawnKey >> 50]` stores a running average of `searchScore - rawStaticEval` (in 1/256 cp, clamped to
+-48 cp). `staticEval = rawEval + correction`. Updated at the end of a node only when the best move is quiet, the
node is not in check, the score is not a mate score and the bound agrees with the sign of the error.
