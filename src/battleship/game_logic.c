#include "game_logic.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "esp_log.h"
#include <stdio.h>
#include <esp_random.h>
#include "io_builder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_server.h"

static const char *TAG = "game_logic";

void init_board(Board *b, bool isOpp) {
    memset(b->cells, CELL_EMPTY, sizeof(b->cells));
    b->isOpp=isOpp;
}

void print_board(const Board *b, bool reveal) {
    char buffer[64];  // Enough for 5x5 grid and formatting
    ESP_LOGI(TAG, "   0 1 2 3 4"); // Fixed header for GRID_SIZE = 5

    for (int r = 0; r < GRID_SIZE; r++) {
        int offset = snprintf(buffer, sizeof(buffer), "%2d", r);
        for (int c = 0; c < GRID_SIZE && offset < sizeof(buffer) - 2; c++) {
            char ch;
            switch (b->cells[r][c]) {
                case CELL_EMPTY: ch = '.'; break;
                case CELL_SHIP:  ch = reveal ? 'B' : '.'; break;
                case CELL_HIT:   ch = 'H'; break;
                case CELL_MISS:  ch = 'M'; break;
                case CELL_SUNK:  ch = 'X'; break;
                default:         ch = '?'; break;
            }
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, " %c", ch);
        }
        ESP_LOGI(TAG, "%s", buffer);
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
        boat->horizontal = horizontal;
    }
    ESP_LOGI(TAG, "Placed ship at (%d, %d) with length %d %s", row, col, length, horizontal ? "horizontally" : "vertically");
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
                        if(!target->board.isOpp){
                            send_sound_command(4, 0);
                            vTaskDelay(pdMS_TO_TICKS(100));
                            if(boat->horizontal){
                                send_robot_command(4,4 - boat->length,row,col + boat->length - 1,boat->horizontal ? 0:1,0);
                            }else{
                                send_robot_command(4,4 - boat->length,row,col,boat->horizontal ? 0:1,0);
                            }
                            vTaskDelay(pdMS_TO_TICKS(5000));
                        }
                        ESP_LOGI(TAG, "Ship sunk at (%d, %d)", row, col);
                        return 'S';
                    }
                    ESP_LOGI(TAG, "Hit at (%d, %d)", row, col);
                    return 'H';
                }
            }
        }
    } else if (*cell == CELL_EMPTY) {
        *cell = CELL_MISS;
        ESP_LOGI(TAG, "Miss at (%d, %d)", row, col);
        return 'M';
    }
    ESP_LOGI(TAG, "Already guessed at (%d, %d)", row, col);
    return 0; // already guessed
}

bool all_ships_sunk(const Player *p) {
    for (int b = 0; b < p->boat_count; b++) {
        if (p->boats[b].hit_count < p->boats[b].length) return false;
    }
    ESP_LOGI(TAG, "All ships sunk!");
    return true;
}

void place_random_ships(Player *p, const int lengths[], int count) {
    srand(time(NULL));
    p->boat_count = 0;
    for (int i = 0; i < count; i++) {
        int attempts = 0;
        while (attempts++ < 100) {
            int row = esp_random() % GRID_SIZE;
            int col = esp_random() % GRID_SIZE;
            bool horizontal = esp_random() % 2;
            if (can_place_ship(&p->board, row, col, lengths[i], horizontal)) {
                place_ship(&p->board, &p->boats[p->boat_count], row, col, lengths[i], horizontal);

                if(!p->board.isOpp){
                    if(horizontal){
                        send_robot_command(4,lengths[i],row,col+(lengths[i]-1),horizontal ? 0:1,0);
                    }else{
                        send_robot_command(4,lengths[i],row,col,horizontal ? 0:1,0);
                    }
                    send_ws_game_update(get_game_instance(), false);
                    vTaskDelay(pdMS_TO_TICKS(5000));
                }
                p->boat_count++;
                break;
            }
        }
    }
    ESP_LOGI(TAG, "Random ships placed");
}

void reset_board(Board *b) {
    memset(b->cells, CELL_EMPTY, sizeof(b->cells));
    ESP_LOGI(TAG, "Board reset");
}