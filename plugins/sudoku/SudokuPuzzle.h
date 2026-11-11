#pragma once

#include <cstdint>
#include <cstring>

#if defined(__has_include)
#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif
#endif

namespace sudoku {

class Solver {
public:
    uint8_t g[81];
    uint8_t firstSolution[81];
    int solutions = 0;
    int limit = 1;
    long nodes = 0;
    long nodeLimit = 1000000;
    bool aborted = false;
    bool randomize = false;

    void init(const uint8_t* grid) {
        memset(rowM_, 0, sizeof(rowM_));
        memset(colM_, 0, sizeof(colM_));
        memset(boxM_, 0, sizeof(boxM_));
        memcpy(g, grid, 81);
        for (int i = 0; i < 81; i++) {
            if (g[i]) {
                const uint16_t bit = (uint16_t)(1u << (g[i] - 1));
                rowM_[i / 9] |= bit;
                colM_[i % 9] |= bit;
                boxM_[boxOf(i)] |= bit;
            }
        }
        solutions = 0;
        nodes = 0;
        aborted = false;
    }

    void search() {
        if (aborted || solutions >= limit) return;
        if (++nodes > nodeLimit) {
            aborted = true;
            return;
        }
        if ((nodes & 2047) == 0) delay(1);

        int best = -1;
        int bestCount = 10;
        uint16_t bestMask = 0;
        for (int i = 0; i < 81; i++) {
            if (g[i]) continue;
            const uint16_t cand = (uint16_t)(~(rowM_[i / 9] | colM_[i % 9] | boxM_[boxOf(i)]) & 0x1FF);
            const int count = popcount(cand);
            if (count == 0) return;
            if (count < bestCount) {
                bestCount = count;
                best = i;
                bestMask = cand;
                if (count == 1) break;
            }
        }

        if (best < 0) {
            solutions++;
            if (solutions == 1) memcpy(firstSolution, g, 81);
            return;
        }

        int digits[9];
        int n = 0;
        for (int d = 0; d < 9; d++) {
            if (bestMask & (1u << d)) digits[n++] = d + 1;
        }
        if (randomize) {
            for (int i = n - 1; i > 0; i--) {
                const int j = (int)random(0, i + 1);
                const int t = digits[i];
                digits[i] = digits[j];
                digits[j] = t;
            }
        }

        for (int k = 0; k < n; k++) {
            const int d = digits[k];
            const uint16_t bit = (uint16_t)(1u << (d - 1));
            g[best] = (uint8_t)d;
            rowM_[best / 9] |= bit;
            colM_[best % 9] |= bit;
            boxM_[boxOf(best)] |= bit;
            search();
            g[best] = 0;
            rowM_[best / 9] &= (uint16_t)~bit;
            colM_[best % 9] &= (uint16_t)~bit;
            boxM_[boxOf(best)] &= (uint16_t)~bit;
            if (aborted || solutions >= limit) return;
        }
    }

private:
    uint16_t rowM_[9];
    uint16_t colM_[9];
    uint16_t boxM_[9];

    static int boxOf(int i) { return (i / 27) * 3 + (i % 9) / 3; }
    static int popcount(uint16_t v) {
        int c = 0;
        while (v) {
            c += v & 1;
            v >>= 1;
        }
        return c;
    }
};

inline int generatePuzzle(int targetGivens, unsigned long budgetMs, uint8_t* puzzle, uint8_t* solution, Solver& s) {
    uint8_t empty[81];
    memset(empty, 0, sizeof(empty));
    s.init(empty);
    s.randomize = true;
    s.limit = 1;
    s.nodeLimit = 2000000;
    s.search();
    memcpy(solution, s.firstSolution, 81);
    memcpy(puzzle, solution, 81);

    int order[81];
    for (int i = 0; i < 81; i++) order[i] = i;
    for (int i = 80; i > 0; i--) {
        const int j = (int)random(0, i + 1);
        const int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }

    const unsigned long deadline = millis() + budgetMs;
    int givens = 81;
    for (int k = 0; k < 81 && givens > targetGivens; k++) {
        if (millis() > deadline) break;
        const int idx = order[k];
        const uint8_t saved = puzzle[idx];
        puzzle[idx] = 0;
        s.init(puzzle);
        s.randomize = false;
        s.limit = 2;
        s.nodeLimit = 30000;
        s.search();
        if (s.solutions == 1 && !s.aborted) {
            givens--;
        } else {
            puzzle[idx] = saved;
        }
    }
    return givens;
}

}
