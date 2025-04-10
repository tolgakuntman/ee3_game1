#include "game_logic.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

void init_board(Board *b) {
    memset(b->cells, CELL_EMPTY, sizeof(b->cells));
}

void print_board(const Board *b, bool reveal) {
    printf("   ");
    for (int c = 0; c < GRID_SIZE; c++) {
        printf(" %d", c);
    }
    printf("\n");
    for (int r = 0; r < GRID_SIZE; r++) {
        printf("%2d ", r);
        for (int c = 0; c < GRID_SIZE; c++) {
            char ch;
            switch (b->cells[r][c]) {
                case CELL_EMPTY: ch = '.'; break;
                case CELL_SHIP:  ch = reveal ? 'B' : '.'; break;
                case CELL_HIT:   ch = 'H'; break;
                case CELL_MISS:  ch = 'M'; break;
                case CELL_SUNK:  ch = 'X'; break;
                default:         ch = '?'; break;
            }
            printf(" %c", ch);
        }
        printf("\n");
    }
}

bool can_place_ship(const Board *b, int row, int col, int length, bool horizontal) {
    if (horizontal) {
        if (col + length > GRID_SIZE) return false;
        for (int i = 0; i < length; i++) {
            if (b->cells[row][col + i] != CELL_EMPTY) return false;
        }
    } else {
        if (row + length > GRID_SIZE) return false;
        for (int i = 0; i < length; i++) {
            if (b->cells[row + i][col] != CELL_EMPTY) return false;
        }
    }
    return true;
}

void place_ship(Board *b, Boat *boat, int row, int col, int length, bool horizontal) {
    boat->length = length;
    boat->hit_count = 0;
    for (int i = 0; i < length; i++) {
        int r = row + (horizontal ? 0 : i);
        int c = col + (horizontal ? i : 0);
        b->cells[r][c] = CELL_SHIP;
        boat->coords[i].row = r;
        boat->coords[i].col = c;
    }
}

char apply_guess(Player *target, int row, int col) {
    uint8_t *cell = &target->board.cells[row][col];
    if (*cell == CELL_SHIP) {
        *cell = CELL_HIT;
        for (int b = 0; b < target->boat_count; b++) {
            Boat *boat = &target->boats[b];
            for (int i = 0; i < boat->length; i++) {
                if (boat->coords[i].row == row && boat->coords[i].col == col) {
                    boat->hit_count++;
                    if (boat->hit_count == boat->length) {
                        for (int j = 0; j < boat->length; j++) {
                            int r = boat->coords[j].row;
                            int c = boat->coords[j].col;
                            target->board.cells[r][c] = CELL_SUNK;
                        }
                        return 'S';
                    }
                    return 'H';
                }
            }
        }
    } else if (*cell == CELL_EMPTY) {
        *cell = CELL_MISS;
        return 'M';
    }
    return ' '; // already guessed
}

bool all_ships_sunk(const Player *p) {
    for (int b = 0; b < p->boat_count; b++) {
        if (p->boats[b].hit_count < p->boats[b].length) return false;
    }
    return true;
}

void place_random_ships(Player *p, const int lengths[], int count) {
    srand(time(NULL));
    p->boat_count = 0;
    for (int i = 0; i < count; i++) {
        int attempts = 0;
        while (attempts++ < 100) {
            int row = rand() % GRID_SIZE;
            int col = rand() % GRID_SIZE;
            bool horizontal = rand() % 2;
            if (can_place_ship(&p->board, row, col, lengths[i], horizontal)) {
                place_ship(&p->board, &p->boats[p->boat_count], row, col, lengths[i], horizontal);
                p->boat_count++;
                break;
            }
        }
    }
}
void reset_board(Board *b) {
    memset(b->cells, CELL_EMPTY, sizeof(b->cells));
}