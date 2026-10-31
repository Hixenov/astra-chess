#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Astra Chess match runner: plays UCI engines against each other and estimates the Elo difference.

Example:
    python3 tools/match.py ./astra ./astra-old --games 200 --movetime 100 --concurrency 2
    python3 tools/match.py ./astra ./astra-old --games 2000 --movetime 50 --sprt 0 5

Design notes
  * Games start from short opening lines (pairs of games with colours reversed).
  * Game rules (legality, mate, stalemate, 50-move, threefold, insufficient material) are
    arbitrated by an Astra binary (--arbiter, default: first engine), using its `status`
    command. Any engine that sends an illegal move loses the game.
  * Reported: W/D/L, score %, Elo difference with 95% confidence interval, LOS, and an
    optional SPRT (normal approximation of the trinomial GSPRT; stops early when decided).
  * Output is only as meaningful as the number of games: a few dozen games cannot
    distinguish engines of similar strength. Look at the confidence interval.
"""
import argparse
import math
import queue
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

OPENINGS = [
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6",
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4",
    "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6",
    "d2d4 g8f6 c2c4 e7e6 g1f3 d7d5",
    "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6",
    "c2c4 e7e5 b1c3 g8f6 g1f3 b8c6",
    "g1f3 d7d5 g2g3 g8f6 f1g2 e7e6",
    "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4",
    "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7",
    "e2e4 e7e5 g1f3 g8f6 f3e5 d7d6",
    "e2e4 d7d6 d2d4 g8f6 b1c3 g7g6",
    "d2d4 d7d5 g1f3 g8f6 c1f4 e7e6",
    "e2e4 g7g6 d2d4 f8g7 b1c3 d7d6",
    "c2c4 g8f6 b1c3 e7e6 e2e4 d7d5",
    "e2e4 e7e5 b1c3 g8f6 f1c4 f6e4",
    "d2d4 e7e6 e2e4 d7d5 b1d2 g8f6",
    "e2e4 c7c5 b1c3 b8c6 g2g3 g7g6",
    "d2d4 g8f6 g1f3 e7e6 c2c4 b7b6",
    "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4",
    "c2c4 c7c5 g1f3 g8f6 b1c3 e7e6",
]


class Engine:
    def __init__(self, path, name=None):
        self.path = path
        self.name = name or path
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1)
        # A reader thread feeds a queue: select() on a buffered text stream can miss lines
        # that are already inside Python's buffer.
        self.q = queue.Queue()
        threading.Thread(target=self._pump, daemon=True).start()
        self.send("uci")
        self.wait_for("uciok", 10)
        self.send("isready")
        self.wait_for("readyok", 10)

    def send(self, line):
        try:
            self.p.stdin.write(line + "\n")
            self.p.stdin.flush()
        except (BrokenPipeError, OSError):
            pass

    def _pump(self):
        for line in self.p.stdout:
            self.q.put(line)
        self.q.put("")  # EOF marker

    def readline(self, timeout):
        try:
            return self.q.get(timeout=max(0.0, timeout))
        except queue.Empty:
            return None  # timeout; "" means EOF

    def wait_for(self, prefix, timeout):
        end = time.time() + timeout
        while time.time() < end:
            line = self.readline(end - time.time())
            if line is None:
                break
            if line == "":
                raise RuntimeError(f"{self.name}: engine exited")
            if line.startswith(prefix):
                return line.strip()
        raise RuntimeError(f"{self.name}: timeout waiting for '{prefix}'")

    def setoption(self, name, value):
        self.send(f"setoption name {name} value {value}")

    def new_game(self):
        self.send("ucinewgame")
        self.send("isready")
        self.wait_for("readyok", 10)

    def bestmove(self, moves, go_cmd, timeout):
        """Returns (move, last_cp_score or None). move is None on timeout/crash."""
        self.send("position startpos" + (" moves " + " ".join(moves) if moves else ""))
        self.send(go_cmd)
        end = time.time() + timeout
        score = None
        while time.time() < end:
            line = self.readline(end - time.time())
            if line is None:
                break
            if line == "":
                return None, score
            if line.startswith("info") and " score " in line:
                t = line.split()
                try:
                    i = t.index("score")
                    if t[i + 1] == "cp":
                        score = int(t[i + 2])
                    elif t[i + 1] == "mate":
                        score = 30000 if int(t[i + 2]) > 0 else -30000
                except (ValueError, IndexError):
                    pass
            elif line.startswith("bestmove"):
                parts = line.split()
                return (parts[1] if len(parts) > 1 else None), score
        return None, score

    def close(self):
        try:
            self.send("quit")
            self.p.wait(timeout=2)
        except Exception:
            self.p.kill()


def arbiter_status(arb, moves):
    arb.send("position startpos" + (" moves " + " ".join(moves) if moves else ""))
    arb.send("status")
    line = arb.wait_for("status", 5)
    return line.split()[1]


def play_game(path_white, path_black, opening, go_cmd, movetime_ms, arbiter_path, options, max_plies=400):
    """Returns (result, reason, moves). result: '1-0', '0-1', '1/2-1/2' from White's view."""
    w, b, arb = Engine(path_white), Engine(path_black), Engine(arbiter_path)
    try:
        for e, opts in ((w, options[0]), (b, options[1])):
            for k, v in opts:
                e.setoption(k, v)
            e.new_game()
        moves = opening.split()
        losing_streak = {0: 0, 1: 0}
        drawish = 0
        timeout = movetime_ms / 1000.0 + 5.0
        side = len(moves) % 2  # 0 = white to move
        while len(moves) < max_plies:
            st = arbiter_status(arb, moves)
            if st == "checkmate":
                return ("0-1" if side == 0 else "1-0"), "checkmate", moves
            if st != "ongoing":
                return "1/2-1/2", st, moves
            eng = w if side == 0 else b
            mv, score = eng.bestmove(moves, go_cmd, timeout)
            if mv is None or mv == "0000":
                return ("0-1" if side == 0 else "1-0"), "no move / timeout / crash", moves
            moves.append(mv)
            if arbiter_status(arb, moves) == "illegal":
                moves.pop()
                return ("0-1" if side == 0 else "1-0"), f"illegal move {mv}", moves
            # Adjudication (keeps matches fast)
            if score is not None:
                losing_streak[side] = losing_streak[side] + 1 if score <= -900 else 0
                if losing_streak[side] >= 4:
                    return ("0-1" if side == 0 else "1-0"), "adjudicated (score <= -9.00)", moves
                drawish = drawish + 1 if abs(score) <= 5 else 0
                if drawish >= 24 and len(moves) > 100:
                    return "1/2-1/2", "adjudicated (flat 0.00 for 12 moves each)", moves
            side ^= 1
        return "1/2-1/2", "move limit", moves
    finally:
        for e in (w, b, arb):
            e.close()


# ----------------------------------------------------------------------------- statistics

def elo_from_score(s):
    s = min(max(s, 1e-6), 1 - 1e-6)
    return -400.0 * math.log10(1.0 / s - 1.0)


def score_from_elo(elo):
    return 1.0 / (1.0 + 10 ** (-elo / 400.0))


def phi(x):
    return 0.5 * (1.0 + math.erf(x / math.sqrt(2.0)))


def stats(w, d, l):
    n = w + d + l
    if n == 0:
        return None
    s = (w + 0.5 * d) / n
    var = (w * (1 - s) ** 2 + d * (0.5 - s) ** 2 + l * (0 - s) ** 2) / n  # per-game variance
    if w == 0 or l == 0 and d == 0:  # shut-out: add half a game each way (continuity correction)
        s = (w + 0.5 * d + 0.25) / (n + 0.5)
        var = max(var, 0.25 / (n + 1))
    se = math.sqrt(var / n) if n > 1 else 0.5
    lo, hi = s - 1.96 * se, s + 1.96 * se
    los = phi((w - l) / math.sqrt(w + l)) if (w + l) > 0 else 0.5
    return dict(n=n, score=s, elo=elo_from_score(s), elo_lo=elo_from_score(lo), elo_hi=elo_from_score(hi), los=los, var=var)


def sprt_llr(w, d, l, elo0, elo1):
    n = w + d + l
    if n == 0 or w + l == 0:
        return 0.0
    s = (w + 0.5 * d) / n
    var = (w * (1 - s) ** 2 + d * (0.5 - s) ** 2 + l * (0 - s) ** 2) / n
    if var <= 0:
        return 0.0
    s0, s1 = score_from_elo(elo0), score_from_elo(elo1)
    return (s1 - s0) * (2 * s - s0 - s1) * n / (2 * var)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("engine1", help="path to engine 1 (the one being tested)")
    ap.add_argument("engine2", help="path to engine 2 (the reference)")
    ap.add_argument("--games", type=int, default=100, help="number of games (rounded up to an even number)")
    ap.add_argument("--movetime", type=int, default=100, help="milliseconds per move")
    ap.add_argument("--depth", type=int, default=0, help="fixed depth instead of movetime")
    ap.add_argument("--concurrency", type=int, default=1)
    ap.add_argument("--arbiter", default=None, help="Astra 0.5+ binary used as rules arbiter (default: engine1)")
    ap.add_argument("--opt1", action="append", default=[], metavar="NAME=VALUE", help="UCI option for engine 1")
    ap.add_argument("--opt2", action="append", default=[], metavar="NAME=VALUE", help="UCI option for engine 2")
    ap.add_argument("--sprt", nargs=2, type=float, metavar=("ELO0", "ELO1"), help="stop early with SPRT (alpha=beta=0.05)")
    ap.add_argument("--out", default=None, help="write all games (UCI move lists) to this file")
    args = ap.parse_args()

    arbiter = args.arbiter or args.engine1
    opts1 = [tuple(o.split("=", 1)) for o in args.opt1]
    opts2 = [tuple(o.split("=", 1)) for o in args.opt2]
    go_cmd = f"go depth {args.depth}" if args.depth else f"go movetime {args.movetime}"
    movetime_ms = args.movetime if not args.depth else 5000

    # Validate openings with the arbiter
    arb = Engine(arbiter)
    openings = []
    for line in OPENINGS:
        try:
            if arbiter_status(arb, line.split()) == "ongoing":
                openings.append(line)
        except RuntimeError:
            pass
    arb.close()
    if not openings:
        sys.exit("no valid openings (is --arbiter an Astra 0.5+ binary?)")

    pairs = (args.games + 1) // 2
    jobs = []
    for i in range(pairs):
        op = openings[i % len(openings)]
        jobs.append((i, 0, op))  # engine1 white
        jobs.append((i, 1, op))  # engine1 black

    w = d = l = 0
    done = 0
    lock = threading.Lock()
    log = open(args.out, "w") if args.out else None
    llr_lo, llr_hi = math.log(0.05 / 0.95), math.log(0.95 / 0.05)
    stop = threading.Event()

    def run(job):
        if stop.is_set():
            return None
        idx, colour, op = job
        paths = (args.engine1, args.engine2) if colour == 0 else (args.engine2, args.engine1)
        options = (opts1, opts2) if colour == 0 else (opts2, opts1)
        res, reason, moves = play_game(paths[0], paths[1], op, go_cmd, movetime_ms, arbiter, options)
        return job, res, reason, moves

    print(f"Match: {args.engine1} vs {args.engine2} | {len(jobs)} games | {go_cmd} | concurrency {args.concurrency}")
    with ThreadPoolExecutor(max_workers=args.concurrency) as ex:
        futs = [ex.submit(run, j) for j in jobs]
        for f in as_completed(futs):
            out = f.result()
            if out is None:
                continue
            (idx, colour, op), res, reason, moves = out
            e1_score = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}[res]
            if colour == 1:
                e1_score = 1.0 - e1_score
            with lock:
                done += 1
                if e1_score == 1.0: w += 1
                elif e1_score == 0.0: l += 1
                else: d += 1
                st = stats(w, d, l)
                tag = "W" if e1_score == 1 else ("L" if e1_score == 0 else "D")
                line = (f"[{done:4d}/{len(jobs)}] {tag} ({'white' if colour == 0 else 'black'}) {reason:<32} "
                        f"+{w} ={d} -{l}  score {st['score'] * 100:5.1f}%  Elo {st['elo']:+6.1f} "
                        f"[{st['elo_lo']:+.0f},{st['elo_hi']:+.0f}]")
                if args.sprt:
                    llr = sprt_llr(w, d, l, *args.sprt)
                    line += f"  LLR {llr:+.2f} ({llr_lo:.2f},{llr_hi:.2f})"
                    if llr >= llr_hi or llr <= llr_lo:
                        stop.set()
                print(line, flush=True)
                if log:
                    log.write(f"{res} {reason} | {' '.join(moves)}\n")
                    log.flush()

    st = stats(w, d, l)
    print("\n=== Result ===")
    if not st:
        print("no games finished")
        return
    print(f"{args.engine1} vs {args.engine2}: +{w} ={d} -{l}  ({st['n']} games)")
    print(f"Score {st['score'] * 100:.1f}%   Elo difference {st['elo']:+.1f}  (95% CI {st['elo_lo']:+.1f} .. {st['elo_hi']:+.1f})   LOS {st['los'] * 100:.1f}%")
    if args.sprt:
        llr = sprt_llr(w, d, l, *args.sprt)
        verdict = "H1 accepted (improvement)" if llr >= llr_hi else "H0 accepted (no improvement)" if llr <= llr_lo else "inconclusive"
        print(f"SPRT [{args.sprt[0]}, {args.sprt[1]}]: LLR {llr:+.2f} -> {verdict}")
    if st["n"] < 200:
        print("Note: fewer than ~200 games; treat the Elo estimate as indicative only.")


if __name__ == "__main__":
    main()
