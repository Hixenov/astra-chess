# How to measure Astra Chess's absolute strength

Matches between Astra versions only show *relative* progress. To anchor an Elo number you need opponents with a
known rating. The cheapest source is **Stockfish's strength limiter**.

## Gauntlet against Stockfish (UCI_Elo)

```bash
# stockfish must be installed; Astra (>= 0.5) is used as the rules arbiter
python3 tools/match.py ./astra stockfish --games 200 --movetime 100 --concurrency 4 \
    --opt2 UCI_LimitStrength=true --opt2 UCI_Elo=2000
```

1. Start at `UCI_Elo=2000` and read the score. Move the level up/down (Stockfish allows about 1320-3190) until
   Astra scores **40-60 %** - extreme scores carry almost no information.
2. Estimate: `Astra Elo ~ UCI_Elo + "Elo difference" printed by match.py`. Use at least 200 games per level and
   report the 95 % interval that `match.py` prints.
3. Repeat at two or three levels; the estimates should agree. If they do not, trust the level closest to 50 %.

## Caveats (read before quoting a number)

* Stockfish's `UCI_Elo` is calibrated for specific conditions (CCRL-style scale, long time controls, particular
  hardware). With short fixed `movetime` the mapping shifts, so the result is an **estimate on your setup**, not a
  CCRL rating. Say so when you publish it, e.g. "~X Elo vs Stockfish UCI_Elo, 100 ms/move, N games".
* Use the same time control and hardware for all levels, and avoid running other heavy programs meanwhile.
* For a rating comparable to rating lists, submit the release binary to a testing group (e.g. CCRL/CEGT)
  or play a gauntlet against several engines with published ratings using cutechess-cli/fastchess.
