# Astra Chess 0.5

A UCI chess engine in C++17, built as a learning/development project in the spirit of
[Stockfish](https://github.com/official-stockfish/Stockfish), [Velvet](https://github.com/mhonert/velvet-chess)
and [Winter](https://github.com/rosenthj/Winter). **No Elo rating is claimed**: strength has only been compared
against earlier Astra versions (see below), not against rated engines.

## What's new in 0.5

| Area | Change |
|------|--------|
| **Bitboards** | New board representation (12 piece/colour bitboards + mailbox), **magic bitboards** for sliders (magics found at start-up), incremental Zobrist hashing, fast legality via `checkers`/`pinned` instead of make+test, bulk-counted perft |
| **SEE** | Swap-algorithm Static Exchange Evaluation with x-rays. Used for capture ordering, quiescence filtering and SEE pruning in the main search |
| **NNUE-ready** | `PS768 -> H -> 1` network: file format, loader, incrementally updated accumulators in make/unmake, `UseNNUE` / `EvalFile` UCI options. **No trained network is bundled** - classical evaluation is the default (see `docs/NNUE.md`) |
| **Matches** | `tools/match.py`: automatic engine-vs-engine matches, opening pairs with colours swapped, Elo +- 95% CI, LOS, SPRT |
| **Search** | Real PV and `seldepth`/`hashfull` output, `Hash` option, reverse futility, futility/late-move pruning, check extension, history malus, mate-distance pruning |
| **Tests** | `--selftest`: perft (incl. en-passant edge cases), SEE values, magic check, hashing & legality cross-checks on ~18k random-game positions, NNUE incremental == refresh, repetition rules, search sanity. CI + sanitizer build |

## Build and run

```bash
make            # or: make native   (adds -march=native)
make test       # self-test + UCI tests
./astra         # speaks UCI; use it in any GUI (Arena, CuteChess, Banksia, ...)
./astra --bench 9
./astra --perft 5
```

UCI options: `Hash` (MB), `Clear Hash`, `UseNNUE`, `EvalFile`.
Extra commands: `d`, `eval`, `perft N`, `bench N`, `status` (used by the match tool).

## Measuring strength

```bash
cp astra astra-old                       # build the baseline first
# ... make changes, rebuild ...
python3 tools/match.py ./astra ./astra-old --games 400 --movetime 100 --concurrency 2
python3 tools/match.py ./astra ./astra-old --games 4000 --movetime 50 --sprt 0 5   # stops when decided
```

**Result of 0.5 vs 0.4** (50 ms/move, 100 games, single core, 20 openings with colours swapped):
**+100 =0 -0**. A shut-out cannot be converted into a meaningful Elo number; the 95% lower bound is
roughly +750 Elo, so 0.5 is vastly stronger than 0.4. This says nothing about absolute strength.
Speed: about 1.6 M nodes/s in `--bench` on the development machine versus about 0.5 M for 0.4
(different search, so not an apples-to-apples comparison).

## Known limitations

* Standard chess only (no Chess960), single search thread, no Syzygy, no pondering.
* SEE does not exclude absolutely pinned recapturers; promotions/en passant are treated as neutral.
* Evaluation is a compact hand-crafted function with generated piece-square terms - not tuned.
* The NNUE network is not trained yet; the time-management logic is simple.
* Absolute playing strength has not been measured against rated engines.

## Roadmap

1. **Self-play data generation** (`datagen`) and a training pipeline -> first trained NNUE net (see `docs/NNUE.md`).
2. **Parameter tuning** from self-play (SPSA) for search constants (`RFP`, `LMR`, futility margins) and eval terms.
3. Gauntlet against rated engines (e.g. Stockfish with `UCI_LimitStrength`) to anchor an Elo estimate.
4. Lazy-SMP multithreading, Syzygy probing, king-bucketed NNUE features + AVX2.
