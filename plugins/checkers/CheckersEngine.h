#pragma once

#include <cstdint>
#include <cstring>

#if defined(__has_include)
#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif
#endif

namespace checkers {

struct Move {
    uint8_t from;
    uint8_t to;
    int8_t cap;
};

struct Position {
    int8_t b[64];
    uint8_t side;
    int8_t chain;
    uint8_t half;
};

struct Level {
    const char* name;
    const char* hint;
    int depth;
    unsigned long timeMs;
    int noise;
};

inline bool samePosition(const Position& a, const Position& c) {
    return a.side == c.side && a.chain == c.chain && memcmp(a.b, c.b, 64) == 0;
}

inline void startPosition(Position& p) {
    memset(&p, 0, sizeof(p));
    for (int r = 0; r < 8; r++) {
        for (int f = 0; f < 8; f++) {
            if (((r + f) & 1) == 0) continue;
            if (r < 3) p.b[r * 8 + f] = -1;
            if (r > 4) p.b[r * 8 + f] = 1;
        }
    }
    p.side = 0;
    p.chain = -1;
    p.half = 0;
}

class Engine {
public:
    static constexpr int MAXPLY = 40;
    static constexpr int MAXM   = 64;
    static constexpr int INF    = 32000;
    static constexpr int MATE   = 29000;
    static constexpr int DRAW_HALF = 80;

    static bool ownPiece(const Position& p, int sq) {
        const int8_t v = p.b[sq];
        return v != 0 && ((v > 0) == (p.side == 0));
    }

    static int countPieces(const Position& p, int side) {
        int n = 0;
        for (int s = 0; s < 64; s++) {
            const int8_t v = p.b[s];
            if (v != 0 && ((v > 0) == (side == 0))) n++;
        }
        return n;
    }

    static bool hasCapture(const Position& p, int sq) {
        const int8_t v = p.b[sq];
        if (v == 0) return false;
        const bool white = v > 0;
        const bool king = v == 2 || v == -2;
        const int r = sq >> 3;
        const int f = sq & 7;
        for (int dr = -1; dr <= 1; dr += 2) {
            if (!king && ((white && dr != -1) || (!white && dr != 1))) continue;
            for (int df = -1; df <= 1; df += 2) {
                const int mr = r + dr;
                const int mf = f + df;
                const int lr = r + 2 * dr;
                const int lf = f + 2 * df;
                if (lr < 0 || lr > 7 || lf < 0 || lf > 7) continue;
                const int8_t mid = p.b[mr * 8 + mf];
                if (mid == 0 || ((mid > 0) == white)) continue;
                if (p.b[lr * 8 + lf] == 0) return true;
            }
        }
        return false;
    }

    int genMoves(const Position& p, Move* out) const {
        int caps = 0;
        int simple = 0;
        Move capList[MAXM];
        Move simpleList[MAXM];

        for (int s = 0; s < 64; s++) {
            if (!ownPiece(p, s)) continue;
            if (p.chain >= 0 && s != p.chain) continue;
            const int8_t v = p.b[s];
            const bool white = v > 0;
            const bool king = v == 2 || v == -2;
            const int r = s >> 3;
            const int f = s & 7;
            for (int dr = -1; dr <= 1; dr += 2) {
                if (!king && ((white && dr != -1) || (!white && dr != 1))) continue;
                for (int df = -1; df <= 1; df += 2) {
                    const int nr = r + dr;
                    const int nf = f + df;
                    if (nr < 0 || nr > 7 || nf < 0 || nf > 7) continue;
                    const int8_t t = p.b[nr * 8 + nf];
                    if (t == 0) {
                        if (p.chain < 0 && simple < MAXM) {
                            simpleList[simple++] = {(uint8_t)s, (uint8_t)(nr * 8 + nf), (int8_t)-1};
                        }
                    } else if ((t > 0) != white) {
                        const int lr = r + 2 * dr;
                        const int lf = f + 2 * df;
                        if (lr < 0 || lr > 7 || lf < 0 || lf > 7) continue;
                        if (p.b[lr * 8 + lf] == 0 && caps < MAXM) {
                            capList[caps++] = {(uint8_t)s, (uint8_t)(lr * 8 + lf), (int8_t)(nr * 8 + nf)};
                        }
                    }
                }
            }
        }

        if (caps > 0) {
            for (int i = 0; i < caps; i++) out[i] = capList[i];
            return caps;
        }
        for (int i = 0; i < simple; i++) out[i] = simpleList[i];
        return simple;
    }

    static void makeMove(const Position& p, const Move& m, Position& np) {
        np = p;
        int8_t piece = np.b[m.from];
        const bool wasMan = piece == 1 || piece == -1;
        np.b[m.from] = 0;
        bool promoted = false;
        if (wasMan) {
            const int row = m.to >> 3;
            if ((piece > 0 && row == 0) || (piece < 0 && row == 7)) {
                piece = piece > 0 ? 2 : -2;
                promoted = true;
            }
        }
        np.b[m.to] = piece;

        if (m.cap >= 0) {
            np.b[m.cap] = 0;
            np.half = 0;
        } else if (wasMan) {
            np.half = 0;
        } else if (np.half < 250) {
            np.half++;
        }

        if (m.cap >= 0 && !promoted && hasCapture(np, m.to)) {
            np.chain = (int8_t)m.to;
            np.side = p.side;
        } else {
            np.chain = -1;
            np.side = p.side ^ 1;
        }
    }

    int legalMoves(const Position& p, Move* out) const { return genMoves(p, out); }

    int evaluate(const Position& p) const {
        int score = 0;
        for (int s = 0; s < 64; s++) {
            const int8_t v = p.b[s];
            if (v == 0) continue;
            const bool white = v > 0;
            const int r = s >> 3;
            const int f = s & 7;
            const bool king = v == 2 || v == -2;
            int val = king ? 180 : 100;
            if (!king) {
                val += (white ? (7 - r) : r) * 4;
                if ((white && r == 7) || (!white && r == 0)) val += 8;
            }
            const int cr = r < 4 ? 3 - r : r - 4;
            const int cf = f < 4 ? 3 - f : f - 4;
            val += (3 - cr) + (3 - cf);
            score += white ? val : -val;
        }
        return p.side == 0 ? score : -score;
    }

    bool findBest(const Position& pos, const Level& lv, Move& out) {
        Move list[MAXM];
        const int n = genMoves(pos, list);
        if (n == 0) return false;
        for (int i = 0; i < n; i++) rootMoves_[i] = list[i];

        Move bestMove = rootMoves_[0];
        aborted_ = false;
        deadline_ = millis() + lv.timeMs;
        nodes_ = 0;
        Position& c = posBuf_[1];

        if (n == 1) {
            out = bestMove;
            return true;
        }

        for (int d = 1; d <= lv.depth; d++) {
            checkTime_ = d > 1;
            int alpha = -INF;
            int iterBest = -INF;
            int iterRaw = -INF;
            int iterIdx = 0;

            for (int i = 0; i < n; i++) {
                makeMove(pos, rootMoves_[i], c);
                int sc;
                if (lv.noise > 0) {
                    sc = child(c, pos.side, d - 1, -INF, INF, 1);
                } else {
                    sc = child(c, pos.side, d - 1, alpha, INF, 1);
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

            if (iterRaw > MATE - 100 || iterRaw < -MATE + 100) break;
        }

        out = bestMove;
        return true;
    }

#ifdef CHECKERS_TEST
    uint64_t perft(const Position& p, int depth, int ply) {
        if (depth == 0) return 1;
        Move list[MAXM];
        const int n = genMoves(p, list);
        uint64_t total = 0;
        Position& c = posBuf_[ply + 1];
        for (int i = 0; i < n; i++) {
            makeMove(p, list[i], c);
            if (c.side == p.side) {
                total += perft(c, depth, ply + 1);
            } else {
                total += perft(c, depth - 1, ply + 1);
            }
        }
        return total;
    }
#endif

private:
    Move moves_[MAXPLY][MAXM];
    Position posBuf_[MAXPLY + 2];
    Move rootMoves_[MAXM];

    bool aborted_ = false;
    bool checkTime_ = false;
    unsigned long deadline_ = 0;
    uint32_t nodes_ = 0;

    bool tick() {
        nodes_++;
        if ((nodes_ & 255) == 0 && checkTime_ && millis() > deadline_) aborted_ = true;
        if ((nodes_ & 4095) == 0) delay(1);
        return aborted_;
    }

    int child(const Position& c, uint8_t parentSide, int depth, int alpha, int beta, int ply) {
        if (c.side == parentSide) return negamax(c, depth, alpha, beta, ply);
        return -negamax(c, depth, -beta, -alpha, ply);
    }

    int negamax(const Position& p, int depth, int alpha, int beta, int ply) {
        if (tick()) return 0;
        if (p.half >= DRAW_HALF) return 0;

        Move* list = moves_[ply];
        const int n = genMoves(p, list);
        if (n == 0) return -MATE + ply;

        if (depth <= 0) {
            const bool forced = list[0].cap >= 0;
            if (!forced || ply >= MAXPLY - 4) return evaluate(p);
        }
        if (ply >= MAXPLY - 2) return evaluate(p);

        Position& c = posBuf_[ply + 1];
        int best = -INF;
        for (int i = 0; i < n; i++) {
            makeMove(p, list[i], c);
            const int sc = child(c, p.side, depth - 1, alpha, beta, ply + 1);
            if (aborted_) return 0;
            if (sc > best) best = sc;
            if (sc > alpha) alpha = sc;
            if (alpha >= beta) break;
        }
        return best;
    }
};

}
