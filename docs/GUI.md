# Using Astra Chess in a chess GUI

Astra Chess speaks the standard **UCI** protocol, so it works with any UCI GUI.

## En Croissant
1. Get a binary: download `astra-windows-x86_64.exe` / `astra-linux-x86_64` / `astra-macos` from the
   GitHub *Releases* page (built by CI), or run `make` yourself.
2. En Croissant -> **Engines** -> **Add new** -> **Local** -> select the binary.
3. Open the engine's settings to change `Hash`, `MultiPV` (number of analysis lines), `Move Overhead`.
4. Use it for **analysis** (any number of lines via `MultiPV`) or play against it from the *Play* tab.

## Arena, CuteChess, Banksia, Lucas Chess, SCID, Lichess-bot, ...
Add a new UCI engine and point it to the executable. For tournaments, `Move Overhead` (default 10 ms)
should cover GUI/process latency; raise it if you see time losses.

## Options

| Option | Default | Meaning |
|--------|---------|---------|
| `Hash` | 64 | Transposition table size in MB (1-4096) |
| `Clear Hash` | - | Button: clear the hash table |
| `Threads` | 1 | Search threads (Lazy SMP, 1-64). Set to your physical core count; not combined with `MultiPV` > 1 |
| `MultiPV` | 1 | Number of principal variations reported (1-256) |
| `Move Overhead` | 10 | Milliseconds reserved per move for GUI/communication latency |
| `UseNNUE` | false | Use a loaded NNUE network (see `NNUE.md`) |
| `EvalFile` | empty | Path of an `ASTN` network file |
| `UCI_AnalyseMode` | false | Accepted for GUI compatibility; no behaviour change |

Not supported: Chess960, pondering, `searchmoves`.
