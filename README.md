# Astra Chess 0.1

A first C++17 chess engine prototype with a UCI command loop, legal move generation (0x88 mailbox), basic material/positional evaluation, negamax alpha-beta search, and a perft mode.

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
Then send UCI commands such as:

```text
uci
isready
position startpos
go depth 4
quit
```

Load a position from FEN and apply moves:

```text
position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 moves e2e4 e7e5
go depth 3
```

## Tests

```bash
./astra --perft 1
./astra --perft 2
./astra --perft 3
make test
```
Expected start-position perft values: 20, 400, 8902.

## Current limitations

This is an educational baseline, not yet a competitive engine. Search currently uses a fixed depth (default 4; maximum 8), has no time management, transposition table, repetition detection, fifty-move draw handling, or robust UCI option support. The UCI `stop` command is currently a no-op. The reported `score cp` is the static evaluation of the root position, not the searched score. These are explicit next milestones.
