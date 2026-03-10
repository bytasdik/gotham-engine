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
9. [`king_in_check()`](#9-king_in_check)
10. [SAN Generation — `Move::san()`](#10-san-generation)
11. [SAN Parsing — `parse_san()`](#11-san-parsing)
12. [Known Bugs and Gaps](#12-known-bugs-and-gaps)
13. [Where to Improve Next](#13-where-to-improve-next)

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

`Move` has two serialization methods:

- `uci()` — internal format (`e2e4`, `e7e8q`). Kept for debugging.
- `san(const Board& pre)` — Standard Algebraic Notation (`Nf3`, `exd5`, `O-O`, `e8=Q`). Requires the **pre-move board** because SAN is context-dependent: you need to know what other pieces of the same type can reach the same square to decide if disambiguation is needed, and you need to apply the move to detect check/checkmate.

`san()` is forward-declared in the `Move` struct and defined after the `Board` class, since it calls `Board::apply()` and `Board::legal_moves()`.

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

## 9. `king_in_check()`

```cpp
static bool king_in_check(const Board& board, Color side)
```

A standalone attack detector: given a board and a color, returns `true` if that color's king is currently attacked. It works by scanning outward from the king square in all directions — the same ray logic used by the slider generator — and checking whether an attacking piece sits at the end of each ray.

It checks three attack types:

- **Knight attacks** — fixed 8-delta table, boundary-checked
- **Slider attacks** — rays in all 8 directions; stops at the first piece; checks if it's a bishop/queen (diagonals) or rook/queen (orthogonals); also catches pawn attacks on the first diagonal step
- **King adjacency** — prevents two kings from standing next to each other (which the normal move generator doesn't catch on its own)

This function exists primarily to power `san()` for `+`/`#` annotation. It is **not yet hooked into `legal_moves()`** to filter out moves that leave the king in check — that's the next step.

---

## 10. SAN Generation — `Move::san()`

```cpp
std::string Move::san(const Board& pre) const
```

Builds the SAN string for a move. The steps in order:

**Castling** — returns `O-O` or `O-O-O` immediately (determined by whether `to > from`), then appends `+` if needed.

**Piece letter** — uppercase letter for the moving piece (`N`, `B`, `R`, `Q`, `K`). Omitted for pawns.

**Disambiguation** — scans `legal_moves()` on the pre-move board to find any other piece of the same type that can also reach `to`. If found:
- If they're on different files → add source file letter (`Raf1`)
- If they're on different ranks → add source rank digit (`R1f3`)
- If both (very rare) → add full source square (`Qd1f3` style)

**Pawn capture prefix** — for pawn captures (including en passant), the source file is always added (`exd5`, `gxh8=Q`).

**Capture symbol** — `x` if `captured` is non-empty or `is_ep` is true.

**Destination square** — always present.

**Promotion** — `=Q`, `=R`, `=B`, or `=N` if `promotion` is non-empty.

**Check/checkmate** — applies the move with `pre.apply(*this)`, then calls `king_in_check()` on the resulting board. If the opponent is in check, filters their legal replies for any that escape check. If none escape → `#`, otherwise → `+`.

---

## 11. SAN Parsing — `parse_san()`

```cpp
static std::optional<Move> parse_san(const Board& board, std::string s)
```

Converts a SAN string typed by the user into a `Move` by matching it against `legal_moves()`.

**Steps:**

1. Strip trailing `+` and `#` (irrelevant for matching)
2. Check for castling (`O-O` / `O-O-O`, also accepts `0-0`)
3. Read optional leading piece letter. Absence means pawn.
4. Strip promotion suffix from the end (`=Q`, `=R`, etc.)
5. Remove `x` (capture marker) — its presence is advisory, not used for matching
6. The last two characters are always the destination square
7. Whatever remains (0–2 characters) is disambiguation: a file letter, a rank digit, or both
8. Collect all legal moves that match `(piece type, destination, disambiguation, promotion)` into candidates
9. Return the unique candidate, or `std::nullopt` if zero or ambiguous

This approach — generate all legal moves first, then filter — means the parser never needs to know anything about piece movement rules itself. The legality check is entirely delegated to `Board::legal_moves()`.

---

## 12. Known Bugs and Gaps

### No illegal move filtering
`king_in_check()` exists and works correctly for check/checkmate annotation. But `legal_moves()` does not call it to filter out moves that leave the king in check. Consequences:
- You can move into check without being stopped
- The engine may leave its own king in check during search
- Castling through check is also allowed (castling only checks empty squares)

### Stalemate scored as loss
When `legal_moves()` returns empty, `alpha_beta` returns `-INF`. This means stalemate (a draw) is scored identically to checkmate (a loss). The engine will sometimes blunder into stalemate thinking it's losing anyway.

### No draw detection
No 50-move rule, no threefold repetition. The engine has no game history so it can repeat positions indefinitely.

### No quiescence search
The search stops at depth 0 and calls `evaluate()` even if the position is mid-exchange. If you just captured a queen and the opponent hasn't recaptured, the engine may overestimate the position's value. This is called the "horizon effect."

---

## 13. Where to Improve Next

These are in order of priority and dependency:

**1. Filter illegal moves (medium effort, foundational)**
After generating each candidate move in `legal_moves()`, apply it and call `king_in_check(post_board, side_to_move)`. Discard if true. This one change fixes: moving into check, castling through check, and correct checkmate/stalemate distinction. Everything below gets more reliable once this is in.

**2. Fix stalemate scoring (easy, depends on #1)**
Once illegal moves are filtered, a position with zero legal moves is either checkmate or stalemate. Check `king_in_check()` on the current position: if in check → mate score (`-INF`), if not → stalemate score (`0`).

**3. Quiescence search (medium, high impact)**
At depth 0, instead of returning `evaluate()` immediately, continue searching captures-only until the position is "quiet" (no immediate captures available). This eliminates the horizon effect and is one of the biggest strength improvements available.

**4. Iterative deepening + time management (medium)**
Search depth 1, then 2, then 3, within a time budget rather than a fixed depth. Side effects: free time management, and earlier depths' results can be used to sort moves for later depths (improving cutoffs).

**5. Transposition table (harder, high impact)**
Cache `(Zobrist hash → {score, depth, flag})` in a hash map. Avoids re-searching positions reached via different move orders. Requires computing a Zobrist hash incrementally in `apply()`.

**6. Better evaluation (easy wins, safe after #1)**
Once illegal move filtering is in, it's safe to add king safety heuristics. Other quick wins:
- Doubled pawn penalty (two pawns on same file: `-20` each)
- Isolated pawn penalty (no pawns on adjacent files: `-15`)
- Bishop pair bonus (`+50` if both bishops present)
- Passed pawn bonus (no enemy pawns blocking or attacking the path to promotion)