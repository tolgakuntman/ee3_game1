#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_http_server.h"
#include "game.h"
// Setup endpoint URIs
#define URI_SET_DIFFICULTY "/api/set_difficulty"
#define URI_PLACE_SHIPS    "/api/place_ships"
#define URI_RESTART      "/api/restart"
#define URI_WS_GAME_EVENTS "/ws"


// Ship grid size
#define GRID_SIZE 5

// Game setup data structure
typedef struct {
    difficulty_level_t difficulty;
    uint8_t ship_grid[GRID_SIZE][GRID_SIZE];
} game_setup_t;
void wifi_init_sta(void);
// Initialize HTTP + WebSocket server
httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);

// Retrieve latest game setup
const game_setup_t *get_game_setup(void);

// Send JSON string to all connected WebSocket clients
void broadcast_ws_event(const char *json_str);

#endif // HTTP_SERVER_H
