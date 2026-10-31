# Notice

Astra Chess is free software, licensed under the **GNU General Public License v3.0 or later** (see `LICENSE`).
You may use, study, modify and redistribute it; if you distribute modified versions or binaries you must
make the corresponding source available under the same licence.

## Credits and influences

Astra Chess is an independent implementation, but its techniques are standard computer-chess knowledge and
several are modelled on the engines that inspired the project:

* **Stockfish** (GPL-3.0): the swap-based static exchange evaluation, the magic-bitboard initialisation
  scheme (random sparse magics with the per-rank seeds used there), the lockless transposition-table scheme,
  the idea of correction history, and general search/ordering ideas.
* **Velvet** and **Winter** (GPL-3.0): inspiration for NNUE layout and project structure.
* The Chess Programming Wiki, for bitboard, magic bitboard, SEE and Texel-tuning descriptions.

Because parts are modelled on GPL-3.0 engines, the project is licensed GPL-3.0-or-later as well.
This is not legal advice; if you need a different licence, rewrite those components independently first.
