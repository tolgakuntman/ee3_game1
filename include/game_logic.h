#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stdint.h>
#include <stdbool.h>
#include "game.h"




// Game setup
void init_board(Board *b, bool isOpp);
void print_board(const Board *b, bool reveal);
void place_ship(Board *b, Boat *boat, int row, int col, int length, bool horizontal);
bool can_place_ship(const Board *b, int row, int col, int length, bool horizontal);

// Game mechanics
char apply_guess(Player *target, int row, int col); // returns result: 'H', 'M', 'S'
bool all_ships_sunk(const Player *p);

// Random placement (for testing)
void place_random_ships(Player *p, const int lengths[], int count);

#endif
