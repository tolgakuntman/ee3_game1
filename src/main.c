#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <time.h>
#include "mirf.h"
#include "nrf_io.h"
#include "hb_monitor.h"
#include "io_builder.h"
#include "led_matrix_ctrl.h"
#include "http_server.h"
#include "game_logic.h"
#include "game_engine.h"
#include "game.h"
static const char *TAG = "http_server_thread";

// Task function to run the HTTP server
void http_server_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting HTTP server task...");

    // Start the HTTP server
    httpd_handle_t server = start_webserver();
    if (server) {
        ESP_LOGI(TAG, "HTTP server started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        vTaskDelete(NULL); // Delete the task if the server fails to start
        return;
    }

    // Keep the task running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay to keep the task alive
    }

    // Stop the server when the task is deleted
    stop_webserver(server);
    vTaskDelete(NULL);
}

void app_main() {
    io_thread_init();
    hb_init();
    led_matrix_init();
    Game *game=malloc(sizeof(Game));
    game->state = GAME_STATE_INIT;
    if (game == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for game structure");
        return;
    }
    srand(time(NULL)); // Seed the random number generator
    xTaskCreate(http_server_task, "http_server_task", 4096, NULL, 5, NULL);
    xTaskCreate(game_task, "game_task", 4096*2, game, 5, NULL);
    while(1){
        
        // send_robot_command(3,1,0,0,0,0);
        vTaskDelay(10 / portTICK_PERIOD_MS);
        // send_robot_command(2,1,0,0,0,1);
        // setLEDColor(1, 0, 0, COLOR_RED);
        // vTaskDelay(50 / portTICK_PERIOD_MS);
        // setLEDColor(2, 0, 0, COLOR_RED);
        // vTaskDelay(1000 / portTICK_PERIOD_MS);

        // // setLEDColor(1, 0, 0, COLOR_GREEN);
        // // vTaskDelay(50 / portTICK_PERIOD_MS);
        // setLEDColor(2, 0, 0, COLOR_GREEN);
        // vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}