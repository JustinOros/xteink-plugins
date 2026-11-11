#pragma once

#include <cstdint>
#include <cstring>

#if defined(__has_include)
#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif
#endif

namespace solitaire {

constexpr uint8_t UP = 0x40;
constexpr int COLS = 7;
constexpr int MAXT = 24;
constexpr int MAXS = 24;
constexpr int MAX_UNDO = 100;

enum class Src : uint8_t { NONE, WASTE, FOUND, TAB };

inline int rankOf(int id) { return id % 13; }
inline int suitOf(int id) { return id / 13; }
inline bool isRed(int id) {
    const int s = id / 13;
    return s == 1 || s == 2;
}

struct State {
    uint8_t tab[COLS][MAXT];
    uint8_t tabN[COLS];
    uint8_t stock[MAXS];
    uint8_t stockN;
    uint8_t waste[MAXS];
    uint8_t wasteN;
    uint8_t found[4];
    uint16_t moves;
};

class Game {
public:
    State s;
    int drawCount = 1;

    void deal(int draw) {
        drawCount = draw;
        memset(&s, 0, sizeof(s));
        undoCount_ = 0;

        uint8_t deck[52];
        for (int i = 0; i < 52; i++) deck[i] = (uint8_t)i;
        for (int i = 51; i > 0; i--) {
            const int j = (int)random(0, i + 1);
            const uint8_t t = deck[i];
            deck[i] = deck[j];
            deck[j] = t;
        }

        int k = 0;
        for (int c = 0; c < COLS; c++) {
            for (int r = 0; r <= c; r++) {
                uint8_t v = deck[k++];
                if (r == c) v |= UP;
                s.tab[c][s.tabN[c]++] = v;
            }
        }
        while (k < 52) s.stock[s.stockN++] = deck[k++];
    }

    bool won() const { return s.found[0] == 13 && s.found[1] == 13 && s.found[2] == 13 && s.found[3] == 13; }
    bool canUndo() const { return undoCount_ > 0; }

    void undo() {
        if (undoCount_ == 0) return;
        undoCount_--;
        s = undo_[undoCount_];
    }

    bool draw() {
        if (s.stockN > 0) {
            pushUndo();
            int n = drawCount < s.stockN ? drawCount : s.stockN;
            for (int i = 0; i < n; i++) s.waste[s.wasteN++] = s.stock[--s.stockN];
            s.moves++;
            return true;
        }
        if (s.wasteN > 0) {
            pushUndo();
            for (int i = s.wasteN - 1; i >= 0; i--) s.stock[s.stockN++] = s.waste[i];
            s.wasteN = 0;
            s.moves++;
            return true;
        }
        return false;
    }

    int srcFirst(Src t, int a, int idx) const {
        switch (t) {
            case Src::WASTE:
                return s.wasteN ? s.waste[s.wasteN - 1] : -1;
            case Src::FOUND:
                return s.found[a] ? a * 13 + s.found[a] - 1 : -1;
            case Src::TAB:
                if (idx < 0 || idx >= s.tabN[a]) return -1;
                if (!(s.tab[a][idx] & UP)) return -1;
                return s.tab[a][idx] & 0x3F;
            default:
                return -1;
        }
    }

    int srcCount(Src t, int a, int idx) const {
        if (t == Src::TAB) return s.tabN[a] - idx;
        return 1;
    }

    bool canTab(int id, int col) const {
        if (s.tabN[col] == 0) return rankOf(id) == 12;
        const uint8_t top = s.tab[col][s.tabN[col] - 1];
        if (!(top & UP)) return false;
        const int tid = top & 0x3F;
        return rankOf(tid) == rankOf(id) + 1 && isRed(tid) != isRed(id);
    }

    bool canFound(int id) const { return s.found[suitOf(id)] == rankOf(id); }

    bool moveToTab(Src t, int a, int idx, int dest) {
        const int id = srcFirst(t, a, idx);
        if (id < 0) return false;
        if (t == Src::TAB && a == dest) return false;
        if (!canTab(id, dest)) return false;
        pushUndo();
        performTab(t, a, idx, dest);
        s.moves++;
        return true;
    }

    bool moveToFound(Src t, int a, int idx) {
        if (t == Src::FOUND) return false;
        const int id = srcFirst(t, a, idx);
        if (id < 0 || srcCount(t, a, idx) != 1) return false;
        if (!canFound(id)) return false;
        pushUndo();
        performFound(t, a, idx);
        s.moves++;
        return true;
    }

    int autoAll() {
        bool saved = false;
        int moved = 0;
        for (;;) {
            bool any = false;
            if (s.wasteN && canFound(s.waste[s.wasteN - 1])) {
                if (!saved) { pushUndo(); saved = true; }
                performFound(Src::WASTE, 0, 0);
                s.moves++;
                moved++;
                any = true;
            }
            for (int c = 0; c < COLS; c++) {
                if (s.tabN[c] == 0) continue;
                const uint8_t top = s.tab[c][s.tabN[c] - 1];
                if ((top & UP) && canFound(top & 0x3F)) {
                    if (!saved) { pushUndo(); saved = true; }
                    performFound(Src::TAB, c, s.tabN[c] - 1);
                    s.moves++;
                    moved++;
                    any = true;
                }
            }
            if (!any) break;
        }
        return moved;
    }

    bool smartMove(Src t, int a, int idx) {
        if (moveToFound(t, a, idx)) return true;
        const int id = srcFirst(t, a, idx);
        if (id < 0) return false;
        for (int c = 0; c < COLS; c++) {
            if (t == Src::TAB && c == a) continue;
            if (s.tabN[c] == 0) continue;
            if (canTab(id, c)) return moveToTab(t, a, idx, c);
        }
        if (rankOf(id) == 12) {
            const bool alreadyBottom = t == Src::TAB && idx == 0;
            if (!alreadyBottom) {
                for (int c = 0; c < COLS; c++) {
                    if (s.tabN[c] == 0) return moveToTab(t, a, idx, c);
                }
            }
        }
        return false;
    }

    int cardTotal() const {
        int n = s.stockN + s.wasteN;
        for (int c = 0; c < COLS; c++) n += s.tabN[c];
        for (int k = 0; k < 4; k++) n += s.found[k];
        return n;
    }

private:
    State undo_[MAX_UNDO];
    int undoCount_ = 0;

    void pushUndo() {
        if (undoCount_ >= MAX_UNDO) {
            memmove(undo_, undo_ + 1, sizeof(State) * (MAX_UNDO - 1));
            undoCount_--;
        }
        undo_[undoCount_++] = s;
    }

    void flipTop(int col) {
        if (s.tabN[col] > 0) s.tab[col][s.tabN[col] - 1] |= UP;
    }

    void performFound(Src t, int a, int idx) {
        const int id = srcFirst(t, a, idx);
        if (t == Src::WASTE) {
            s.wasteN--;
        } else {
            s.tabN[a] = (uint8_t)idx;
            flipTop(a);
        }
        s.found[suitOf(id)]++;
    }

    void performTab(Src t, int a, int idx, int dest) {
        uint8_t run[MAXT];
        int n = 0;
        if (t == Src::TAB) {
            n = s.tabN[a] - idx;
            for (int i = 0; i < n; i++) run[i] = s.tab[a][idx + i];
            s.tabN[a] = (uint8_t)idx;
            flipTop(a);
        } else if (t == Src::WASTE) {
            run[0] = (uint8_t)(s.waste[s.wasteN - 1] | UP);
            n = 1;
            s.wasteN--;
        } else {
            run[0] = (uint8_t)((a * 13 + s.found[a] - 1) | UP);
            n = 1;
            s.found[a]--;
        }
        for (int i = 0; i < n; i++) s.tab[dest][s.tabN[dest]++] = run[i];
    }
};

}
