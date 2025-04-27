#include "http_server.h"
#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "game.h"
#include "game_logic.h"
#include "io_builder.h"
static const char *TAG_WIFI = "WiFiStation";
static const char *TAG_HTTP = "HTTP_Server";

static httpd_handle_t active_ws_handle = NULL;
static int active_ws_fd = -1;


static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG_WIFI, "STA start -> connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG_WIFI, "STA disconnected -> reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_WIFI, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Tolga",
            .password = "1234567890",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG_WIFI, "Wi-Fi station started. Connecting to SSID=%s...", wifi_config.sta.ssid);
}


struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

static void ws_async_send(void *arg) {
    static const char *data = "Async data";
    struct async_resp_arg *resp_arg = arg;

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)data,
        .len = strlen(data)
    };

    httpd_ws_send_frame_async(resp_arg->hd, resp_arg->fd, &ws_pkt);
    free(resp_arg);
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req) {
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    if (!resp_arg) return ESP_ERR_NO_MEM;

    resp_arg->hd = handle;
    resp_arg->fd = httpd_req_to_sockfd(req);

    esp_err_t ret = httpd_queue_work(handle, ws_async_send, resp_arg);
    if (ret != ESP_OK) free(resp_arg);
    return ret;
}


static esp_err_t post_set_difficulty_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return ESP_FAIL;

    cJSON *level = cJSON_GetObjectItem(json, "difficulty");
    if (!level || !cJSON_IsNumber(level)) {
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    Game *game = get_game_instance();
    game->difficulty = (difficulty_level_t)level->valueint;
    ESP_LOGI(TAG_HTTP, "Difficulty set to %d", game->difficulty);

    cJSON_Delete(json);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t post_place_ships_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return ESP_FAIL;

    cJSON *ships = cJSON_GetObjectItem(json, "ships");
    if (!ships || !cJSON_IsArray(ships)) {
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    Game *game = get_game_instance();
    Player *player = &game->player;
    player->boat_count = 0;

    for (int i = 0; i < cJSON_GetArraySize(ships); ++i) {
        cJSON *ship = cJSON_GetArrayItem(ships, i);
        if (!ship) continue;
        int len = cJSON_GetObjectItem(ship, "length")->valueint;
        int row = cJSON_GetObjectItem(ship, "row")->valueint + (GRID_SIZE-1);
        int col = cJSON_GetObjectItem(ship, "col")->valueint;
        bool horizontal = cJSON_GetObjectItem(ship, "horizontal")->valueint;
        ESP_LOGI(TAG_HTTP, "Placing ship at (%d, %d) length %d %s", row, col, len, horizontal ? "horizontal" : "vertical");
        if (player->boat_count >= MAX_SHIPS) break;
        if (!can_place_ship(&player->board, row, col, len, horizontal)){
            ESP_LOGE(TAG_HTTP, "Invalid ship placement at (%d, %d)", row, col);
            cJSON_Delete(json);
            return ESP_FAIL;
        }
        Boat *b = &player->boats[player->boat_count];
        place_ship(&player->board, b, row, col, len, horizontal);
        send_robot_command(4,player->boat_count,row,col+ b->length -1,horizontal ? 0:1,0);
        player->boat_count++;
        vTaskDelay(10000/portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG_HTTP, "Ships placed via API.");
    // print_ship_grid();  // Show updated grid
    cJSON_Delete(json);
    httpd_resp_sendstr(req, "OK");
    game->game_ready = true;
    return ESP_OK;
}

static esp_err_t post_setup_game_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char buf[512];
    buf[511] = '\0';
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    ESP_LOGI(TAG_HTTP, "Received setup game request: %s", buf);
    if (ret <= 0) return ESP_FAIL;

    cJSON *json = cJSON_Parse(buf);
    if (!json) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");

    cJSON *level = cJSON_GetObjectItem(json, "difficulty");
    cJSON *ships = cJSON_GetObjectItem(json, "ships");
    cJSON *random = cJSON_GetObjectItem(json, "random");

    Game *game = get_game_instance();
    game->difficulty = (difficulty_level_t)atoi(level->valuestring);
    // ESP_LOGI(TAG_HTTP, "Difficulty set to %d", game->difficulty);
    bool is_random = cJSON_IsTrue(random);
    if(is_random) {
        game->state = GAME_STATE_RANDOM_INIT;
        ESP_LOGI(TAG_HTTP, "Random game setup requested");
        ESP_LOGI(TAG_HTTP, "Setup game completed: difficulty=%d and randomized", game->difficulty);
        cJSON_Delete(json);
        return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    }
    Player *player = &game->player;
    // reset_board(&player->board);  // enable if needed
    player->boat_count = 0;

    for (int i = 0; i < cJSON_GetArraySize(ships); ++i) {
        cJSON *ship = cJSON_GetArrayItem(ships, i);
        if (!ship) continue;

        int row = (GRID_SIZE - 1) - cJSON_GetObjectItem(ship, "row")->valueint;
        int len = cJSON_GetObjectItem(ship, "length")->valueint;
        int col = cJSON_GetObjectItem(ship, "col")->valueint;
        bool horizontal = cJSON_GetObjectItem(ship, "horizontal")->valueint;
        if(!horizontal){
            row -= (len - 1);
        }
        ESP_LOGI(TAG_HTTP, "Placing ship at (%d, %d) length %d %s", row, col, len, horizontal ? "horizontal" : "vertical");

        if (player->boat_count >= MAX_SHIPS) break;
        if (!can_place_ship(&player->board, row, col, len, horizontal)) {
            ESP_LOGE(TAG_HTTP, "Invalid ship placement at (%d, %d)", row, col);
            cJSON_Delete(json);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid placement");
        }
        Boat *b = &player->boats[player->boat_count];
        place_ship(&player->board, b, row, col, len, horizontal);
        if(horizontal){
            send_robot_command(4,player->boat_count,row,col+(b->length-1),horizontal ? 0:1,0);
        }else{
            send_robot_command(4,player->boat_count,row,col,horizontal ? 0:1,0);
        }
        // send_robot_command(4,player->boat_count,row,col+(b->length-1),horizontal ? 0:1,0);
        vTaskDelay(250/portTICK_PERIOD_MS);
        render_board_to_matrix(&player->board,2,true);
        send_led_matrix_update(3);
        vTaskDelay(5000/portTICK_PERIOD_MS);
        player->boat_count++;
    }

    ESP_LOGI(TAG_HTTP, "Setup game completed: difficulty=%d, ships=%d", game->difficulty, player->boat_count);
    cJSON_Delete(json);
    game->game_ready = true;
    return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t cors_options_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t websocket_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        active_ws_handle = req->handle;
        active_ws_fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG_HTTP, "WebSocket client connected.");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = { .type = HTTPD_WS_TYPE_TEXT };
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    uint8_t *buf = calloc(1, ws_pkt.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    ws_pkt.payload = buf;

    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG_HTTP, "WebSocket message: %s", buf);

        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
            if (strcmp((char *)buf, "init") == 0) {
                Game *game = get_game_instance();
                send_ws_game_update(game, true); // ✅ Only send when browser says it's ready
            }else if (strcmp((char *)buf, "restart") == 0) {
                Game *game = get_game_instance();
                game->state = GAME_STATE_GAME_RESTART;
                ESP_LOGI(TAG_HTTP, "Game restart requested");
                return ESP_OK;
            } else {
                ESP_LOGI(TAG_HTTP, "Unknown WebSocket message: %s", buf);
            }
        }
    }


    free(buf);
    return ret;
}

static cJSON *board_to_json(const Board *b, bool revealShips) {
    cJSON *boardArr = cJSON_CreateArray();
    for (int r = GRID_SIZE - 1; r >= 0; r--) {
        cJSON *rowArr = cJSON_CreateArray();
        for (int c = 0; c <GRID_SIZE; c++) {
            char cellChar = 'E';
            switch (b->cells[r][c]) {
                case CELL_SHIP: cellChar = revealShips ? 'S' : 'E'; break;
                case CELL_EMPTY: cellChar = 'E'; break;
                case CELL_HIT:  cellChar = 'H'; break;
                case CELL_MISS: cellChar = 'M'; break;
                case CELL_SUNK: cellChar = 'X'; break;
                default: break;
            }
            char cellStr[2] = {cellChar, '\0'};
            cJSON_AddItemToArray(rowArr, cJSON_CreateString(cellStr));
        }
        cJSON_AddItemToArray(boardArr, rowArr);
    }
    return boardArr;
}

void send_ws_game_update(Game *game, bool isInit) {
    if (!active_ws_handle || active_ws_fd < 0) {
        ESP_LOGE(TAG_HTTP, "WebSocket not connected");
        return;
    }

    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", isInit ? "init" : "update");

    cJSON *player = board_to_json(&game->player.board, true);
    cJSON *opponent = board_to_json(&game->opponent.board, false);

    cJSON_AddItemToObject(msg, "player", player);
    cJSON_AddItemToObject(msg, "opponent", opponent);
    cJSON_AddStringToObject(msg, "turn", game->turn == 0 ? "player" : "opponent");

    char *str = cJSON_PrintUnformatted(msg);
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)str,
        .len = strlen(str)
    };
    esp_err_t ret = httpd_ws_send_frame_async(active_ws_handle, active_ws_fd, &frame);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Failed to send WebSocket message: %s", esp_err_to_name(ret));
    }
    free(str);
    ESP_LOGI(TAG_HTTP, "WebSocket update sent");
    cJSON_Delete(msg);
}

void send_ws_result(const char *winner) {
    if (!active_ws_handle || active_ws_fd < 0) return;

    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "result");
    cJSON_AddStringToObject(msg, "winner", winner); // "player" or "opponent"

    char *str = cJSON_PrintUnformatted(msg);

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)str,
        .len = strlen(str)
    };
    httpd_ws_send_frame_async(active_ws_handle, active_ws_fd, &frame);

    free(str);
    cJSON_Delete(msg);
}


static esp_err_t register_endpoints(httpd_handle_t server) {
    httpd_uri_t uri_diff = {
        .uri = URI_SET_DIFFICULTY,
        .method = HTTP_POST,
        .handler = post_set_difficulty_handler
    };
    httpd_register_uri_handler(server, &uri_diff);

    httpd_uri_t uri_ships = {
        .uri = URI_PLACE_SHIPS,
        .method = HTTP_POST,
        .handler = post_place_ships_handler
    };
    httpd_register_uri_handler(server, &uri_ships);

    httpd_uri_t uri_game_setup = {
        .uri = URI_GAME_SETUP,
        .method = HTTP_POST,
        .handler = post_setup_game_handler
    };
    httpd_register_uri_handler(server, &uri_game_setup);
    httpd_uri_t uri_setup_options = {
        .uri = URI_GAME_SETUP,
        .method = HTTP_OPTIONS,
        .handler = cors_options_handler
    };
    httpd_register_uri_handler(server, &uri_setup_options);
    httpd_uri_t uri_ws = {
        .uri = URI_WS_GAME_EVENTS,
        .method = HTTP_GET,
        .handler = websocket_handler,
        .is_websocket = true
    };
    return httpd_register_uri_handler(server, &uri_ws);
}


httpd_handle_t start_webserver(void) {
    wifi_init_sta();
    ESP_LOGI(TAG_HTTP, "Starting HTTP server...");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        register_endpoints(server);
        ESP_LOGI(TAG_HTTP, "HTTP & WebSocket server started.");
    } else {
        ESP_LOGE(TAG_HTTP, "Failed to start server");
    }

    return server;
}

void stop_webserver(httpd_handle_t server) {
    if (server) httpd_stop(server);
}
