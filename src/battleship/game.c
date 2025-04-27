#include "game.h"
#include "game_logic.h"
#include "game_engine.h"
#include <stdlib.h>
#include "led_matrix_ctrl.h"
#include "freertos/FreeRTOS.h"
#include "io_builder.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nrf_io.h"
#include "http_server.h"

static Game *current_game;

static void Game_Init(Game *game) {
    memset(game, 0, sizeof(Game));

    game->difficulty = DIFFICULTY_HARD;
    current_game=game;
    game->turn= 0;            
    init_board(&game->player.board, false);
    init_board(&game->opponent.board, true);
    engine_init();
    game->player.boat_count = MAX_SHIPS;
    game->opponent.boat_count = MAX_SHIPS;
    ESP_LOGI(GAME_TAG, "Game initialized with difficulty level: %d", game->difficulty);
    led_matrix_init();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

Game *get_game_instance(void) {
    return current_game;
}
void setup_game_randomly(Game *game) {
    place_random_ships(&game->player, ship_lengths, MAX_SHIPS);
    place_random_ships(&game->opponent, ship_lengths, MAX_SHIPS);
    render_board_to_matrix(&game->player.board, PLAYER_MATRIX, true);
    print_board(&game->player.board, true);
    print_board(&game->opponent.board, true);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void render_board_to_matrix(const Board *board, uint8_t matrix_num, bool reveal_ships) {
    for (uint8_t row = 0; row < GRID_SIZE; row++) {
        for (uint8_t col = 0; col < GRID_SIZE; col++) {
            uint8_t cell = board->cells[row][col];
            Color color;

            switch (cell) {
                case CELL_SHIP:
                    if (reveal_ships) {
                        color = COLOR_CYAN;  // Ship preview
                    } else {
                        color = COLOR_BLUE; // Hide enemy ships
                    }
                    break;
                case CELL_SUNK:
                case CELL_HIT:
                    color = COLOR_RED;
                    break;
                case CELL_MISS:
                    color = COLOR_GREEN;
                    break;
                case CELL_EMPTY:
                default:
                    color = COLOR_BLUE;
                    break;
            }

            setLEDColor(matrix_num, row, col, color);
        }
    }
}

void game_task(void *pvParameters) {
    Game *game = (Game *)pvParameters;
    if (game == NULL) {
        ESP_LOGE(GAME_TAG, "Game structure is NULL");
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        switch (game->state) {
            case GAME_STATE_INIT:
                Game_Init(game);    
                game->state = GAME_STATE_WEB_INIT;
                break;

            case GAME_STATE_WEB_INIT:
                if(current_game->game_ready){
                    game->state = GAME_STATE_WAITING_FOR_PLAYER;
                    render_board_to_matrix(&game->player.board, PLAYER_MATRIX, true);
                    place_random_ships(&game->opponent, ship_lengths, MAX_SHIPS);
                    print_board(&game->player.board, true);
                    print_board(&game->opponent.board, true);
                    // send_ws_game_update(game, false);
                }
                break;

            case GAME_STATE_RANDOM_INIT:
                setup_game_randomly(game);
                game->state = GAME_STATE_WAITING_FOR_PLAYER;
                // send_ws_game_update(game, false);
                break;

                case GAME_STATE_WAITING_FOR_PLAYER: {
                    io_message_t msg;
                    if (xQueueReceive(io_receive_queue, &msg, pdMS_TO_TICKS(100))) {
                        if (strcmp(msg.message_type, "BUTTON") == 0) {
                            Coordinate coord = {
                                .row = msg.payload[1],
                                .col = msg.payload[0]
                            };
                            coord.col = GRID_SIZE - 1 - coord.col; // Invert row for display
                            char result = apply_guess(&game->opponent, coord.row, coord.col);
                            if(!result){
                                ESP_LOGI(GAME_TAG, "Already guessed at (%d, %d)", coord.row, coord.col);
                                break;
                            }
                            if(result == 'H' || result == 'S') {
                                ESP_LOGI(GAME_TAG, "Hit at (%d, %d)", coord.row, coord.col);
                                send_sound_command(4, 0);
                            } else if(result == 'M') {
                                send_sound_command(4, 1);
                                ESP_LOGI(GAME_TAG,"Miss at (%d, %d)", coord.row, coord.col);
                            }
                            render_board_to_matrix(&game->player.board, PLAYER_MATRIX, true);
                            render_board_to_matrix(&game->opponent.board, OPPONENT_MATRIX, false);
                            print_board(&game->opponent.board, true);
                            print_board(&game->player.board, true);
                            game->turn = 1;
                            if (all_ships_sunk(&game->opponent)) {
                                game->state = GAME_STATE_GAME_OVER;
                                ESP_LOGI(GAME_TAG, "All ships sunk! Game Over.");
                                send_ws_result("player");
                            } else {
                                game->state = GAME_STATE_WAITING_FOR_OPPONENT;
                                send_ws_game_update(game, false);
                            }
                        }
                    }
                    break;
                }
            case GAME_STATE_WAITING_FOR_OPPONENT:
                vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for opponent's move
                Coordinate coord={0,0};
                Board *ai_board = engine_get_ai_board();
                engine_get_guess(game->difficulty, ai_board, &coord.row, &coord.col);
                char result = apply_guess(&game->player, coord.row, coord.col);
                engine_update_smart_ai(coord.row, coord.col, result);
                if(!result){
                    ESP_LOGI(GAME_TAG, "Already guessed at (%d, %d)", coord.row, coord.col);
                    break;
                }
                if(result == 'H' || result == 'S') {
                    ESP_LOGI(GAME_TAG, "Hit at (%d, %d)", coord.row, coord.col);
                } else if(result == 'M') {

                    ESP_LOGI(GAME_TAG,"Miss at (%d, %d)", coord.row, coord.col);
                } 
                render_board_to_matrix(&game->player.board, PLAYER_MATRIX, true);
                render_board_to_matrix(&game->opponent.board, OPPONENT_MATRIX, false);
                print_board(&game->opponent.board, true);
                print_board(&game->player.board, true);
                game->turn = 0;
                if (all_ships_sunk(&game->player)) {
                    game->state = GAME_STATE_GAME_OVER;
                    ESP_LOGI(GAME_TAG, "All ships sunk! Game Over.");
                    send_led_matrix_update(3);
                    send_ws_result("opponent");
                } else {
                    game->state = GAME_STATE_WAITING_FOR_PLAYER;
                    send_ws_game_update(game, false);
                }
                break;

            case GAME_STATE_RUNNING:
                // Add logic for GAME_STATE_RUNNING here
                break;

            case GAME_STATE_GAME_OVER:
                // send_game_over_update(); // to GUI, LEDs, etc.
                vTaskDelay(pdMS_TO_TICKS(500)); // brief pause
                game->state = GAME_STATE_GAME_RESTART;
                break;

            case GAME_STATE_GAME_RESTART:
                engine_restart();
                ESP_LOGI(GAME_TAG, "restarting the game");
                for(int i =0; i < MAX_SHIPS; i++){
                    ESP_LOGI(GAME_TAG, "Boat %d hit count: %d", i, game->player.boats[i].hit_count);
                    if(game->player.boats[i].hit_count < game->player.boats[i].length){
                        if(game->player.boats[i].horizontal){
                            send_robot_command(4,4 - game->player.boats[i].length,game->player.boats[i].coords[0].row + game->player.boats[i].length - 1
                                ,game->player.boats[i].coords[0].col,game->player.boats[i].horizontal ? 0:1,0);
                        }else{  
                            send_robot_command(4,4 - game->player.boats[i].length,game->player.boats[i].coords[0].row,game->player.boats[i].coords[0].col,game->player.boats[i].horizontal ? 0:1,0);
                        }
                        vTaskDelay(pdMS_TO_TICKS(5000));
                    }
                }
                game->state = GAME_STATE_INIT;
                break;
        }
        send_led_matrix_update(3);
        vTaskDelay(pdMS_TO_TICKS(250)); // reduce CPU load
    }
}
