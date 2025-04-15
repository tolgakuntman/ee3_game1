#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_http_server.h"
#include "game.h"
// Setup endpoint URIs
#define URI_SET_DIFFICULTY "/api/set_difficulty"
#define URI_PLACE_SHIPS    "/api/place_ships"
#define URI_GAME_SETUP    "/api/setup_game"
#define URI_RESTART      "/api/restart"
#define URI_WS_GAME_EVENTS "/ws"


void wifi_init_sta(void);
// Initialize HTTP + WebSocket server
httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);
void send_ws_game_update(Game *game, bool isInit);
void send_ws_result(const char *winner);
// Send JSON string to all connected WebSocket clients
void broadcast_ws_event(const char *json_str);

#endif // HTTP_SERVER_H
