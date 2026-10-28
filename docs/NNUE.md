# NNUE in Astra Chess

Astra Chess 0.5 contains the **inference side** of an NNUE evaluation (architecture, file format,
incremental updates, tests). **No trained network is bundled**, so by default the engine uses the
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

## Getting a *trained* network (the missing piece)

1. **Generate data**: self-play games with the classical eval at fixed depth/nodes, recording
   (FEN, search score, game result) tuples. (A `datagen` command is the natural next step; not in 0.5.)
2. **Train** with a PS768 trainer (for example, a small PyTorch script or an existing trainer such as
   `bullet`) using a blend of search score and game result as target. Quantise: feature weights/biases x `QA`,
   output weights x `QB`, output bias x `QA*QB`.
3. **Export** to the `ASTN` layout above and test with `--selftest` and a match against the classical eval:
   `python3 tools/match.py ./astra ./astra --opt1 UseNNUE=true --opt1 EvalFile=net.nnue --games 400`.

Do not ship someone else's network unless its licence allows it; train your own or check the licence first.

## Planned architecture upgrades

* King-bucketed input features (HalfKP-style) with accumulator refresh on king moves.
* SIMD (AVX2) accumulator updates and output layer; `-march=native` builds already auto-vectorise the loops.
* Deeper heads (e.g. 2 hidden layers) once data volume justifies it.
