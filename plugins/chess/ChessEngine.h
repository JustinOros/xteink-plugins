#pragma once

#include <cstdint>
#include <cstring>

#if defined(__has_include)
#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif
#endif

namespace chess {

enum PieceType : int8_t { EMPTY = 0, PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6 };

constexpr uint8_t CASTLE_WK = 1;
constexpr uint8_t CASTLE_WQ = 2;
constexpr uint8_t CASTLE_BK = 4;
constexpr uint8_t CASTLE_BQ = 8;

constexpr uint8_t FLAG_CAPTURE = 1;
constexpr uint8_t FLAG_EP      = 2;
constexpr uint8_t FLAG_CASTLE  = 4;
constexpr uint8_t FLAG_DOUBLE  = 8;

struct Move {
    uint8_t from;
    uint8_t to;
    int8_t promo;
    uint8_t flags;
};

struct Position {
    int8_t b[64];
    uint8_t castle;
    int8_t ep;
    uint8_t side;
    uint8_t half;
    int8_t king[2];
};

struct Level {
    const char* name;
    const char* hint;
    int depth;
    unsigned long timeMs;
    int noise;
    bool quiescence;
};

inline bool samePosition(const Position& a, const Position& c) {
    return a.side == c.side && a.castle == c.castle && a.ep == c.ep && memcmp(a.b, c.b, 64) == 0;
}

inline void startPosition(Position& p) {
    memset(&p, 0, sizeof(p));
    const int8_t back[8] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};
    for (int f = 0; f < 8; f++) {
        p.b[f]      = (int8_t)-back[f];
        p.b[8 + f]  = -PAWN;
        p.b[48 + f] = PAWN;
        p.b[56 + f] = back[f];
    }
    p.castle = CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ;
    p.ep = -1;
    p.side = 0;
    p.half = 0;
    p.king[0] = 60;
    p.king[1] = 4;
}

inline constexpr int8_t PST_PAWN[64] = {
    0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
    5,  5,  10, 25, 25, 10, 5,  5,
    0,  0,  0,  20, 20, 0,  0,  0,
    5,  -5, -10, 0, 0,  -10, -5, 5,
    5,  10, 10, -20, -20, 10, 10, 5,
    0,  0,  0,  0,  0,  0,  0,  0};

inline constexpr int8_t PST_KNIGHT[64] = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20, 0,   0,   0,   0,   -20, -40,
    -30, 0,   10,  15,  15,  10,  0,   -30,
    -30, 5,   15,  20,  20,  15,  5,   -30,
    -30, 0,   15,  20,  20,  15,  0,   -30,
    -30, 5,   10,  15,  15,  10,  5,   -30,
    -40, -20, 0,   5,   5,   0,   -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50};

inline constexpr int8_t PST_BISHOP[64] = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10, 0,   0,   0,   0,   0,   0,   -10,
    -10, 0,   5,   10,  10,  5,   0,   -10,
    -10, 5,   5,   10,  10,  5,   5,   -10,
    -10, 0,   10,  10,  10,  10,  0,   -10,
    -10, 10,  10,  10,  10,  10,  10,  -10,
    -10, 5,   0,   0,   0,   0,   5,   -10,
    -20, -10, -10, -10, -10, -10, -10, -20};

inline constexpr int8_t PST_ROOK[64] = {
    0,  0,  0,  0,  0,  0,  0,  0,
    5,  10, 10, 10, 10, 10, 10, 5,
    -5, 0,  0,  0,  0,  0,  0,  -5,
    -5, 0,  0,  0,  0,  0,  0,  -5,
    -5, 0,  0,  0,  0,  0,  0,  -5,
    -5, 0,  0,  0,  0,  0,  0,  -5,
    -5, 0,  0,  0,  0,  0,  0,  -5,
    0,  0,  0,  5,  5,  0,  0,  0};

inline constexpr int8_t PST_QUEEN[64] = {
    -20, -10, -10, -5, -5, -10, -10, -20,
    -10, 0,   0,   0,  0,  0,   0,   -10,
    -10, 0,   5,   5,  5,  5,   0,   -10,
    -5,  0,   5,   5,  5,  5,   0,   -5,
    0,   0,   5,   5,  5,  5,   0,   -5,
    -10, 5,   5,   5,  5,  5,   0,   -10,
    -10, 0,   5,   0,  0,  0,   0,   -10,
    -20, -10, -10, -5, -5, -10, -10, -20};

inline constexpr int8_t PST_KING_MID[64] = {
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -10, -20, -20, -20, -20, -20, -20, -10,
    20,  20,  0,   0,   0,   0,   20,  20,
    20,  30,  10,  0,   0,   10,  30,  20};

inline constexpr int8_t PST_KING_END[64] = {
    -50, -40, -30, -20, -20, -30, -40, -50,
    -30, -20, -10, 0,   0,   -10, -20, -30,
    -30, -10, 20,  30,  30,  20,  -10, -30,
    -30, -10, 30,  40,  40,  30,  -10, -30,
    -30, -10, 30,  40,  40,  30,  -10, -30,
    -30, -10, 20,  30,  30,  20,  -10, -30,
    -30, -30, 0,   0,   0,   0,   -30, -30,
    -50, -30, -30, -30, -30, -30, -30, -50};

inline constexpr int PIECE_VALUE[7] = {0, 100, 320, 330, 500, 900, 0};

inline constexpr int KNIGHT_DR[8] = {-2, -2, -1, -1, 1, 1, 2, 2};
inline constexpr int KNIGHT_DF[8] = {-1, 1, -2, 2, -2, 2, -1, 1};
inline constexpr int KING_DR[8]   = {-1, -1, -1, 0, 0, 1, 1, 1};
inline constexpr int KING_DF[8]   = {-1, 0, 1, -1, 1, -1, 0, 1};
inline constexpr int ROOK_DR[4]   = {-1, 1, 0, 0};
inline constexpr int ROOK_DF[4]   = {0, 0, -1, 1};
inline constexpr int BISHOP_DR[4] = {-1, -1, 1, 1};
inline constexpr int BISHOP_DF[4] = {-1, 1, -1, 1};

class Engine {
public:
    static constexpr int MAXPLY = 32;
    static constexpr int MAXM   = 128;
    static constexpr int INF    = 32000;
    static constexpr int MATE   = 29000;

    bool attacked(const Position& p, int s, int by) const {
        const int r = s >> 3;
        const int f = s & 7;
        const int8_t sg = by == 0 ? 1 : -1;

        const int pr = by == 0 ? r + 1 : r - 1;
        if (pr >= 0 && pr < 8) {
            if (f > 0 && p.b[pr * 8 + f - 1] == sg * PAWN) return true;
            if (f < 7 && p.b[pr * 8 + f + 1] == sg * PAWN) return true;
        }
        for (int i = 0; i < 8; i++) {
            const int nr = r + KNIGHT_DR[i];
            const int nf = f + KNIGHT_DF[i];
            if (nr < 0 || nr > 7 || nf < 0 || nf > 7) continue;
            if (p.b[nr * 8 + nf] == sg * KNIGHT) return true;
        }
        for (int i = 0; i < 8; i++) {
            const int nr = r + KING_DR[i];
            const int nf = f + KING_DF[i];
            if (nr < 0 || nr > 7 || nf < 0 || nf > 7) continue;
            if (p.b[nr * 8 + nf] == sg * KING) return true;
        }
        for (int i = 0; i < 4; i++) {
            int nr = r + ROOK_DR[i];
            int nf = f + ROOK_DF[i];
            while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                const int8_t t = p.b[nr * 8 + nf];
                if (t != 0) {
                    if (t == sg * ROOK || t == sg * QUEEN) return true;
                    break;
                }
                nr += ROOK_DR[i];
                nf += ROOK_DF[i];
            }
        }
        for (int i = 0; i < 4; i++) {
            int nr = r + BISHOP_DR[i];
            int nf = f + BISHOP_DF[i];
            while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                const int8_t t = p.b[nr * 8 + nf];
                if (t != 0) {
                    if (t == sg * BISHOP || t == sg * QUEEN) return true;
                    break;
                }
                nr += BISHOP_DR[i];
                nf += BISHOP_DF[i];
            }
        }
        return false;
    }

    bool inCheck(const Position& p) const { return attacked(p, p.king[p.side], p.side ^ 1); }

    static void makeMove(const Position& p, const Move& m, Position& np) {
        np = p;
        const int8_t piece = np.b[m.from];
        np.b[m.to]   = piece;
        np.b[m.from] = 0;

        if (m.flags & FLAG_EP) {
            np.b[p.side == 0 ? m.to + 8 : m.to - 8] = 0;
        }
        if (m.promo) {
            np.b[m.to] = p.side == 0 ? m.promo : (int8_t)-m.promo;
        }
        if (m.flags & FLAG_CASTLE) {
            if (m.to == 62) { np.b[61] = np.b[63]; np.b[63] = 0; }
            else if (m.to == 58) { np.b[59] = np.b[56]; np.b[56] = 0; }
            else if (m.to == 6) { np.b[5] = np.b[7]; np.b[7] = 0; }
            else if (m.to == 2) { np.b[3] = np.b[0]; np.b[0] = 0; }
        }
        if (piece == KING || piece == -KING) {
            np.king[p.side] = (int8_t)m.to;
        }

        np.ep = (m.flags & FLAG_DOUBLE) ? (int8_t)((m.from + m.to) / 2) : (int8_t)-1;

        const int touched[2] = {m.from, m.to};
        for (int i = 0; i < 2; i++) {
            switch (touched[i]) {
                case 60: np.castle &= (uint8_t)~(CASTLE_WK | CASTLE_WQ); break;
                case 63: np.castle &= (uint8_t)~CASTLE_WK; break;
                case 56: np.castle &= (uint8_t)~CASTLE_WQ; break;
                case 4:  np.castle &= (uint8_t)~(CASTLE_BK | CASTLE_BQ); break;
                case 7:  np.castle &= (uint8_t)~CASTLE_BK; break;
                case 0:  np.castle &= (uint8_t)~CASTLE_BQ; break;
                default: break;
            }
        }

        if (piece == PAWN || piece == -PAWN || (m.flags & FLAG_CAPTURE)) {
            np.half = 0;
        } else if (np.half < 250) {
            np.half++;
        }
        np.side = p.side ^ 1;
    }

    int genMoves(const Position& p, Move* out, bool capsOnly) const {
        int n = 0;
        const int8_t own = p.side == 0 ? 1 : -1;

        for (int s = 0; s < 64; s++) {
            const int8_t piece = p.b[s];
            if (piece == 0 || (piece > 0) != (own > 0)) continue;
            const int type = piece > 0 ? piece : -piece;
            const int r = s >> 3;
            const int f = s & 7;

            if (type == PAWN) {
                const int dir = own > 0 ? -1 : 1;
                const int startRow = own > 0 ? 6 : 1;
                const int nr = r + dir;
                if (nr < 0 || nr > 7) continue;
                const bool promoRow = (nr == 0 || nr == 7);

                if (p.b[nr * 8 + f] == 0) {
                    if (promoRow) {
                        add(out, n, s, nr * 8 + f, QUEEN, 0);
                        add(out, n, s, nr * 8 + f, ROOK, 0);
                        add(out, n, s, nr * 8 + f, BISHOP, 0);
                        add(out, n, s, nr * 8 + f, KNIGHT, 0);
                    } else if (!capsOnly) {
                        add(out, n, s, nr * 8 + f, 0, 0);
                        if (r == startRow && p.b[(r + 2 * dir) * 8 + f] == 0) {
                            add(out, n, s, (r + 2 * dir) * 8 + f, 0, FLAG_DOUBLE);
                        }
                    }
                }
                for (int df = -1; df <= 1; df += 2) {
                    const int nf = f + df;
                    if (nf < 0 || nf > 7) continue;
                    const int to = nr * 8 + nf;
                    const int8_t t = p.b[to];
                    if (t != 0 && (t > 0) != (own > 0)) {
                        if (promoRow) {
                            add(out, n, s, to, QUEEN, FLAG_CAPTURE);
                            add(out, n, s, to, ROOK, FLAG_CAPTURE);
                            add(out, n, s, to, BISHOP, FLAG_CAPTURE);
                            add(out, n, s, to, KNIGHT, FLAG_CAPTURE);
                        } else {
                            add(out, n, s, to, 0, FLAG_CAPTURE);
                        }
                    } else if (t == 0 && to == p.ep) {
                        add(out, n, s, to, 0, FLAG_CAPTURE | FLAG_EP);
                    }
                }
            } else if (type == KNIGHT || type == KING) {
                const int* dr = type == KNIGHT ? KNIGHT_DR : KING_DR;
                const int* df = type == KNIGHT ? KNIGHT_DF : KING_DF;
                for (int i = 0; i < 8; i++) {
                    const int nr = r + dr[i];
                    const int nf = f + df[i];
                    if (nr < 0 || nr > 7 || nf < 0 || nf > 7) continue;
                    const int to = nr * 8 + nf;
                    const int8_t t = p.b[to];
                    if (t == 0) {
                        if (!capsOnly) add(out, n, s, to, 0, 0);
                    } else if ((t > 0) != (own > 0)) {
                        add(out, n, s, to, 0, FLAG_CAPTURE);
                    }
                }
            } else {
                for (int pass = 0; pass < 2; pass++) {
                    const bool straight = pass == 0;
                    if (straight && type == BISHOP) continue;
                    if (!straight && type == ROOK) continue;
                    const int* dr = straight ? ROOK_DR : BISHOP_DR;
                    const int* df = straight ? ROOK_DF : BISHOP_DF;
                    for (int i = 0; i < 4; i++) {
                        int nr = r + dr[i];
                        int nf = f + df[i];
                        while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                            const int to = nr * 8 + nf;
                            const int8_t t = p.b[to];
                            if (t == 0) {
                                if (!capsOnly) add(out, n, s, to, 0, 0);
                            } else {
                                if ((t > 0) != (own > 0)) add(out, n, s, to, 0, FLAG_CAPTURE);
                                break;
                            }
                            nr += dr[i];
                            nf += df[i];
                        }
                    }
                }
            }
        }

        if (!capsOnly) {
            if (p.side == 0 && p.king[0] == 60 && !attacked(p, 60, 1)) {
                if ((p.castle & CASTLE_WK) && p.b[63] == ROOK && p.b[61] == 0 && p.b[62] == 0 &&
                    !attacked(p, 61, 1) && !attacked(p, 62, 1)) {
                    add(out, n, 60, 62, 0, FLAG_CASTLE);
                }
                if ((p.castle & CASTLE_WQ) && p.b[56] == ROOK && p.b[59] == 0 && p.b[58] == 0 && p.b[57] == 0 &&
                    !attacked(p, 59, 1) && !attacked(p, 58, 1)) {
                    add(out, n, 60, 58, 0, FLAG_CASTLE);
                }
            } else if (p.side == 1 && p.king[1] == 4 && !attacked(p, 4, 0)) {
                if ((p.castle & CASTLE_BK) && p.b[7] == -ROOK && p.b[5] == 0 && p.b[6] == 0 &&
                    !attacked(p, 5, 0) && !attacked(p, 6, 0)) {
                    add(out, n, 4, 6, 0, FLAG_CASTLE);
                }
                if ((p.castle & CASTLE_BQ) && p.b[0] == -ROOK && p.b[3] == 0 && p.b[2] == 0 && p.b[1] == 0 &&
                    !attacked(p, 3, 0) && !attacked(p, 2, 0)) {
                    add(out, n, 4, 2, 0, FLAG_CASTLE);
                }
            }
        }
        return n;
    }

    int legalMoves(const Position& p, Move* out, int maxOut) const {
        Move tmp[MAXM];
        Position np;
        const int n = genMoves(p, tmp, false);
        int count = 0;
        for (int i = 0; i < n && count < maxOut; i++) {
            makeMove(p, tmp[i], np);
            if (attacked(np, np.king[p.side], np.side)) continue;
            out[count++] = tmp[i];
        }
        return count;
    }

    static bool insufficientMaterial(const Position& p) {
        int minors = 0;
        for (int s = 0; s < 64; s++) {
            const int t = p.b[s] > 0 ? p.b[s] : -p.b[s];
            if (t == 0 || t == KING) continue;
            if (t == KNIGHT || t == BISHOP) {
                minors++;
            } else {
                return false;
            }
        }
        return minors <= 1;
    }

    int evaluate(const Position& p) const {
        int score = 0;
        int nonPawn = 0;
        for (int s = 0; s < 64; s++) {
            const int8_t piece = p.b[s];
            if (piece == 0) continue;
            const int type = piece > 0 ? piece : -piece;
            if (type != PAWN && type != KING) nonPawn += PIECE_VALUE[type];
        }
        const bool endgame = nonPawn <= 2600;
        for (int s = 0; s < 64; s++) {
            const int8_t piece = p.b[s];
            if (piece == 0) continue;
            const bool white = piece > 0;
            const int type = white ? piece : -piece;
            const int idx = white ? s : (s ^ 56);
            int v = PIECE_VALUE[type];
            switch (type) {
                case PAWN:   v += PST_PAWN[idx]; break;
                case KNIGHT: v += PST_KNIGHT[idx]; break;
                case BISHOP: v += PST_BISHOP[idx]; break;
                case ROOK:   v += PST_ROOK[idx]; break;
                case QUEEN:  v += PST_QUEEN[idx]; break;
                default:     v += endgame ? PST_KING_END[idx] : PST_KING_MID[idx]; break;
            }
            score += white ? v : -v;
        }
        return p.side == 0 ? score : -score;
    }

    bool findBest(const Position& pos, const Level& lv, Move& out) {
        Move pseudo[MAXM];
        const int pn = genMoves(pos, pseudo, false);
        Position& c = posBuf_[1];
        int n = 0;
        for (int i = 0; i < pn; i++) {
            makeMove(pos, pseudo[i], c);
            if (attacked(c, c.king[pos.side], c.side)) continue;
            rootMoves_[n] = pseudo[i];
            rootOrder_[n] = orderScore(pos, pseudo[i]);
            n++;
        }
        if (n == 0) return false;

        for (int i = 1; i < n; i++) {
            const Move m = rootMoves_[i];
            const int sc = rootOrder_[i];
            int j = i - 1;
            while (j >= 0 && rootOrder_[j] < sc) {
                rootMoves_[j + 1] = rootMoves_[j];
                rootOrder_[j + 1] = rootOrder_[j];
                j--;
            }
            rootMoves_[j + 1] = m;
            rootOrder_[j + 1] = sc;
        }

        Move bestMove = rootMoves_[0];
        aborted_ = false;
        useQuiescence_ = lv.quiescence;
        deadline_ = millis() + lv.timeMs;
        nodes_ = 0;

        for (int d = 1; d <= lv.depth; d++) {
            checkTime_ = d > 1;
            int alpha = -INF;
            int iterBest = -INF;
            int iterIdx = 0;
            int iterRaw = -INF;

            for (int i = 0; i < n; i++) {
                makeMove(pos, rootMoves_[i], c);
                int sc;
                if (lv.noise > 0) {
                    sc = -negamax(c, d - 1, -INF, INF, 1);
                } else {
                    sc = -negamax(c, d - 1, -INF, -alpha, 1);
                }
                if (aborted_) break;
                const int adj = sc + (lv.noise > 0 ? (int)random(0, lv.noise + 1) : 0);
                if (adj > iterBest) {
                    iterBest = adj;
                    iterIdx = i;
                    iterRaw = sc;
                }
                if (lv.noise == 0 && sc > alpha) alpha = sc;
            }
            if (aborted_) break;

            bestMove = rootMoves_[iterIdx];
            const Move keep = rootMoves_[iterIdx];
            for (int i = iterIdx; i > 0; i--) rootMoves_[i] = rootMoves_[i - 1];
            rootMoves_[0] = keep;

            if (iterRaw > MATE - 100) break;
        }

        out = bestMove;
        return true;
    }

    uint32_t lastNodes() const { return nodes_; }

#ifdef CHESS_TEST
    uint64_t perft(const Position& p, int depth, int ply) {
        if (depth == 0) return 1;
        const int n = genMoves(p, moves_[ply], false);
        uint64_t total = 0;
        Position& c = posBuf_[ply + 1];
        for (int i = 0; i < n; i++) {
            makeMove(p, moves_[ply][i], c);
            if (attacked(c, c.king[p.side], c.side)) continue;
            total += perft(c, depth - 1, ply + 1);
        }
        return total;
    }

    static bool setFen(Position& p, const char* fen) {
        memset(&p, 0, sizeof(p));
        p.ep = -1;
        int r = 0;
        int f = 0;
        const char* s = fen;
        for (; *s && *s != ' '; s++) {
            const char ch = *s;
            if (ch == '/') { r++; f = 0; continue; }
            if (ch >= '1' && ch <= '8') { f += ch - '0'; continue; }
            int8_t t = 0;
            const char lower = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
            switch (lower) {
                case 'p': t = PAWN; break;
                case 'n': t = KNIGHT; break;
                case 'b': t = BISHOP; break;
                case 'r': t = ROOK; break;
                case 'q': t = QUEEN; break;
                case 'k': t = KING; break;
                default: return false;
            }
            const bool white = ch >= 'A' && ch <= 'Z';
            p.b[r * 8 + f] = white ? t : (int8_t)-t;
            if (t == KING) p.king[white ? 0 : 1] = (int8_t)(r * 8 + f);
            f++;
        }
        if (*s == ' ') s++;
        p.side = (*s == 'b') ? 1 : 0;
        while (*s && *s != ' ') s++;
        if (*s == ' ') s++;
        for (; *s && *s != ' '; s++) {
            if (*s == 'K') p.castle |= CASTLE_WK;
            if (*s == 'Q') p.castle |= CASTLE_WQ;
            if (*s == 'k') p.castle |= CASTLE_BK;
            if (*s == 'q') p.castle |= CASTLE_BQ;
        }
        if (*s == ' ') s++;
        if (*s >= 'a' && *s <= 'h' && s[1] >= '1' && s[1] <= '8') {
            p.ep = (int8_t)(('8' - s[1]) * 8 + (*s - 'a'));
        }
        return true;
    }
#endif

private:
    Move moves_[MAXPLY][MAXM];
    int16_t scores_[MAXPLY][MAXM];
    Position posBuf_[MAXPLY + 2];
    Move rootMoves_[MAXM];
    int rootOrder_[MAXM];

    bool aborted_ = false;
    bool checkTime_ = false;
    bool useQuiescence_ = true;
    unsigned long deadline_ = 0;
    uint32_t nodes_ = 0;

    static void add(Move* out, int& n, int from, int to, int promo, uint8_t flags) {
        if (n >= MAXM) return;
        out[n].from = (uint8_t)from;
        out[n].to = (uint8_t)to;
        out[n].promo = (int8_t)promo;
        out[n].flags = flags;
        n++;
    }

    static int orderScore(const Position& p, const Move& m) {
        int sc = 0;
        if (m.flags & FLAG_CAPTURE) {
            const int8_t v = (m.flags & FLAG_EP) ? (int8_t)PAWN : p.b[m.to];
            const int vt = v > 0 ? v : -v;
            const int8_t a = p.b[m.from];
            const int at = a > 0 ? a : -a;
            sc += 1000 + PIECE_VALUE[vt] * 10 - PIECE_VALUE[at];
        }
        if (m.promo) sc += 800 + PIECE_VALUE[m.promo];
        return sc;
    }

    bool tick() {
        nodes_++;
        if ((nodes_ & 255) == 0 && checkTime_ && millis() > deadline_) aborted_ = true;
        if ((nodes_ & 4095) == 0) delay(1);
        return aborted_;
    }

    void pickBest(int ply, int n, int i) {
        int bi = i;
        for (int j = i + 1; j < n; j++) {
            if (scores_[ply][j] > scores_[ply][bi]) bi = j;
        }
        if (bi != i) {
            const Move tm = moves_[ply][i];
            moves_[ply][i] = moves_[ply][bi];
            moves_[ply][bi] = tm;
            const int16_t ts = scores_[ply][i];
            scores_[ply][i] = scores_[ply][bi];
            scores_[ply][bi] = ts;
        }
    }

    int quiesce(const Position& p, int alpha, int beta, int ply, int qdepth) {
        if (tick()) return 0;
        const int stand = evaluate(p);
        if (stand >= beta) return stand;
        if (stand > alpha) alpha = stand;
        if (qdepth <= 0 || ply >= MAXPLY - 2) return alpha;

        const int n = genMoves(p, moves_[ply], true);
        for (int i = 0; i < n; i++) scores_[ply][i] = (int16_t)orderScore(p, moves_[ply][i]);
        Position& c = posBuf_[ply + 1];
        for (int i = 0; i < n; i++) {
            pickBest(ply, n, i);
            makeMove(p, moves_[ply][i], c);
            if (attacked(c, c.king[p.side], c.side)) continue;
            const int sc = -quiesce(c, -beta, -alpha, ply + 1, qdepth - 1);
            if (aborted_) return 0;
            if (sc >= beta) return sc;
            if (sc > alpha) alpha = sc;
        }
        return alpha;
    }

    int negamax(const Position& p, int depth, int alpha, int beta, int ply) {
        if (tick()) return 0;
        if (p.half >= 100) return 0;

        const bool check = inCheck(p);
        if (check && ply < 20) depth++;

        if (depth <= 0) {
            return useQuiescence_ ? quiesce(p, alpha, beta, ply, 4) : evaluate(p);
        }
        if (ply >= MAXPLY - 6) return evaluate(p);

        const int n = genMoves(p, moves_[ply], false);
        for (int i = 0; i < n; i++) scores_[ply][i] = (int16_t)orderScore(p, moves_[ply][i]);

        Position& c = posBuf_[ply + 1];
        int best = -INF;
        int legal = 0;
        for (int i = 0; i < n; i++) {
            pickBest(ply, n, i);
            makeMove(p, moves_[ply][i], c);
            if (attacked(c, c.king[p.side], c.side)) continue;
            legal++;
            const int sc = -negamax(c, depth - 1, -beta, -alpha, ply + 1);
            if (aborted_) return 0;
            if (sc > best) best = sc;
            if (sc > alpha) alpha = sc;
            if (alpha >= beta) break;
        }
        if (legal == 0) return check ? -MATE + ply : 0;
        return best;
    }
};

}
