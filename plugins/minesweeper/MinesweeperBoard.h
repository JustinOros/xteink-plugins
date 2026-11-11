#pragma once

#include <cstdint>
#include <cstring>

#if defined(__has_include)
#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif
#endif

namespace minesweeper {

constexpr int MAXC = 14;
constexpr int MAXR = 18;

enum class Outcome { NONE, LOST, WON };

class Board {
public:
    static constexpr uint8_t HIDDEN = 0;
    static constexpr uint8_t OPEN = 1;
    static constexpr uint8_t FLAG = 2;

    void reset(int cols, int rows, int mines) {
        cols_ = cols;
        rows_ = rows;
        mines_ = mines;
        memset(mine_, 0, sizeof(mine_));
        memset(state_, 0, sizeof(state_));
        memset(adj_, 0, sizeof(adj_));
        placed_ = false;
        revealed_ = 0;
        flags_ = 0;
        hit_ = -1;
        outcome_ = Outcome::NONE;
    }

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    int mines() const { return mines_; }
    int flagsLeft() const { return mines_ - flags_; }
    int hit() const { return hit_; }
    bool started() const { return placed_; }
    Outcome outcome() const { return outcome_; }
    bool isMine(int i) const { return mine_[i] != 0; }
    uint8_t state(int i) const { return state_[i]; }
    uint8_t adjacent(int i) const { return adj_[i]; }
    int revealedCount() const { return revealed_; }

    void toggleFlag(int i) {
        if (outcome_ != Outcome::NONE) return;
        if (state_[i] == HIDDEN) {
            state_[i] = FLAG;
            flags_++;
        } else if (state_[i] == FLAG) {
            state_[i] = HIDDEN;
            flags_--;
        }
    }

    Outcome reveal(int i) {
        if (outcome_ != Outcome::NONE) return outcome_;
        if (state_[i] != HIDDEN) return Outcome::NONE;
        if (!placed_) placeMines(i);
        return openCell(i);
    }

    Outcome chord(int i) {
        if (outcome_ != Outcome::NONE) return outcome_;
        if (state_[i] != OPEN || adj_[i] == 0) return Outcome::NONE;

        int neighbors[8];
        const int n = neighborsOf(i, neighbors);
        int flagged = 0;
        for (int k = 0; k < n; k++) {
            if (state_[neighbors[k]] == FLAG) flagged++;
        }
        if (flagged != adj_[i]) return Outcome::NONE;

        for (int k = 0; k < n; k++) {
            if (state_[neighbors[k]] == HIDDEN) {
                const Outcome o = openCell(neighbors[k]);
                if (o != Outcome::NONE) return o;
            }
        }
        return outcome_;
    }

    int neighborsOf(int i, int* out) const {
        const int r = i / cols_;
        const int c = i % cols_;
        int n = 0;
        for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
                if (dr == 0 && dc == 0) continue;
                const int nr = r + dr;
                const int nc = c + dc;
                if (nr < 0 || nr >= rows_ || nc < 0 || nc >= cols_) continue;
                out[n++] = nr * cols_ + nc;
            }
        }
        return n;
    }

private:
    int cols_ = 0;
    int rows_ = 0;
    int mines_ = 0;
    bool placed_ = false;
    int revealed_ = 0;
    int flags_ = 0;
    int hit_ = -1;
    Outcome outcome_ = Outcome::NONE;
    uint8_t mine_[MAXC * MAXR];
    uint8_t state_[MAXC * MAXR];
    uint8_t adj_[MAXC * MAXR];
    int16_t stack_[MAXC * MAXR];

    void placeMines(int safe) {
        const int total = cols_ * rows_;
        int safeCells[9];
        int safeCount = 0;
        safeCells[safeCount++] = safe;
        int nb[8];
        const int nn = neighborsOf(safe, nb);
        if (total - 9 >= mines_) {
            for (int k = 0; k < nn; k++) safeCells[safeCount++] = nb[k];
        }

        int placed = 0;
        while (placed < mines_) {
            const int idx = (int)random(0, total);
            if (mine_[idx]) continue;
            bool blocked = false;
            for (int k = 0; k < safeCount; k++) {
                if (safeCells[k] == idx) blocked = true;
            }
            if (blocked) continue;
            mine_[idx] = 1;
            placed++;
        }

        for (int i = 0; i < total; i++) {
            int around[8];
            const int n = neighborsOf(i, around);
            int count = 0;
            for (int k = 0; k < n; k++) count += mine_[around[k]];
            adj_[i] = (uint8_t)count;
        }
        placed_ = true;
    }

    Outcome openCell(int start) {
        if (mine_[start]) {
            state_[start] = OPEN;
            hit_ = start;
            outcome_ = Outcome::LOST;
            return outcome_;
        }

        int top = 0;
        stack_[top++] = (int16_t)start;
        state_[start] = OPEN;
        revealed_++;
        while (top > 0) {
            const int cur = stack_[--top];
            if (adj_[cur] != 0) continue;
            int around[8];
            const int n = neighborsOf(cur, around);
            for (int k = 0; k < n; k++) {
                const int nx = around[k];
                if (state_[nx] != HIDDEN || mine_[nx]) continue;
                state_[nx] = OPEN;
                revealed_++;
                stack_[top++] = (int16_t)nx;
            }
        }

        if (revealed_ >= cols_ * rows_ - mines_) {
            outcome_ = Outcome::WON;
            for (int i = 0; i < cols_ * rows_; i++) {
                if (mine_[i] && state_[i] != FLAG) {
                    state_[i] = FLAG;
                    flags_++;
                }
            }
        }
        return outcome_;
    }
};

}
