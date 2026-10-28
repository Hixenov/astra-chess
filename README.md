# Astra Chess 0.2

An educational C++17 UCI chess engine using a 0x88 mailbox board.

## Features in v0.2

- Legal move generation, including castling, en passant and promotions.
- UCI commands: `uci`, `isready`, `ucinewgame`, `position`, `go depth N`, `stop`, `quit`.
- Negamax alpha-beta search with iterative deepening.
- Quiescence search to reduce horizon effects around captures.
- Deterministic Zobrist hashing and a transposition table.
- Basic material/positional evaluation and capture/promotion ordering.
- Perft mode for move-generation testing.
- Fifty-move counter draw handling in search.

## Build (Arch Linux)

```bash
sudo pacman -S --needed base-devel
make
make test
```

## Run

```bash
./astra
```

Then send UCI commands:

```text
uci
isready
position startpos
go depth 4
quit
```

Set up a position from FEN and apply moves:

```text
position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 moves e2e4 e7e5
go depth 4
```

## Tests

```bash
./astra --perft 1  # 20
./astra --perft 2  # 400
./astra --perft 3  # 8902
./astra --perft 4  # 197281
make test
```

## Known limitations

- Search is synchronous; `stop` cannot interrupt an active search yet.
- `go` supports depth limits only, not clocks or node limits.
- The Hash UCI option and configurable TT sizing are not implemented yet.
- Repetition detection and complete draw adjudication are not implemented.
- Evaluation is intentionally basic; this is a learning engine, not a Stockfish-strength engine.
