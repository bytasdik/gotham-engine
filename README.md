# ♟ Gotham Engine

A chess engine written in C++17. Built from scratch as a learning project — no external libraries, no UCI protocol yet, just raw move generation, search, and evaluation in a single file.

---

## Quick Start

```bash
# Compile
g++ -std=c++17 -O2 -o gotham chess.cpp

# Play
./gotham
```

You play **White**. Enter moves in **Standard Algebraic Notation (SAN)** — the same notation used in chess books and tournaments:

```
e4          pawn to e4
Nf3         knight to f3
exd5        pawn on e-file captures on d5
O-O         castle kingside
O-O-O       castle queenside
e8=Q        pawn promotes to queen
Raxd1       rook on a-file captures d1 (disambiguation)
```

The engine responds in the same notation, with move numbers:

```
1. e4
1... Nc6
2. Nf3
2... Nf6
```

Type `quit` to exit.

---

## Project Structure

```
gotham/
├── chess.cpp     # Entire engine (single file)
├── README.md     # This file
└── DEVNOTES.md   # Deep-dive: every function explained
```

---

## What It Can Do

| Feature | Status |
|---|---|
| All standard piece moves | ✅ |
| Pawn double push | ✅ |
| En passant | ✅ |
| Pawn promotion (all 4 pieces) | ✅ |
| Castling (kingside + queenside) | ✅ |
| Standard Algebraic Notation input | ✅ |
| SAN output with `+` / `#` indicators | ✅ |
| Disambiguation (`Raxd1`, `N1f3`) | ✅ |
| Minimax with alpha-beta pruning | ✅ |
| Piece-square table evaluation | ✅ |
| Move ordering (captures first) | ✅ |
| Check/checkmate detection (for display) | ✅ |
| Legal move filtering (no moving into check) | ❌ (known limitation) |
| Stalemate detection | ❌ (known limitation) |
| UCI protocol | ❌ (planned) |
| Time management | ❌ (planned) |

---

## Known Limitations

**No illegal move filtering.** `king_in_check()` exists and works, but moves that leave the king in check are not yet filtered out of `legal_moves()`. The engine can move into check and won't stop you from doing the same. This is the next thing to fix.

**Castling through check.** The castling generator only checks that intermediate squares are empty, not whether they're attacked.

**Stalemate scored as loss.** With no legal moves, both checkmate and stalemate return `-INF`. The engine treats a drawn stalemate as badly as being mated, which is wrong.

**No draw detection.** No 50-move rule, no threefold repetition. The engine has no game history so it can repeat positions indefinitely.

---

## Changing Search Depth

Edit the constant near the top of `chess.cpp`:

```cpp
constexpr int SEARCH_DEPTH = 4;  // increase for stronger play, slower thinking
```

Depth 4 is fast (under 1 second per move). Depth 6 is noticeably stronger but takes a few seconds.

---

## Roadmap

See `DEVNOTES.md` for a full explanation of each system. Planned improvements in order:

1. Filter illegal moves (moves that leave king in check) — unlocks everything below
2. Fix stalemate scoring
3. Quiescence search (don't evaluate mid-capture positions)
4. Iterative deepening + time management
5. Transposition table (cache previously evaluated positions)
6. UCI protocol support