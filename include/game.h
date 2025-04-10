#ifndef GAME_H
#define GAME_H

#define GRID_SIZE 5
#define MAX_SHIPS 3
#include <stdint.h>
// Difficulty levels
typedef enum {
    DIFFICULTY_RANDOM = 0,
    DIFFICULTY_SMART,
    DIFFICULTY_HARD
} difficulty_level_t;


// Cell states
typedef enum {
    CELL_EMPTY = 0,
    CELL_SHIP,
    CELL_HIT,
    CELL_MISS,
    CELL_SUNK
} cell_state_t;

typedef enum {
    GAME_STATE_INIT = 0,
    GAME_STATE_WEB_INIT,
    GAME_STATE_RANDOM_INIT,
    GAME_STATE_GAME_OVER,
    GAME_STATE_GAME_RESTART
} game_state_t;

// Coordinate helper
typedef struct {
    int row;
    int col;
} Coordinate;

// Boat structure
typedef struct {
    int length;
    Coordinate coords[5];
    int hit_count;
} Boat;

// Board representation
typedef struct {
    uint8_t cells[GRID_SIZE][GRID_SIZE];
} Board;

// Player board + boat state
typedef struct {
    Board board;
    Boat boats[MAX_SHIPS];
    int boat_count;
} Player;

typedef struct {
    Player player;
    Player opponent;
    difficulty_level_t difficulty;
    game_state_t state;
}Game;


#endif