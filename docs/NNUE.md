# NNUE in Astra Chess

Astra Chess contains a complete NNUE toolchain: inference (architecture, file format, incremental updates),
data generation and a built-in trainer. **No trained network is bundled**, so by default the engine uses the
classical evaluation. A network that has not been trained plays nonsense; `--gen-test-net` only
produces random weights for plumbing tests.

## Architecture `PS768 -> H -> 1`

| Part            | Detail |
|-----------------|--------|
| Inputs          | 768 = (own, enemy) x (P N B R Q K) x 64 squares, per perspective; black's view mirrors the board vertically |
| Hidden layer    | `H <= 256` neurons per perspective, int16 accumulators, updated incrementally in `makeMove` |
| Activation      | clipped ReLU, `clamp(x, 0, QA)` with `QA = 255` |
| Output          | dot product of `[acc(stm) \| acc(not stm)]` (2H values) with int16 weights (`QB = 64`) |
| Scaling         | `eval_cp = (sum + outputBias) * 400 / (QA * QB)` |

`Position` keeps one accumulator per ply (`accs_`); `makeMove` copies the parent accumulator and applies the
add/sub of the moved, captured and promoted pieces; `unmakeMove` just pops it. `--selftest` verifies on
thousands of random positions that the incrementally updated accumulator equals a from-scratch refresh.

## File format `ASTN` v1 (little endian)

```
char[4] "ASTN" | u32 version=1 | u32 featureSet=1 | u32 hidden
i16 featureWeights[768][hidden]
i16 featureBias[hidden]
i16 outputWeights[2*hidden]      (first `hidden` = side to move, next `hidden` = opponent)
i32 outputBias
```

The loader rejects wrong magic/version/feature set, hidden sizes outside 1..256, truncated files and trailing bytes.

## Using a network

```
setoption name EvalFile value /path/to/net.nnue
setoption name UseNNUE value true
```

If `UseNNUE` is on but no valid network is loaded, the engine says so and falls back to the classical evaluation.

## Training a network (built in, no Python needed)

Astra Chess contains a dependency-free trainer for exactly this architecture.

```bash
# 1. Generate self-play data: "FEN;result;score" lines (quiet positions, search score + game result)
./astra --datagen 24000 5 1 data1.txt        # games depth seed outfile (run several seeds in parallel
./astra --datagen 24000 5 2 data2.txt        #  processes/machines, then: cat data*.txt > data.txt)

# 2. Train: data out.nnue [hidden=64] [epochs=40] [lambda=0.5] [threads=auto] [lr=0.004]
./astra --train-nnue data.txt net.nnue 64 40 0.7

# 3. Use it
#    setoption name EvalFile value net.nnue
#    setoption name UseNNUE value true
```

`lambda` blends the target: 1.0 = imitate the search score only, 0.0 = game result only.
The trainer prints the validation error against game results next to the **classical evaluation's error on
the same positions** - a net is only worth using if it beats that number.
After training it re-loads the quantised file through the engine's own inference code and reports the difference
to the float model (typically a few centipawns), which also verifies that feature indexing matches the engine.

### Lessons learned (please read)

* **Validate on whole games.** Neighbouring positions of one game are near-duplicates with the same result. A
  validation split that samples every n-th line leaks them into training and makes a net look far better than it
  is (I measured 0.034 vs a true 0.10+). The trainer holds out the **last 5 % of the file**; do not shuffle
  the file before training.
* **Data volume matters.** With ~270k positions from ~3000 games and result-only labels a 64-neuron net overfits and
  loses to the tuned classical evaluation. Use millions of positions and score-based targets.
* Judge the final net by **games** (`tools/match.py ... --opt1 UseNNUE=true --opt1 EvalFile=net.nnue`), not only by loss.

## Planned architecture upgrades

* King-bucketed input features (HalfKP-style) with accumulator refresh on king moves.
* SIMD (AVX2) accumulator updates and output layer; `-march=native` builds already auto-vectorise the loops.
* Deeper heads (e.g. 2 hidden layers) once data volume justifies it.
