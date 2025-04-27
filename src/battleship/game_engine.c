#include "game_engine.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "game_logic.h"
#include <esp_random.h>


#include "game_engine.h"
#include <string.h>
#include <esp_random.h>
#include <stdio.h>

static ai_state_t smart_ai_state;
static Board ai_guess_board;

void engine_init(void) {
    ai_reset_smart(&smart_ai_state);
    init_board(&ai_guess_board,true);
}

void engine_restart(void) {
    ai_reset_smart(&smart_ai_state);
    init_board(&ai_guess_board,true);
}

void engine_update_smart_ai(int row, int col, char result) {
    ai_update_smart(&smart_ai_state, row, col, result);
    if (result == 'H' || result == 'S') {
        ai_guess_board.cells[row][col] = CELL_HIT;
    } else if (result == 'M') {
        ai_guess_board.cells[row][col] = CELL_MISS;
    }
}

Board* engine_get_ai_board(void) {
    return &ai_guess_board;
}

void engine_get_guess(difficulty_level_t difficulty, Board *ai_board, int *row, int *col) {
    switch (difficulty) {
        case DIFFICULTY_RANDOM:
            ai_guess_random(ai_board, row, col);
            break;

        case DIFFICULTY_SMART:
            ai_guess_smart(ai_board, &smart_ai_state, row, col);
            break;

        case DIFFICULTY_HARD: {
            const int ship_lengths[] = {3, 2, 2};
            uint8_t heatmap[GRID_SIZE][GRID_SIZE];
            generate_heatmap(ai_board->cells, ship_lengths, 3, heatmap);
            Coordinate best = select_best_move(heatmap, ai_board->cells);
            *row = best.row;
            *col = best.col;
            break;
        }

        default:
            ai_guess_random(ai_board, row, col);
            break;
    }
}

void ai_guess_random(const Board *guessBoard, int *row, int *col) {
    do {
        *row = esp_random() % GRID_SIZE;
        *col = esp_random() % GRID_SIZE;
    } while (guessBoard->cells[*row][*col] != CELL_EMPTY);
}




static bool is_valid_guess(const Board *board, int r, int c) {
    return r >= 0 && r < GRID_SIZE && c >= 0 && c < GRID_SIZE && board->cells[r][c] == CELL_EMPTY;
}

void ai_reset_smart(ai_state_t *state) {
    memset(state, 0, sizeof(ai_state_t));
    state->hit_count = 0;
    state->target_dir = DIR_NONE;
    state->hunting = true;
}

void ai_guess_smart(const Board *guessBoard, ai_state_t *state, int *row, int *col) {
    if (state->hunting || state->hit_count == 0) {
        // HUNT PHASE: checkerboard pattern
        for (int r = 0; r < GRID_SIZE; r++) {
            for (int c = 0; c < GRID_SIZE; c++) {
                if ((r + c) % 2 == 0 && guessBoard->cells[r][c] == CELL_EMPTY) {
                    *row = r;
                    *col = c;
                    return;
                }
            }
        }
        // Fallback
        ai_guess_random(guessBoard, row, col);
        return;
    }

    Coordinate last = state->hits[state->hit_count - 1];

    // If direction known, continue in that direction
    if (state->target_dir != DIR_NONE) {
        int dr[] = {-1, 1, 0, 0};
        int dc[] = {0, 0, -1, 1};
        int r = last.row + dr[state->target_dir];
        int c = last.col + dc[state->target_dir];
        if (is_valid_guess(guessBoard, r, c)) {
            *row = r;
            *col = c;
            return;
        } else {
            // Try reverse direction from first hit
            Coordinate base = state->hits[0];
            int rev_dir = (state->target_dir % 2 == 0) ? state->target_dir + 1 : state->target_dir - 1;
            r = base.row + dr[rev_dir];
            c = base.col + dc[rev_dir];
            if (is_valid_guess(guessBoard, r, c)) {
                *row = r;
                *col = c;
                return;
            } else {
                ai_reset_smart(state);
                ai_guess_smart(guessBoard, state, row, col);
                return;
            }
        }
    }

    // If only one hit, try probing all directions
    int directions[4][2] = { {-1,0}, {1,0}, {0,-1}, {0,1} };
    for (int d = 0; d < 4; d++) {
        int r = last.row + directions[d][0];
        int c = last.col + directions[d][1];
        if (is_valid_guess(guessBoard, r, c)) {
            *row = r;
            *col = c;
            return;
        }
    }

    // Fallback to hunting if blocked
    ai_reset_smart(state);
    ai_guess_smart(guessBoard, state, row, col);
}

void ai_update_smart(ai_state_t *state, int row, int col, char result) {
    if (result == 'H') {
        state->hits[state->hit_count].row = row;
        state->hits[state->hit_count].col = col;
        state->hit_count++;
        state->hunting = false;

        // Set direction if we have two hits
        if (state->hit_count == 2) {
            Coordinate a = state->hits[0];
            Coordinate b = state->hits[1];
            if (a.row == b.row) {
                state->target_dir = (a.col < b.col) ? DIR_RIGHT : DIR_LEFT;
            } else if (a.col == b.col) {
                state->target_dir = (a.row < b.row) ? DIR_DOWN : DIR_UP;
            }
        }
    } else if (result == 'S') {
        ai_reset_smart(state);
    }
    // 'M' does not update unless handled in direction logic
}


void generate_heatmap(uint8_t board[GRID_SIZE][GRID_SIZE], const int *remaining_ships, int ship_count, uint8_t heatmap[GRID_SIZE][GRID_SIZE]) {
    memset(heatmap, 0, sizeof(uint8_t) * GRID_SIZE * GRID_SIZE);

    for (int s = 0; s < ship_count; ++s) {
        int len = remaining_ships[s];

        // Horizontal
        for (int y = 0; y < GRID_SIZE; ++y) {
            for (int x = 0; x <= GRID_SIZE - len; ++x) {
                bool valid = true;
                int hit_bonus = 0;

                for (int i = 0; i < len; ++i) {
                    uint8_t cell = board[y][x + i];
                    if (cell == CELL_MISS || cell == CELL_SUNK) {
                        valid = false;
                        break;
                    }
                    if (cell == CELL_HIT) hit_bonus++;
                }

                if (valid) {
                    for (int i = 0; i < len; ++i)
                        heatmap[y][x + i] += 1 + hit_bonus;
                }
            }
        }

        // Vertical
        for (int x = 0; x < GRID_SIZE; ++x) {
            for (int y = 0; y <= GRID_SIZE - len; ++y) {
                bool valid = true;
                int hit_bonus = 0;

                for (int i = 0; i < len; ++i) {
                    uint8_t cell = board[y + i][x];
                    if (cell == CELL_MISS || cell == CELL_SUNK) {
                        valid = false;
                        break;
                    }
                    if (cell == CELL_HIT) hit_bonus++;
                }

                if (valid) {
                    for (int i = 0; i < len; ++i)
                        heatmap[y + i][x] += 1 + hit_bonus;
                }
            }
        }
    }
}

Coordinate select_best_move(uint8_t heatmap[GRID_SIZE][GRID_SIZE], uint8_t board[GRID_SIZE][GRID_SIZE]) {
    Coordinate best = {-1, -1};
    int max = -1;

    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            if ((board[y][x] == CELL_EMPTY || board[y][x] == CELL_SHIP) && heatmap[y][x] > max) {
                max = heatmap[y][x];
                best.row = y;
                best.col = x;
            }
        }
    }
    return best;
}
void print_heatmap(uint8_t heatmap[GRID_SIZE][GRID_SIZE]) {
    printf("Heatmap:\n");
    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            printf("%2d ", heatmap[y][x]);
        }
        printf("\n");
    }
}
