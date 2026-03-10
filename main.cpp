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
//  Input / Output helpers
// ─────────────────────────────────────────────

static std::optional<Move> parse_uci(const Board& board, const std::string& uci) {
    if (uci.size() < 4) return std::nullopt;
    auto to_sq = [](char f, char r) -> int {
        if (f < 'a' || f > 'h' || r < '1' || r > '8') return -1;
        return (r - '1') * 8 + (f - 'a');
    };
    int from = to_sq(uci[0], uci[1]);
    int to   = to_sq(uci[2], uci[3]);
    if (from < 0 || to < 0) return std::nullopt;

    for (const auto& m : board.legal_moves())
        if (m.from == from && m.to == to)
            return m;

    return std::nullopt;
}

// ─────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────

int main() {
    std::cout << "╔══════════════════════════════╗\n";
    std::cout << "║   C++ Chess Engine  v2.0     ║\n";
    std::cout << "╚══════════════════════════════╝\n";
    std::cout << "You play White. Enter moves in UCI format (e.g. e2e4).\n";
    std::cout << "Type 'quit' to exit.\n\n";

    Board board = Board::starting_position();
    Engine engine(SEARCH_DEPTH);

    while (true) {
        board.print();

        if (board.side_to_move == WHITE) {
            std::string input;
            std::cout << "Your move: ";
            std::getline(std::cin, input);
            if (input == "quit") break;

            auto move = parse_uci(board, input);
            if (!move) {
                std::cout << "Illegal move. Try again.\n\n";
                continue;
            }
            board = board.apply(*move);

        } else {
            std::cout << "Engine thinking...\n";
            Move m = engine.best_move(board);
            std::cout << "Engine plays: " << m.uci() << "\n\n";
            board = board.apply(m);
        }
    }

    std::cout << "Thanks for playing!\n";
    return 0;
}