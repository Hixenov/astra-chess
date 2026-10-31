#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One command to build an NNUE network for Astra Chess: parallel data generation -> training -> (optional) match.

Example (8-core PC, about an hour for ~8M positions):
    python tools/make_net.py ./astra.exe --games-per-proc 9000 --procs 6 --depth 6 --hidden 128 --epochs 40

Steps
  1. Runs `--procs` data generators in parallel (different seeds) writing data_<seed>.txt in --workdir.
  2. Concatenates them into data.txt (keep the file order: the trainer validates on the LAST 5 % = whole games).
  3. Trains with `astra --train-nnue` and writes net.nnue (best validation epoch).
  4. With --match N: plays N games of "NNUE net vs classical evaluation" with tools/match.py.

Resumable: existing data files are reused unless --fresh is given. Total positions ~ games * 90.
"""
import argparse
import os
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("engine", help="path to the Astra binary")
    ap.add_argument("--workdir", default="nnue_work")
    ap.add_argument("--procs", type=int, default=max(1, (os.cpu_count() or 2) - 1), help="parallel data generators")
    ap.add_argument("--games-per-proc", type=int, default=5000)
    ap.add_argument("--depth", type=int, default=6, help="search depth of the data generation games")
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--lambda", dest="lam", type=float, default=0.8, help="target blend: 1.0 = search score, 0.0 = game result")
    ap.add_argument("--threads", type=int, default=0, help="training threads (0 = all cores)")
    ap.add_argument("--match", type=int, default=0, help="games of net-vs-classical validation match (0 = skip)")
    ap.add_argument("--movetime", type=int, default=100)
    ap.add_argument("--fresh", action="store_true", help="ignore existing data files")
    args = ap.parse_args()

    engine = os.path.abspath(args.engine)
    os.makedirs(args.workdir, exist_ok=True)
    files = [os.path.join(args.workdir, f"data_{seed}.txt") for seed in range(1, args.procs + 1)]

    # 1. data generation
    procs = []
    for seed, path in zip(range(1, args.procs + 1), files):
        if args.fresh and os.path.exists(path):
            os.remove(path)
        if os.path.exists(path) and os.path.getsize(path) > 0:
            print(f"reusing {path}")
            continue
        print(f"datagen seed {seed}: {args.games_per_proc} games at depth {args.depth} -> {path}")
        procs.append(subprocess.Popen([engine, "--datagen", str(args.games_per_proc), str(args.depth), str(seed * 7919), path],
                                      stdout=subprocess.DEVNULL))
    for p in procs:
        if p.wait() != 0:
            sys.exit("data generation failed")

    # 2. concatenate (whole files, so games stay contiguous)
    data = os.path.join(args.workdir, "data.txt")
    positions = 0
    with open(data, "w") as out:
        for path in files:
            with open(path) as f:
                for line in f:
                    if line.count(";") >= 2 and line.endswith("\n"):  # drop a possibly truncated last line
                        out.write(line)
                        positions += 1
    print(f"{positions} training positions in {data}")
    if positions < 100000:
        print("warning: fewer than 100k positions - a network trained on so little data will not beat the classical evaluation")

    # 3. training
    net = os.path.join(args.workdir, "net.nnue")
    cmd = [engine, "--train-nnue", data, net, str(args.hidden), str(args.epochs), str(args.lam), str(args.threads)]
    print("training:", " ".join(cmd))
    if subprocess.call(cmd) != 0:
        sys.exit("training failed")

    # 4. optional validation match
    if args.match:
        here = os.path.dirname(os.path.abspath(__file__))
        mcmd = [sys.executable, os.path.join(here, "match.py"), engine, engine, "--opt1", "UseNNUE=true",
                "--opt1", f"EvalFile={os.path.abspath(net)}", "--games", str(args.match), "--movetime", str(args.movetime),
                "--concurrency", str(max(1, args.procs // 2))]
        print("validation match:", " ".join(mcmd))
        subprocess.call(mcmd)
    print(f"\nDone. Network: {net}\nUse: setoption name EvalFile value {os.path.abspath(net)} ; setoption name UseNNUE value true")


if __name__ == "__main__":
    main()
