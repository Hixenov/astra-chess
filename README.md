# Astra Chess 0.9

A UCI chess engine in C++17, developed in the spirit of
[Stockfish](https://github.com/official-stockfish/Stockfish), [Velvet](https://github.com/mhonert/velvet-chess)
and [Winter](https://github.com/rosenthj/Winter). Licensed under **GPL-3.0-or-later** (see `LICENSE`, `NOTICE.md`).

**No absolute Elo rating is claimed.** Strength has only been measured against earlier Astra versions; see
[`docs/ELO.md`](docs/ELO.md) for how to measure an absolute estimate (e.g. against Stockfish `UCI_Elo`).
In particular, nothing in this repository demonstrates a rating of 2900 or any other specific value.

## Use it in a GUI (En Croissant, Arena, CuteChess, ...)

Astra Chess speaks standard UCI. Build it (`make`) or download a binary from the GitHub *Releases* page
(built by CI for Linux, Windows and macOS), then add it as a **local UCI engine** in your GUI.
Supports `Threads` (multi-core search), `MultiPV` analysis, `Hash`, `Move Overhead`. Details: [`docs/GUI.md`](docs/GUI.md).

## Build and run

```bash
make            # or: make native (adds -march=native)   |   make static (static binary)
make test       # self-test + UCI protocol tests
./astra         # speaks UCI
./astra --bench 9
./astra --perft 5
```

## What's new in 0.9

| Area | Change |
|------|--------|
| **Transposition table** | 2-way buckets with 6-bit ageing and depth/age replacement; per-search generation; `hashfull` reports current-age entries |
| **Search** | Pawn-structure correction history (learned static-eval error, applied to pruning decisions) |
| **Time management** | Soft limit scaled by best-move stability and score drop between iterations |
| **Tests** | New TT unit test, Scholar's-mate search test, UCI suite updated |

Earlier versions: Lazy SMP, singular extensions, capture history, NNUE toolchain, Texel tuning, see `CHANGELOG.md`.

## Measured results (all against earlier Astra versions, single core)

| Test | Games | Result | Note |
|------|-------|--------|------|
| 0.9 vs 0.8-equivalent (legacy TT, no correction history, old time management), 40 ms/move | 160 | +42 =82 -36 | about +13 Elo, 95% CI -25..+51, LOS 75% (**not statistically confirmed**; no regression detected) |
| 0.8 candidate (singular extensions + capture history) vs 0.7 | 249 | +86 =97 -66 | about +28 Elo, 95% CI -6..+62 (not confirmed) |
| Texel-tuned weights vs untuned | 194 | +88 =41 -65 | about +41 Elo, 95% CI -2..+86, LOS ~97% (not yet confirmed) |

The 0.9 baseline was built from the 0.9 sources with the three 0.9 changes disabled, not from the original 0.8
binary. Self-play against a close relative overstates real gains, short time controls favour different ideas than
long ones, and nothing here anchors the engine to a rated scale.

## Using several cores

Set `Threads` to the number of physical cores (e.g. 6-8) in your GUI, and `Hash` to 256-1024 MB for long games.
To measure the benefit on your machine: `python tools/match.py ./astra ./astra --opt1 Threads=4 --opt2 Threads=1 --games 200 --movetime 100` (concurrency 1, so the engines do not fight for cores).

## Building a better NNUE network (the biggest lever for real strength)

```bash
python tools/make_net.py ./astra --procs 6 --games-per-proc 9000 --depth 6 --hidden 128 --epochs 40 --match 200
```
Runs in about an hour on 8 cores, produces `nnue_work/net.nnue`, and plays net-vs-classical. Use the net only if it wins.
Reaching a high rating needs a network trained on tens to hundreds of millions of positions (see `docs/NNUE.md`).

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

* Standard chess only (no Chess960), no Syzygy, no pondering, no `searchmoves`.
* SEE does not exclude absolutely pinned recapturers; promotions/en passant count as neutral.
* Some tuned weights are odd (e.g. a negative 2nd-rank passed-pawn middlegame bonus); they fit the data but are
  unproven in long games.
* The classical evaluation is simple; no trained NNUE network is bundled (a first net trained on 1.5M positions was
  not proven stronger). Absolute strength is unmeasured.

## Roadmap

1. Scale up NNUE: tens of millions of positions, larger hidden layer, king-bucketed features, SIMD; validate by games.
2. SPSA tuning of search constants; longer-time-control validation on a multi-core machine.
3. Gauntlet against rated engines to get an Elo anchor; Syzygy, pondering.
