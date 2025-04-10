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

static const char *TAG_WIFI = "WiFiStation";
static const char *TAG_HTTP = "HTTP_Server";

static game_setup_t current_setup;

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
            .ssid = "FiberHGW_ZTGJ5N_2.4GHz",
            .password = "NJHbuxFTb4",
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

    current_setup.difficulty = (difficulty_level_t)level->valueint;
    ESP_LOGI(TAG_HTTP, "Difficulty set to %d", current_setup.difficulty);

    cJSON_Delete(json);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}
static void print_ship_grid(void) {
    ESP_LOGI(TAG_HTTP, "Current Ship Grid:");
    for (int i = 0; i < GRID_SIZE; i++) {
        char row[GRID_SIZE * 4] = {0}; 
        for (int j = 0; j < GRID_SIZE; j++) {
            char cell[GRID_SIZE+1]; //+1 to avoid compiler error
            snprintf(cell, sizeof(cell), "%d ", current_setup.ship_grid[i][j]);
            strcat(row, cell);
        }
        ESP_LOGI(TAG_HTTP, "%s", row); 
    }
}
static esp_err_t post_place_ships_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return ESP_FAIL;

    cJSON *grid = cJSON_GetObjectItem(json, "ship_grid");
    if (!grid || !cJSON_IsArray(grid)) {
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    for (int i = 0; i < GRID_SIZE; i++) {
        cJSON *row = cJSON_GetArrayItem(grid, i);
        if (!row || !cJSON_IsArray(row)) continue;
        for (int j = 0; j < GRID_SIZE; j++) {
            cJSON *cell = cJSON_GetArrayItem(row, j);
            current_setup.ship_grid[i][j] = (cell && cJSON_IsNumber(cell)) ? (uint8_t)cell->valueint : 0;
        }
    }

    ESP_LOGI(TAG_HTTP, "Ship grid set");
    cJSON_Delete(json);
    httpd_resp_sendstr(req, "OK");
    print_ship_grid();
    return ESP_OK;
}



static esp_err_t websocket_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        struct sockaddr_in6 addr;
        socklen_t len = sizeof(addr);
        getpeername(httpd_req_to_sockfd(req), (struct sockaddr *)&addr, &len);
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin6_addr, ip, sizeof(ip));
        ESP_LOGI(TAG_HTTP, "WebSocket client connected: %s", ip);
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

        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
            strcmp((char *)buf, "Trigger async") == 0) {
            free(buf);
            return trigger_async_send(req->handle, req);
        }

        httpd_ws_send_frame(req, &ws_pkt);
    }

    free(buf);
    return ret;
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
