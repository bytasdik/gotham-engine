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

You play **White**. Enter moves in UCI long algebraic notation:

```
e2e4        pawn to e4
g1f3        knight to f3
e7e8q       pawn promotes to queen
```

Type `quit` to exit.

---

## Project Structure

```
gotham/
├── chess.cpp          # Entire engine (single file)
├── README.md          # This file
└── DEVNOTES.md        # Deep-dive: every function explained
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
| Minimax with alpha-beta pruning | ✅ |
| Piece-square table evaluation | ✅ |
| Move ordering (captures first) | ✅ |
| Check detection | ❌ (known limitation) |
| Stalemate detection | ❌ (known limitation) |
| UCI protocol | ❌ (planned) |
| Time management | ❌ (planned) |

---

## Known Limitations

**No check detection.** The engine will not stop you from moving into check, and it won't detect checkmate vs stalemate — both look like "no moves available" and score identically as `-INF`. This is the biggest gap to fix next.

**Castling through check.** The castling generator only checks whether intermediate squares are empty, not whether they're attacked.

**No repetition or 50-move rule.** The engine has no game history, so it can't detect draws.

---

## Changing Search Depth

Edit the constant near the top of `chess.cpp`:

```cpp
constexpr int SEARCH_DEPTH = 4;  // increase for stronger play, slower thinking
```

Depth 4 is fast (< 1 second per move). Depth 6 is noticeably stronger but takes a few seconds.

---

## Roadmap

See `DEVNOTES.md` for a full explanation of each system. Planned improvements:

1. Add check/checkmate detection
2. Iterative deepening
3. Quiescence search (stop evaluating mid-capture)
4. Transposition table (cache positions)
5. UCI protocol support
