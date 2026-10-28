# Astra Chess 0.4

Astra Chess is a C++17 chess engine with a UCI interface. This release improves root search and, importantly, adds standard-position perft regression tests that exercise castling, en passant, promotions, checks and pins. It is still a development engine; **2500 Elo is not claimed or verified**.

## What's new in v0.4

- Aspiration windows at the root, with widening on fail-low/fail-high.
- Fixed 0x88 edge-wrap errors in piece attacks and move generation.
- Fixed en-passant unmake logic that could leave a duplicate captured pawn on the board.
- Improved repetition history bookkeeping so the current position is not double-counted.
- Expanded perft regression suite with canonical Kiwipete and tactical/endgame positions.
- Keeps PVS, conservative late-move reductions, null-move pruning, killer moves, history heuristic, quiescence search, transposition table, timed asynchronous UCI search and `stop` support.

## Build on Arch Linux

```bash
sudo pacman -S --needed base-devel
make
make test
```

## UCI use

Configure `astra` as a UCI engine in your chess GUI. Quick terminal check:

```text
uci
isready
position startpos
go depth 10
stop
quit
```

Time controls are accepted, for example:

```text
go wtime 300000 btime 300000 winc 2000 binc 2000
```

## Perft regression tests

```bash
./astra --perft 4
./astra --perft-fen 3 'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1'
make test
```

The suite checks start-position counts through depth 4, Kiwipete through depth 3, and additional tactical positions. Expected canonical counts include start position depth 4 = 197281, Kiwipete depth 3 = 97862, and position 3 depth 3 = 2812.

## Limitations on the path to 2500 Elo

- **2500 Elo has not been demonstrated.** A rating requires controlled matches against rated engines with sufficient games and confidence intervals.
- Uses a 0x88 mailbox board, not bitboards; move generation is correct on the current perft suite but needs broader differential testing.
- Hand-crafted evaluation only; no trained NNUE network is bundled.
- No Syzygy tablebases, robust static exchange evaluation, configurable hash size, or complete tournament-grade draw/repetition adjudication.
- The repetition logic tracks the supplied move history and search path, but should be validated against a dedicated rule test suite before tournament use.
- Search and evaluation remain far below modern top-engine architecture and strength.

## Roadmap

1. Differential move-generation testing across a broad perft suite.
2. Bitboards and incremental Zobrist updates.
3. Static exchange evaluation and further search tuning.
4. Integrate a licensed, compatible NNUE network or train an original network.
5. Run self-play and matches against known engines, tune parameters, and estimate Elo statistically.
