# Astra Chess 0.3

Astra Chess is a C++17 chess engine project with a UCI interface. Version 0.3 is a stronger search prototype, not a verified 2500-Elo engine.

## What's new in v0.3

- Iterative deepening and negamax alpha-beta search with transposition table.
- Principal-variation search and conservative late-move reductions.
- Killer-move and history-heuristic ordering for quiet moves.
- Quiescence search, capture/promotion ordering, and mate-distance score storage.
- Basic positional evaluation: material, piece activity/centralization, pawn advancement, doubled/isolated pawns, bishop pair, seventh-rank rooks and king pawn shelter.
- UCI search runs asynchronously, so `stop` and `quit` can interrupt it.
- Time management for `go movetime`, `wtime`, `btime`, `winc`, `binc`, and `movestogo`.
- Basic threefold-repetition path detection and fifty-move counter handling in search.
- Perft commands for move-generation regression tests.

## Build (Arch Linux)

```bash
sudo pacman -S --needed base-devel
make
make test
```

## Use with a UCI chess GUI

Select the compiled `astra` executable as the engine. From a terminal:

```text
uci
isready
position startpos
go depth 8
stop
quit
```

Time controls are accepted, for example:

```text
go wtime 300000 btime 300000 winc 2000 binc 2000
```

## Tests

```bash
./astra --perft 1  # 20
./astra --perft 2  # 400
./astra --perft 3  # 8902
./astra --perft 4  # 197281
make test
```

For a FEN position:

```bash
./astra --perft-fen 3 'FEN GOES HERE'
./astra --list-fen 'FEN GOES HERE'
```

## Important limitations

- **2500 Elo has not been demonstrated.** No strength rating is claimed until Astra is tested in controlled engine matches against rated opponents.
- The board uses a 0x88 mailbox rather than bitboards; legal move generation is not yet optimized for high performance.
- Evaluation is hand-written and basic; no trained NNUE network is included.
- Search is still missing several established techniques, including null-move pruning, aspiration windows, robust SEE, tablebases, and advanced draw handling.
- Hash size is fixed in this build. Repetition detection is a search-path approximation, not a complete tournament-grade repetition implementation.
- Tests cover start-position perft and UCI smoke checks; special-position perft and tactical suites are still needed.

## Roadmap toward serious playing strength

1. Verify move generation against a broad perft suite and differential testing.
2. Add null-move pruning, aspiration windows, SEE and more advanced pruning with regression tests.
3. Replace the mailbox move generator with bitboards and incremental position/hash updates.
4. Integrate or train an NNUE evaluator and tune search parameters with self-play.
5. Run thousands of games against known engines and estimate Elo with confidence intervals.
