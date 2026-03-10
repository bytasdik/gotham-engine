# Gotham Engine — Developer Notes

This document explains how every part of `chess.cpp` works, why decisions were made, and what the known gaps are. The goal is to give you a full map of the code before you start modifying it.

---

## Table of Contents

1. [Board Representation](#1-board-representation)
2. [The Piece Struct](#2-the-piece-struct)
3. [The Move Struct](#3-the-move-struct)
4. [Piece-Square Tables (PST namespace)](#4-piece-square-tables)
5. [The Board Class](#5-the-board-class)
6. [Move Generation — How It Works](#6-move-generation)
7. [The Engine Class — Search](#7-the-engine-class)
8. [Evaluation](#8-evaluation)
9. [Input Parsing](#9-input-parsing)
10. [Known Bugs and Gaps](#10-known-bugs-and-gaps)
11. [Where to Improve Next](#11-where-to-improve-next)

---

## 1. Board Representation

The board is a flat `std::array<Piece, 64>`. Square numbering goes:

```
56 57 58 59 60 61 62 63   ← rank 8 (Black's back rank)
48 49 50 51 52 53 54 55
40 41 42 43 44 45 46 47
32 33 34 35 36 37 38 39
24 25 26 27 28 29 30 31
16 17 18 19 20 21 22 23
 8  9 10 11 12 13 14 15
 0  1  2  3  4  5  6  7   ← rank 1 (White's back rank)
```

So square `0` = a1, `7` = h1, `63` = h8.

**Converting between square index and row/col:**

```cpp
int row = sq / 8;   // 0 = rank 1, 7 = rank 8
int col = sq % 8;   // 0 = file a, 7 = file h
```

This is used constantly in move generation to do boundary checking (e.g. "don't wrap a knight from h-file to a-file").

**Why a flat array?** It's the simplest representation that works. An 8×8 2D array would also work but makes direction math slightly messier. Alternatives like bitboards (one 64-bit integer per piece type) are far more performant but much harder to write.

---

## 2. The Piece Struct

```cpp
struct Piece {
    PieceType type  = NONE;
    Color     color = NO_COLOR;
};
```

A piece is just two enums. An empty square is `{NONE, NO_COLOR}`.

- `empty()` checks `type == NONE`
- `symbol()` returns the display character: uppercase for White (`P N B R Q K`), lowercase for Black (`p n b r q k`)

**Enums used:**

```cpp
enum Color     { WHITE = 0, BLACK = 1, NO_COLOR = 2 };
enum PieceType { NONE = 0, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
```

`NONE = 0` is intentional — it means a zero-initialized `Piece{}` is already an empty square.

---

## 3. The Move Struct

```cpp
struct Move {
    int   from      = 0;
    int   to        = 0;
    Piece captured  = {};    // empty if quiet move
    Piece promotion = {};    // empty unless pawn reaches back rank
    bool  is_castle = false;
    bool  is_ep     = false; // en passant flag
};
```

Moves carry all the information needed to **apply and undo** them. This is called a "fat move" design. The alternative — a "thin move" that only stores `from`/`to` — would be smaller, but you'd need to look up captured pieces at apply time.

`uci()` serializes the move to UCI string format (`e2e4`, `e7e8q`). The promotion piece type is appended as a lowercase letter if present.

---

## 4. Piece-Square Tables

```cpp
namespace PST {
    constexpr std::array<int,64> pawn = { ... };
    // knight, bishop, rook, queen, king...

    constexpr int piece_value[7] = {0, 100, 320, 330, 500, 900, 20000};

    int score(Piece p, int sq) { ... }
}
```

Piece-square tables (PSTs) add positional bonuses on top of raw material value. For example, a knight in the center scores higher than a knight on the edge. Values are in centipawns (100 = 1 pawn).

**The PST index trick:** Tables are written from White's perspective (rank 8 at the top of the array, rank 1 at the bottom). For Black pieces, the index is mirrored:

```cpp
int idx = (p.color == WHITE) ? (63 - sq) : sq;
```

For White, `sq=0` (a1) maps to `idx=63` (bottom-left of the PST). For Black, `sq=0` (a1) is already the "far end" of the board, so no flip is needed.

**Material values:**

| Piece  | Centipawns |
|--------|-----------|
| Pawn   | 100       |
| Knight | 320       |
| Bishop | 330       |
| Rook   | 500       |
| Queen  | 900       |
| King   | 20000     |

King value is huge (20000) so the engine will never sacrifice it even in weird positions. In real engines the king is often handled separately (no PST score, and checkmate is tracked explicitly).

---

## 5. The Board Class

```cpp
class Board {
public:
    std::array<Piece, 64> squares{};
    Color   side_to_move  = WHITE;
    int     ep_square     = -1;
    uint8_t castle_rights = 0xF;
    ...
};
```

The `Board` stores the full game state needed to generate and apply moves:

- `squares` — the 64 piece slots
- `side_to_move` — whose turn it is
- `ep_square` — the target square of a possible en passant capture, or `-1`. Set after any double pawn push; cleared after every other move.
- `castle_rights` — a 4-bit mask:
  - bit 0 = White kingside
  - bit 1 = White queenside
  - bit 2 = Black kingside
  - bit 3 = Black queenside

**`apply()` returns a new Board** rather than mutating in place. This is the "copy-make" approach. It's slightly slower than "make/unmake" (which mutates and restores), but much easier to reason about and debug. Since we're doing a tree search, every node gets its own copy.

**`starting_position()`** is a static factory that builds the initial board layout. The back rank order is `{ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK}`.

---

## 6. Move Generation

`legal_moves()` iterates every square, finds pieces belonging to `side_to_move`, and dispatches to a type-specific generator.

### Sliding Pieces: `gen_slider()`

```cpp
void gen_slider(int sq, std::vector<Move>& moves, int dstart, int dend) const
```

Bishops use directions 4–7 (diagonals), Rooks use 0–3 (orthogonals), Queens use all 8:

```cpp
case BISHOP: gen_slider(sq, moves, 4, 8); break;
case ROOK:   gen_slider(sq, moves, 0, 4); break;
case QUEEN:  gen_slider(sq, moves, 0, 8); break;
```

The direction table:
```
Index: 0    1    2    3    4    5    6    7
dr:    +1   -1    0    0   +1   +1   -1   -1
dc:     0    0   +1   -1   +1   -1   +1   -1
```

For each direction, the slider steps outward until it hits the board edge, a friendly piece (stop, don't add), or an enemy piece (add capture, then stop).

### Knights: `gen_knight()`

Uses a fixed delta table of the 8 L-shaped jumps. Boundary-checked with row/col arithmetic before adding.

### Pawns: `gen_pawn()`

The most complex generator because pawns have asymmetric movement:

- `dir = +1` for White (moves up), `-1` for Black (moves down)
- Single push: one square forward if empty
- Double push: two squares forward from starting rank (rank 2 for White = row index 1, rank 7 for Black = row index 6), only if both squares are empty
- Captures: one square diagonally forward, only if enemy piece present
- En passant: diagonal capture to `ep_square` if set, sets `is_ep = true`
- Promotion: if the destination is the back rank (`promo_row`), generate 4 moves (one per promotion piece type)

### King: `gen_king()`

8-direction one-step moves, plus castling. Castling is simplified — it only checks that the intermediate squares are empty, **not** whether the king passes through check. This is a known bug.

```cpp
// White kingside: king on e1 (sq=4) moves to g1 (sq=6)
if ((castle_rights & 1) && squares[5].empty() && squares[6].empty())
```

### `try_add()`

All generators call `try_add()` which does a single safety check: don't add a move that captures a friendly piece. This catches edge cases in the slider/knight generators.

---

## 7. The Engine Class

```cpp
class Engine {
    int max_depth_;
    int alpha_beta(const Board&, int depth, int alpha, int beta);
public:
    Move best_move(const Board&);
};
```

### `best_move()`

The root search. Iterates all moves, calls `alpha_beta` on each resulting board, and picks the highest score. The negation (`-alpha_beta(child, ...)`) is part of the **negamax** pattern — see below.

### Alpha-Beta Search

This is minimax with alpha-beta pruning. The key insight of **negamax** is that `score for the side to move = -score for the other side`. So instead of writing separate `maximize` and `minimize` functions, every node just negates the child score.

```cpp
int score = -alpha_beta(child, depth - 1, -beta, -alpha);
```

**Alpha-beta pruning** cuts off branches that can't possibly improve the result:

- `alpha` = best score the current player is **guaranteed** (lower bound)
- `beta` = best score the opponent is **guaranteed** (upper bound)
- If `score >= beta`: the opponent would never let us reach this position (beta cutoff), so we stop searching.
- If `score > alpha`: we found a better move, update alpha.

**Move ordering** places captures first:

```cpp
std::stable_partition(moves.begin(), moves.end(),
    [](const Move& m){ return !m.captured.empty(); });
```

This matters because alpha-beta is most effective when the best moves are searched first. Captures tend to be better moves, so ordering them first generates more cutoffs and makes the search roughly twice as fast in practice.

---

## 8. Evaluation

```cpp
int evaluate() const {
    int score = 0;
    for (int sq = 0; sq < 64; sq++) {
        int val = PST::score(squares[sq], sq);
        score += (p.color == WHITE) ? val : -val;
    }
    return (side_to_move == WHITE) ? score : -score;
}
```

Sums `PST::score()` for every piece (material + positional bonus), with White positive and Black negative. Then converts to the perspective of `side_to_move` — because alpha-beta expects the score from the current player's point of view.

This is a purely **static** evaluation — it doesn't look at attacks, mobility, pawn structure, or king safety. These are the main areas to improve.

---

## 9. Input Parsing

```cpp
static std::optional<Move> parse_uci(const Board& board, const std::string& uci)
```

Takes a UCI string like `"e2e4"` or `"e7e8q"`, converts it to `(from, to)` squares, then finds the matching move in `legal_moves()`. Returns `std::nullopt` if the string is malformed or the move isn't legal.

The promotion character (`q`, `r`, `b`, `n`) in the input is currently **not checked** — whichever matching promotion move appears first in the list is returned. This means you can't choose your promotion piece from the command line yet (it defaults to whatever comes first, usually knight). This is a small bug to fix.

---

## 10. Known Bugs and Gaps

### No check detection
The engine doesn't know when a king is in check. Consequences:
- You can move into check without being stopped
- The engine may leave its own king in check
- Checkmate and stalemate are both detected as "no legal moves" and scored as `-INF` — so the engine treats stalemate (draw) as equivalent to being mated (loss), which is wrong

### Castling through check
`gen_king()` only checks empty squares, not attacked squares. The king can castle through or into check.

### Promotion input
`parse_uci()` doesn't read the promotion character from input. Promotions default to the first match in `legal_moves()` (knight).

### No draw detection
No 50-move rule, no threefold repetition. The engine can repeat positions indefinitely.

### No quiescence search
The search stops at depth 0 and calls `evaluate()` even if the position is mid-exchange (e.g. you just captured a queen and the opponent hasn't recaptured yet). This causes the "horizon effect" — the engine misjudges these positions.

---

## 11. Where to Improve Next

These are listed in order of difficulty and impact:

**1. Check detection (medium, high impact)**
After generating a move, apply it and check if the king is attacked. The attack check can reuse the slider/knight logic. Filter out moves that leave the king in check. This also fixes castling through check and checkmate/stalemate distinction.

**2. Fix promotion input (easy)**
In `parse_uci()`, read `uci[4]` if present and match it against `m.promotion.type`.

**3. Quiescence search (medium, high impact)**
At depth 0, instead of returning `evaluate()` immediately, continue searching captures only until the position is "quiet". This eliminates the horizon effect.

**4. Iterative deepening (medium)**
Instead of searching at a fixed depth, search depth 1, then 2, then 3, etc., within a time budget. This gives you time management for free and improves move ordering (you can use depth-1 results to sort depth-2 moves).

**5. Transposition table (hard, high impact)**
Cache `(board_hash → score)` to avoid re-evaluating the same position reached via different move orders. Requires a Zobrist hash of the board state.

**6. Better evaluation (easy wins)**
Without check detection, adding king safety is risky. But you can safely add:
- Doubled pawn penalty (two pawns on same file)
- Isolated pawn penalty (no friendly pawns on adjacent files)
- Bishop pair bonus (+50 if you have both bishops)
- Mobility bonus (count legal moves)
