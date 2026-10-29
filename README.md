# Astra Chess 0.7

A UCI chess engine in C++17, developed in the spirit of
[Stockfish](https://github.com/official-stockfish/Stockfish), [Velvet](https://github.com/mhonert/velvet-chess)
and [Winter](https://github.com/rosenthj/Winter). Licensed under **GPL-3.0-or-later** (see `LICENSE`, `NOTICE.md`).

**No absolute Elo rating is claimed.** Strength has only been measured against earlier Astra versions; see [`docs/ELO.md`](docs/ELO.md) for how to measure an absolute estimate (e.g. against Stockfish `UCI_Elo`).

## Use it in a GUI (En Croissant, Arena, CuteChess, ...)

Astra Chess speaks standard UCI. Build it (`make`) or download a binary from the GitHub *Releases* page
(built by CI for Linux, Windows and macOS), then add it as a **local UCI engine** in your GUI.
Supports `MultiPV` analysis, `Hash`, `Move Overhead`. Details: [`docs/GUI.md`](docs/GUI.md).

> I tested the protocol with scripted sessions (including Windows CRLF line endings, MultiPV, `stop` while
> thinking, `isready` during a search). It has not been run inside every GUI.

## Build and run

```bash
make            # or: make native (adds -march=native)   |   make static (static binary)
make test       # self-test + UCI protocol tests
./astra         # speaks UCI
./astra --bench 9
./astra --perft 5
```

## What's in 0.7 / 0.6

| Area | Change |
|------|--------|
| **Bitboards** (0.5) | 12 piece/colour bitboards + mailbox, magic bitboards, incremental Zobrist, fast legality via `checkers`/`pinned` |
| **SEE** (0.5) | Swap algorithm with x-rays; used for capture ordering, quiescence and pruning |
| **NNUE-ready** (0.5) | `PS768 -> H -> 1` network, file format, loader, incremental accumulators, `UseNNUE`/`EvalFile` (no trained net bundled, see `docs/NNUE.md`) |
| **Evaluation** | Threats, knight outposts, king-zone attack weights; all ~70 weights are now parameters, **Texel-tuned** on 285k self-play positions |
| **Search** | TT in quiescence, countermove + continuation history, internal iterative reduction, history-adjusted LMR |
| **GUI support** | `MultiPV` (sorted lines), `Move Overhead`, CRLF-safe input, serialised output, `go mate`, release binaries via CI |
| **NNUE training** (0.7) | Built-in dependency-free trainer `--train-nnue` (sparse inputs, Adam, multithreaded, quantised export, engine cross-check); `--datagen` now records search scores |
| **Tools** | `tools/match.py` (matches, Elo CI, LOS, SPRT), `--datagen` (self-play data), `--tune` (Texel tuner) |

## Measured results (all against earlier Astra versions, 40 ms/move, single core)

| Test | Games | Result | Note |
|------|-------|--------|------|
| 0.5 vs 0.4 | 100 | +100 =0 -0 | shut-out, Elo not estimable |
| Eval terms (threats/outposts/king safety) vs 0.5 | 325 | +125 =115 -85 | about +43 Elo, 95% CI +13..+74 |
| Search additions (TT in qsearch, countermove, cont. history, IIR) | 117 | +34 =51 -32 | indistinguishable from 0 |
| Texel-tuned weights vs untuned | 194 | +88 =41 -65 | about +41 Elo, 95% CI -2..+86, LOS ~97% (**not yet statistically confirmed**) |

Self-play against a close relative overstates real gains, short time controls favour different ideas than long
ones, and nothing here anchors the engine to a rated scale. Reproduce / extend with `tools/match.py`.

## Reproducing the tuning

```bash
./astra --datagen 3000 7 77 data.txt          # ~10 min: games at depth 7 -> quiet positions + results
./astra --tune data.txt 6 tuned_params.txt    # coordinate descent, resumable
```
The shipped values are in `tools/tuned_params.txt`.

## Measuring strength

```bash
cp astra astra-old                                   # build the baseline first
python3 tools/match.py ./astra ./astra-old --games 1000 --movetime 100 --concurrency 4 --sprt 0 10
```

## Known limitations

* Standard chess only (no Chess960), single search thread, no Syzygy, no pondering, no `searchmoves`.
* SEE does not exclude absolutely pinned recapturers; promotions/en passant count as neutral.
* Some tuned weights are odd (e.g. a negative 2nd-rank passed-pawn middlegame bonus); they fit the data but are
  unproven in long games.
* No trained NNUE network is bundled: a first net (H=64, 1.5M positions) predicts game results better than the classical evaluation on held-out games (error 0.098 vs 0.105) but won the match only by an unconfirmed margin (132 games, +59 =22 -51, 95% CI about -33..+76 Elo). Absolute strength is unmeasured.

## Roadmap

1. Scale up NNUE: tens of millions of positions, larger hidden layer, then validate by games (pipeline in `docs/NNUE.md`; a first net trained on 1.5M positions is not yet proven stronger than the classical evaluation).
2. SPSA tuning of search constants; longer-time-control validation on a multi-core machine.
3. Gauntlet against rated engines to get an Elo anchor; Lazy-SMP, Syzygy, king-bucketed NNUE features.
