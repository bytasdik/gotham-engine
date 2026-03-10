/*
 * Chess Engine — Modern C++17
 * Features: OOP design, move generation, alpha-beta search, piece-square tables
 */

#include <iostream>
#include <vector>
#include <array>
#include <string>
#include <algorithm>
#include <limits>
#include <optional>
#include <cassert>
#include <cstdint>

// ─────────────────────────────────────────────
//  Enums & Constants
// ─────────────────────────────────────────────

enum Color { WHITE = 0, BLACK = 1, NO_COLOR = 2 };
enum PieceType { NONE = 0, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };

constexpr int INF        = std::numeric_limits<int>::max() / 2;
constexpr int SEARCH_DEPTH = 4;
constexpr int MAX_MOVES    = 256;

// ─────────────────────────────────────────────
//  Piece
// ─────────────────────────────────────────────

struct Piece {
    PieceType type  = NONE;
    Color     color = NO_COLOR;

    bool empty() const { return type == NONE; }
    char symbol() const {
        if (empty()) return '.';
        static const char syms[] = ".pnbrqk";
        char c = syms[type];
        return (color == WHITE) ? std::toupper(c) : c;
    }
    bool operator==(const Piece& o) const { return type == o.type && color == o.color; }
};

// Forward declaration so Move::san() can reference Board
class Board;

// ─────────────────────────────────────────────
//  Move
// ─────────────────────────────────────────────

struct Move {
    int   from      = 0;
    int   to        = 0;
    Piece captured  = {};
    Piece promotion = {};   // non-empty → promotion
    bool  is_castle = false;
    bool  is_ep     = false;

    // UCI string — kept for internal use / debugging
    std::string uci() const {
        std::string s;
        s += static_cast<char>('a' + (from % 8));
        s += static_cast<char>('1' + (from / 8));
        s += static_cast<char>('a' + (to   % 8));
        s += static_cast<char>('1' + (to   / 8));
        if (!promotion.empty()) {
            static const char syms[] = ".pnbrqk";
            s += syms[promotion.type];
        }
        return s;
    }

    // SAN — forward-declared; defined after Board class
    std::string san(const Board& pre) const;
};

// ─────────────────────────────────────────────
//  Piece-Square Tables  (White's perspective)
// ─────────────────────────────────────────────

namespace PST {
    constexpr std::array<int,64> pawn = {
         0,  0,  0,  0,  0,  0,  0,  0,
        50, 50, 50, 50, 50, 50, 50, 50,
        10, 10, 20, 30, 30, 20, 10, 10,
         5,  5, 10, 25, 25, 10,  5,  5,
         0,  0,  0, 20, 20,  0,  0,  0,
         5, -5,-10,  0,  0,-10, -5,  5,
         5, 10, 10,-20,-20, 10, 10,  5,
         0,  0,  0,  0,  0,  0,  0,  0
    };
    constexpr std::array<int,64> knight = {
       -50,-40,-30,-30,-30,-30,-40,-50,
       -40,-20,  0,  0,  0,  0,-20,-40,
       -30,  0, 10, 15, 15, 10,  0,-30,
       -30,  5, 15, 20, 20, 15,  5,-30,
       -30,  0, 15, 20, 20, 15,  0,-30,
       -30,  5, 10, 15, 15, 10,  5,-30,
       -40,-20,  0,  5,  5,  0,-20,-40,
       -50,-40,-30,-30,-30,-30,-40,-50
    };
    constexpr std::array<int,64> bishop = {
       -20,-10,-10,-10,-10,-10,-10,-20,
       -10,  0,  0,  0,  0,  0,  0,-10,
       -10,  0,  5, 10, 10,  5,  0,-10,
       -10,  5,  5, 10, 10,  5,  5,-10,
       -10,  0, 10, 10, 10, 10,  0,-10,
       -10, 10, 10, 10, 10, 10, 10,-10,
       -10,  5,  0,  0,  0,  0,  5,-10,
       -20,-10,-10,-10,-10,-10,-10,-20
    };
    constexpr std::array<int,64> rook = {
         0,  0,  0,  0,  0,  0,  0,  0,
         5, 10, 10, 10, 10, 10, 10,  5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
         0,  0,  0,  5,  5,  0,  0,  0
    };
    constexpr std::array<int,64> queen = {
       -20,-10,-10, -5, -5,-10,-10,-20,
       -10,  0,  0,  0,  0,  0,  0,-10,
       -10,  0,  5,  5,  5,  5,  0,-10,
        -5,  0,  5,  5,  5,  5,  0, -5,
         0,  0,  5,  5,  5,  5,  0, -5,
       -10,  5,  5,  5,  5,  5,  0,-10,
       -10,  0,  5,  0,  0,  0,  0,-10,
       -20,-10,-10, -5, -5,-10,-10,-20
    };
    constexpr std::array<int,64> king = {
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -20,-30,-30,-40,-40,-30,-30,-20,
       -10,-20,-20,-20,-20,-20,-20,-10,
        20, 20,  0,  0,  0,  0, 20, 20,
        20, 30, 10,  0,  0, 10, 30, 20
    };

    constexpr int piece_value[7] = {0, 100, 320, 330, 500, 900, 20000};

    int score(Piece p, int sq) {
        int idx = (p.color == WHITE) ? (63 - sq) : sq;
        int val = piece_value[p.type];
        switch (p.type) {
            case PAWN:   return val + pawn[idx];
            case KNIGHT: return val + knight[idx];
            case BISHOP: return val + bishop[idx];
            case ROOK:   return val + rook[idx];
            case QUEEN:  return val + queen[idx];
            case KING:   return val + king[idx];
            default:     return 0;
        }
    }
}

// ─────────────────────────────────────────────
//  Board
// ─────────────────────────────────────────────

class Board {
public:
    std::array<Piece, 64> squares{};
    Color  side_to_move = WHITE;
    int    ep_square    = -1;   // en-passant target, or -1
    uint8_t castle_rights = 0xF;  // bit0=WK, bit1=WQ, bit2=BK, bit3=BQ

    // ── Setup ──────────────────────────────────
    static Board starting_position() {
        Board b;
        auto place = [&](int sq, Color c, PieceType t){ b.squares[sq] = {t, c}; };

        for (int i = 0; i < 8; i++) {
            place(8 + i, WHITE, PAWN);
            place(48 + i, BLACK, PAWN);
        }
        static const PieceType back[] = {ROOK,KNIGHT,BISHOP,QUEEN,KING,BISHOP,KNIGHT,ROOK};
        for (int i = 0; i < 8; i++) {
            place(i,      WHITE, back[i]);
            place(56 + i, BLACK, back[i]);
        }
        return b;
    }

    // ── Display ────────────────────────────────
    void print() const {
        std::cout << "\n  a b c d e f g h\n";
        for (int r = 7; r >= 0; r--) {
            std::cout << (r + 1) << ' ';
            for (int c = 0; c < 8; c++)
                std::cout << squares[r * 8 + c].symbol() << ' ';
            std::cout << (r + 1) << '\n';
        }
        std::cout << "  a b c d e f g h\n";
        std::cout << "Side: " << (side_to_move == WHITE ? "White" : "Black") << "\n\n";
    }

    // ── Move generation ────────────────────────
    std::vector<Move> legal_moves() const {
        std::vector<Move> moves;
        moves.reserve(40);

        for (int sq = 0; sq < 64; sq++) {
            const Piece& p = squares[sq];
            if (p.empty() || p.color != side_to_move) continue;

            switch (p.type) {
                case PAWN:   gen_pawn(sq, moves);   break;
                case KNIGHT: gen_knight(sq, moves); break;
                case BISHOP: gen_slider(sq, moves, 4, 8); break;
                case ROOK:   gen_slider(sq, moves, 0, 4); break;
                case QUEEN:  gen_slider(sq, moves, 0, 8); break;
                case KING:   gen_king(sq, moves);   break;
                default: break;
            }
        }
        return moves;
    }

    // ── Apply a move (returns board copy) ──────
    Board apply(const Move& m) const {
        Board nb = *this;
        Piece moving = nb.squares[m.from];
        nb.squares[m.to]   = m.promotion.empty() ? moving : m.promotion;
        nb.squares[m.from] = {};
        nb.ep_square = -1;

        // En passant capture
        if (m.is_ep) {
            int cap_sq = m.to + (side_to_move == WHITE ? -8 : 8);
            nb.squares[cap_sq] = {};
        }
        // Double pawn push → set EP square
        if (moving.type == PAWN && std::abs(m.to - m.from) == 16)
            nb.ep_square = (m.from + m.to) / 2;

        // Castling: move rook
        if (m.is_castle) {
            if (m.to == 6)  { nb.squares[5]  = nb.squares[7];  nb.squares[7]  = {}; }
            if (m.to == 2)  { nb.squares[3]  = nb.squares[0];  nb.squares[0]  = {}; }
            if (m.to == 62) { nb.squares[61] = nb.squares[63]; nb.squares[63] = {}; }
            if (m.to == 58) { nb.squares[59] = nb.squares[56]; nb.squares[56] = {}; }
        }

        // Update castling rights
        if (moving.type == KING) nb.castle_rights &= (side_to_move == WHITE) ? ~3 : ~12;
        if (moving.type == ROOK) {
            if (m.from == 0)  nb.castle_rights &= ~2;
            if (m.from == 7)  nb.castle_rights &= ~1;
            if (m.from == 56) nb.castle_rights &= ~8;
            if (m.from == 63) nb.castle_rights &= ~4;
        }

        nb.side_to_move = (side_to_move == WHITE) ? BLACK : WHITE;
        return nb;
    }

    // ── Static evaluation (from side_to_move's perspective) ──
    int evaluate() const {
        int score = 0;
        for (int sq = 0; sq < 64; sq++) {
            const Piece& p = squares[sq];
            if (p.empty()) continue;
            int val = PST::score(p, sq);
            score += (p.color == WHITE) ? val : -val;
        }
        return (side_to_move == WHITE) ? score : -score;
    }

private:
    static constexpr int row(int sq) { return sq / 8; }
    static constexpr int col(int sq) { return sq % 8; }

    Color enemy() const { return (side_to_move == WHITE) ? BLACK : WHITE; }

    void try_add(int from, int to, std::vector<Move>& moves,
                 bool is_ep = false, bool is_castle = false,
                 Piece promo = {}) const
    {
        Piece cap = squares[to];
        if (!cap.empty() && cap.color == side_to_move) return; // own piece
        Move m;
        m.from       = from;
        m.to         = to;
        m.captured   = cap;
        m.promotion  = promo;
        m.is_ep      = is_ep;
        m.is_castle  = is_castle;
        moves.push_back(m);
    }

    void gen_pawn(int sq, std::vector<Move>& moves) const {
        const int dir       = (side_to_move == WHITE) ? 1 : -1;
        const int start_row = (side_to_move == WHITE) ? 1 : 6;
        const int promo_row = (side_to_move == WHITE) ? 7 : 0;
        const int r = row(sq), c = col(sq);

        auto add_pawn_move = [&](int to, bool capture, bool is_ep = false) {
            if (row(to) == promo_row) {
                for (auto pt : {KNIGHT, BISHOP, ROOK, QUEEN})
                    try_add(sq, to, moves, is_ep, false, {pt, side_to_move});
            } else {
                try_add(sq, to, moves, is_ep);
            }
        };

        // Single push
        int fwd = sq + dir * 8;
        if (fwd >= 0 && fwd < 64 && squares[fwd].empty()) {
            add_pawn_move(fwd, false);
            // Double push
            if (r == start_row) {
                int fwd2 = sq + dir * 16;
                if (squares[fwd2].empty()) add_pawn_move(fwd2, false);
            }
        }
        // Captures
        for (int dc : {-1, 1}) {
            int nc = c + dc;
            if (nc < 0 || nc > 7) continue;
            int t = fwd - 8 + dir * 8 + dc; // = sq + dir*8 + dc
            t = sq + dir * 8 + dc;
            if (t < 0 || t >= 64) continue;
            if (!squares[t].empty() && squares[t].color == enemy())
                add_pawn_move(t, true);
            if (t == ep_square)
                add_pawn_move(t, true, true);
        }
    }

    void gen_knight(int sq, std::vector<Move>& moves) const {
        static constexpr int dr[] = {-2,-2,-1,-1, 1, 1, 2, 2};
        static constexpr int dc[] = {-1, 1,-2, 2,-2, 2,-1, 1};
        int r = row(sq), c = col(sq);
        for (int i = 0; i < 8; i++) {
            int nr = r + dr[i], nc = c + dc[i];
            if (nr < 0 || nr > 7 || nc < 0 || nc > 7) continue;
            try_add(sq, nr * 8 + nc, moves);
        }
    }

    void gen_slider(int sq, std::vector<Move>& moves, int dstart, int dend) const {
        static constexpr int dr[] = { 1,-1, 0, 0, 1, 1,-1,-1};
        static constexpr int dc[] = { 0, 0, 1,-1, 1,-1, 1,-1};
        int r = row(sq), c = col(sq);
        for (int d = dstart; d < dend; d++) {
            for (int step = 1; step < 8; step++) {
                int nr = r + dr[d]*step, nc = c + dc[d]*step;
                if (nr < 0 || nr > 7 || nc < 0 || nc > 7) break;
                int t = nr * 8 + nc;
                if (!squares[t].empty()) {
                    if (squares[t].color == enemy()) try_add(sq, t, moves);
                    break;
                }
                try_add(sq, t, moves);
            }
        }
    }

    void gen_king(int sq, std::vector<Move>& moves) const {
        static constexpr int dr[] = {-1,-1,-1, 0, 0, 1, 1, 1};
        static constexpr int dc[] = {-1, 0, 1,-1, 1,-1, 0, 1};
        int r = row(sq), c = col(sq);
        for (int i = 0; i < 8; i++) {
            int nr = r + dr[i], nc = c + dc[i];
            if (nr < 0 || nr > 7 || nc < 0 || nc > 7) continue;
            try_add(sq, nr * 8 + nc, moves);
        }
        // Castling (simplified: no check detection)
        if (side_to_move == WHITE && sq == 4) {
            if ((castle_rights & 1) && squares[5].empty() && squares[6].empty())
                { Move m{4,6,{},{},true}; moves.push_back(m); }
            if ((castle_rights & 2) && squares[3].empty() && squares[2].empty() && squares[1].empty())
                { Move m{4,2,{},{},true}; moves.push_back(m); }
        }
        if (side_to_move == BLACK && sq == 60) {
            if ((castle_rights & 4) && squares[61].empty() && squares[62].empty())
                { Move m{60,62,{},{},true}; moves.push_back(m); }
            if ((castle_rights & 8) && squares[59].empty() && squares[58].empty() && squares[57].empty())
                { Move m{60,58,{},{},true}; moves.push_back(m); }
        }
    }
};

// ─────────────────────────────────────────────
//  Engine — Alpha-Beta Search
// ─────────────────────────────────────────────

class Engine {
public:
    explicit Engine(int depth = SEARCH_DEPTH) : max_depth_(depth) {}

    Move best_move(const Board& board) {
        auto moves = board.legal_moves();
        if (moves.empty()) return {};

        Move best = moves[0];
        int  best_score = -INF;

        for (const auto& m : moves) {
            Board child = board.apply(m);
            int score = -alpha_beta(child, max_depth_ - 1, -INF, INF);
            if (score > best_score) {
                best_score = score;
                best       = m;
            }
        }
        return best;
    }

private:
    int max_depth_;

    int alpha_beta(const Board& board, int depth, int alpha, int beta) {
        if (depth == 0) return board.evaluate();

        auto moves = board.legal_moves();
        if (moves.empty()) return -INF; // no moves → very bad

        // Move ordering: captures first
        std::stable_partition(moves.begin(), moves.end(),
            [](const Move& m){ return !m.captured.empty(); });

        for (const auto& m : moves) {
            Board child = board.apply(m);
            int score = -alpha_beta(child, depth - 1, -beta, -alpha);
            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }
        return alpha;
    }
};

// ─────────────────────────────────────────────
//  SAN generation  (defined after Board)
// ─────────────────────────────────────────────

// Helper: is the king of `side` attacked on `board`?
static bool king_in_check(const Board& board, Color side) {
    // Find king square
    int king_sq = -1;
    for (int sq = 0; sq < 64; sq++)
        if (board.squares[sq].type == KING && board.squares[sq].color == side)
            { king_sq = sq; break; }
    if (king_sq < 0) return false;

    // Check knight attacks
    static constexpr int ndr[] = {-2,-2,-1,-1, 1, 1, 2, 2};
    static constexpr int ndc[] = {-1, 1,-2, 2,-2, 2,-1, 1};
    int kr = king_sq / 8, kc = king_sq % 8;
    for (int i = 0; i < 8; i++) {
        int nr = kr + ndr[i], nc = kc + ndc[i];
        if (nr < 0 || nr > 7 || nc < 0 || nc > 7) continue;
        Piece p = board.squares[nr * 8 + nc];
        if (p.type == KNIGHT && p.color != side) return true;
    }
    // Check slider/pawn attacks
    static constexpr int dr[] = { 1,-1, 0, 0, 1, 1,-1,-1};
    static constexpr int dc[] = { 0, 0, 1,-1, 1,-1, 1,-1};
    for (int d = 0; d < 8; d++) {
        for (int step = 1; step < 8; step++) {
            int nr = kr + dr[d]*step, nc = kc + dc[d]*step;
            if (nr < 0 || nr > 7 || nc < 0 || nc > 7) break;
            Piece p = board.squares[nr * 8 + nc];
            if (p.empty()) continue;
            if (p.color == side) break; // own piece blocks
            bool diag = (d >= 4);
            bool orth = (d < 4);
            if (diag && (p.type == BISHOP || p.type == QUEEN)) return true;
            if (orth && (p.type == ROOK   || p.type == QUEEN)) return true;
            // Pawn attacks: only on first diagonal step
            if (step == 1 && diag && p.type == PAWN) {
                int pawn_dir = (side == WHITE) ? 1 : -1;
                if (dr[d] == pawn_dir) return true;
            }
            break;
        }
    }
    // King adjacency (prevent kings touching)
    static constexpr int kdr[] = {-1,-1,-1,0,0,1,1,1};
    static constexpr int kdc[] = {-1, 0, 1,-1,1,-1,0,1};
    for (int i = 0; i < 8; i++) {
        int nr = kr + kdr[i], nc = kc + kdc[i];
        if (nr < 0 || nr > 7 || nc < 0 || nc > 7) continue;
        Piece p = board.squares[nr * 8 + nc];
        if (p.type == KING && p.color != side) return true;
    }
    return false;
}

std::string Move::san(const Board& pre) const {
    // Castling
    if (is_castle) {
        std::string s = (to > from) ? "O-O" : "O-O-O";
        Board post = pre.apply(*this);
        if (king_in_check(post, post.side_to_move)) s += '+';
        return s;
    }

    Piece moving = pre.squares[from];
    std::string s;

    static const char piece_letter[] = ".PNBRQK";

    // Piece prefix (omit for pawns)
    if (moving.type != PAWN)
        s += piece_letter[moving.type];

    // Disambiguation: find other pieces of same type that can reach `to`
    if (moving.type != PAWN) {
        auto all = pre.legal_moves();
        std::vector<int> ambig;
        for (const auto& m : all)
            if (m.to == to && m.from != from &&
                pre.squares[m.from].type == moving.type &&
                pre.squares[m.from].color == moving.color)
                ambig.push_back(m.from);

        if (!ambig.empty()) {
            bool same_file = false, same_rank = false;
            for (int sq : ambig) {
                if ((sq % 8) == (from % 8)) same_file = true;
                if ((sq / 8) == (from / 8)) same_rank = true;
            }
            if (!same_file)       s += static_cast<char>('a' + (from % 8));
            else if (!same_rank)  s += static_cast<char>('1' + (from / 8));
            else {
                s += static_cast<char>('a' + (from % 8));
                s += static_cast<char>('1' + (from / 8));
            }
        }
    }

    // Pawn capture: always include source file
    if (moving.type == PAWN && (!captured.empty() || is_ep))
        s += static_cast<char>('a' + (from % 8));

    // Capture symbol
    if (!captured.empty() || is_ep)
        s += 'x';

    // Destination square
    s += static_cast<char>('a' + (to % 8));
    s += static_cast<char>('1' + (to / 8));

    // Promotion
    if (!promotion.empty()) {
        s += '=';
        s += piece_letter[promotion.type];
    }

    // Check / checkmate indicator
    Board post = pre.apply(*this);
    if (king_in_check(post, post.side_to_move)) {
        auto replies = post.legal_moves();
        // Filter out replies that leave own king in check
        bool has_legal = false;
        for (const auto& r : replies) {
            Board after = post.apply(r);
            if (!king_in_check(after, post.side_to_move)) { has_legal = true; break; }
        }
        s += has_legal ? '+' : '#';
    }

    return s;
}

// ─────────────────────────────────────────────
//  SAN parser
// ─────────────────────────────────────────────

// Parse Standard Algebraic Notation into a Move.
// Accepts: e4, Nf3, exd5, O-O, O-O-O, e8=Q, Raxd1, etc.
static std::optional<Move> parse_san(const Board& board, std::string s) {
    // Strip check/mate indicators
    while (!s.empty() && (s.back() == '+' || s.back() == '#')) s.pop_back();
    if (s.empty()) return std::nullopt;

    auto legal = board.legal_moves();

    // ── Castling ──────────────────────────────
    if (s == "O-O" || s == "0-0") {
        for (const auto& m : legal)
            if (m.is_castle && m.to > m.from) return m;
        return std::nullopt;
    }
    if (s == "O-O-O" || s == "0-0-0") {
        for (const auto& m : legal)
            if (m.is_castle && m.to < m.from) return m;
        return std::nullopt;
    }

    // ── Decode fields ─────────────────────────
    // Grammar (simplified):
    //   [Piece][file][rank][x]<file><rank>[=Piece]
    // Where [] = optional.

    PieceType piece = PAWN;
    int  src_file = -1, src_rank = -1;  // disambiguation
    bool capture  = false;
    int  dst_file = -1, dst_rank = -1;
    PieceType promo = NONE;

    size_t i = 0;

    // Piece letter
    if (s[i] >= 'A' && s[i] <= 'Z') {
        switch (s[i]) {
            case 'N': piece = KNIGHT; break;
            case 'B': piece = BISHOP; break;
            case 'R': piece = ROOK;   break;
            case 'Q': piece = QUEEN;  break;
            case 'K': piece = KING;   break;
            default: return std::nullopt;
        }
        i++;
    }

    // Collect remaining lowercase/digit/x characters
    // Pattern after piece letter: [file][rank][x]<file><rank>[=P]
    // We read left-to-right and figure out what's disambiguation vs destination.
    std::string rest = s.substr(i);

    // Strip promotion suffix  e.g. "=Q"
    if (rest.size() >= 2 && rest[rest.size()-2] == '=') {
        char pc = rest.back();
        switch (pc) {
            case 'Q': promo = QUEEN;  break;
            case 'R': promo = ROOK;   break;
            case 'B': promo = BISHOP; break;
            case 'N': promo = KNIGHT; break;
            default: return std::nullopt;
        }
        rest = rest.substr(0, rest.size() - 2);
    }

    // Remove capture 'x' and remember it
    {
        auto xpos = rest.find('x');
        if (xpos != std::string::npos) { capture = true; rest.erase(xpos, 1); }
    }

    // Now `rest` should be 2–4 alphanumeric chars: [file][rank]<file><rank>
    // Destination is always the last file+rank pair.
    if (rest.size() < 2) return std::nullopt;

    dst_file = rest[rest.size()-2] - 'a';
    dst_rank = rest[rest.size()-1] - '1';
    if (dst_file < 0 || dst_file > 7 || dst_rank < 0 || dst_rank > 7) return std::nullopt;

    rest = rest.substr(0, rest.size() - 2); // remove destination

    // Whatever remains is disambiguation
    for (char c : rest) {
        if (c >= 'a' && c <= 'h') src_file = c - 'a';
        else if (c >= '1' && c <= '8') src_rank = c - '1';
        else return std::nullopt;
    }

    int dst_sq = dst_rank * 8 + dst_file;

    // ── Match against legal moves ──────────────
    std::vector<Move> candidates;
    for (const auto& m : legal) {
        if (m.to != dst_sq) continue;
        if (board.squares[m.from].type != piece) continue;
        if (src_file >= 0 && (m.from % 8) != src_file) continue;
        if (src_rank >= 0 && (m.from / 8) != src_rank) continue;
        if (promo != NONE && m.promotion.type != promo) continue;
        if (promo == NONE && !m.promotion.empty()) continue;
        (void)capture; // capture flag is advisory; legality already checked
        candidates.push_back(m);
    }

    if (candidates.size() == 1) return candidates[0];
    if (candidates.empty())     return std::nullopt;

    // Multiple candidates: shouldn't happen with valid SAN, but pick first
    return candidates[0];
}

// ─────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────

int main() {
    std::cout << "╔══════════════════════════════╗\n";
    std::cout << "║     Gotham Engine  v2.1      ║\n";
    std::cout << "╚══════════════════════════════╝\n";
    std::cout << "You play White. Enter moves in Standard Algebraic Notation.\n";
    std::cout << "Examples: e4  Nf3  exd5  O-O  e8=Q\n";
    std::cout << "Type 'quit' to exit.\n\n";

    Board board = Board::starting_position();
    Engine engine(SEARCH_DEPTH);

    int move_number = 1;

    while (true) {
        board.print();

        if (board.side_to_move == WHITE) {
            std::string input;
            std::cout << move_number << ". Your move: ";
            std::getline(std::cin, input);
            if (input == "quit") break;

            auto move = parse_san(board, input);
            if (!move) {
                std::cout << "Illegal or unrecognized move. Try again.\n\n";
                continue;
            }
            std::cout << move_number << ". " << move->san(board) << "\n\n";
            board = board.apply(*move);

        } else {
            std::cout << "Engine thinking...\n";
            Move m = engine.best_move(board);
            std::cout << move_number << "... " << m.san(board) << "\n\n";
            board = board.apply(m);
            move_number++;
        }
    }

    std::cout << "Thanks for playing!\n";
    return 0;
}