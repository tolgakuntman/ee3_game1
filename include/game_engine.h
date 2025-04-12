#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H
#include "game_logic.h"

// AI direction enums
enum { DIR_NONE = -1, DIR_UP = 0, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

typedef struct {
    Coordinate hits[5];
    int hit_count;
    int target_dir;       // -1 = none, 0-3 = directions
    bool hunting;
} ai_state_t;

void engine_init(void);
void engine_restart(void);
void engine_get_guess(difficulty_level_t difficulty, Board *ai_board, int *row, int *col);
void engine_update_smart_ai(int row, int col, char result);
Board* engine_get_ai_board(void);
void ai_guess_random(const Board *guessBoard, int *row, int *col);
void ai_reset_smart(ai_state_t *state);
void ai_guess_smart(const Board *guessBoard, ai_state_t *state, int *row, int *col);
void ai_update_smart(ai_state_t *state, int row, int col, char result) ;
void generate_heatmap(uint8_t board[GRID_SIZE][GRID_SIZE], const int *remaining_ships, int ship_count, uint8_t heatmap[GRID_SIZE][GRID_SIZE]) ;
Coordinate select_best_move(uint8_t heatmap[GRID_SIZE][GRID_SIZE], uint8_t board[GRID_SIZE][GRID_SIZE]);
void print_heatmap(uint8_t heatmap[GRID_SIZE][GRID_SIZE]);

#endif