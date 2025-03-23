#include "hb_monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nrf_io.h"  // For sending pings
#include <string.h>

#define TAG "HB_MONITOR"

static slave_heartbeat_t slave_heartbeats[MAX_SLAVES];

// Timer callback: Called when a heartbeat is missed
static void hb_timeout_callback(TimerHandle_t xTimer) {
    uint8_t slave_id = (uint8_t) pvTimerGetTimerID(xTimer);
    ESP_LOGW(TAG, "Heartbeat missed for Slave %d. Sending Ping...", slave_id);
}
void hb_init(void) {
    for (uint8_t i = 0; i < MAX_SLAVES; i++) {
        slave_heartbeats[i].slave_id = i + 1;
        slave_heartbeats[i].status = SLAVE_OFFLINE;
        slave_heartbeats[i].hb_timer = xTimerCreate("HB_Timer", HEARTBEAT_TIMEOUT, pdFALSE, (void *)(i + 1), hb_timeout_callback);
        // slave_heartbeats[i].ping_timer = xTimerCreate("Ping_Timer", PING_RETRY_TIMEOUT, pdFALSE, (void *)(i + 1), ping_timeout_callback);
        if (slave_heartbeats[i].hb_timer == NULL) {
            ESP_LOGE("HB_MONITOR", "Failed to create heartbeat timer for Slave %d", i + 1);
        } else {
            ESP_LOGI("HB_MONITOR", "Timer created for Slave %d", i + 1);
            xTimerStart(slave_heartbeats[i].hb_timer, 0);
        }
    }
    ESP_LOGE(pcTaskGetName(NULL), "slave hb added");
}

void hb_register_heartbeat(uint8_t slave_id) {
    if (slave_id < 1 || slave_id > MAX_SLAVES) return;

    slave_heartbeats[slave_id - 1].status = SLAVE_ONLINE;
    xTimerReset(slave_heartbeats[slave_id - 1].hb_timer, 0);
    ESP_LOGI(TAG, "Heartbeat received from Slave %d", slave_id);
}

void hb_send_ping(uint8_t slave_id) {
    if (slave_id < 1 || slave_id > MAX_SLAVES) return;

    // Prepare the ping message
    io_message_t ping_msg;
    memset(&ping_msg, 0, sizeof(io_message_t)); // Clear the structure
    ping_msg.slave_id = slave_id;
    strncpy(ping_msg.message_type, "PING", MESSAGE_TYPE_SIZE - 1); // Store "PING"
    
    // Send the ping request via the I/O queue
    io_enqueue_send(&ping_msg);
    ESP_LOGI(TAG, "Ping sent to Slave %d. Marking as PINGED.", slave_id);

    slave_heartbeats[slave_id - 1].status = SLAVE_PINGING;
    xTimerStart(slave_heartbeats[slave_id - 1].ping_timer, 0);
    ESP_LOGI(TAG, "Ping sent to Slave %d. Marking as PINGED.", slave_id);

}

void hb_register_ping_response(uint8_t slave_id) {
    if (slave_id < 1 || slave_id > MAX_SLAVES) return;

    slave_heartbeats[slave_id - 1].status = SLAVE_ONLINE;
    xTimerStop(slave_heartbeats[slave_id - 1].hb_timer, 0);
    xTimerStart(slave_heartbeats[slave_id - 1].hb_timer, 0);
    ESP_LOGI(TAG, "Ping response received from Slave %d. Marking as ONLINE.", slave_id);
}

void hb_mark_offline(uint8_t slave_id) {
    if (slave_id < 1 || slave_id > MAX_SLAVES) return;

    slave_heartbeats[slave_id - 1].status = SLAVE_OFFLINE;
    ESP_LOGE(TAG, "Slave %d is OFFLINE.", slave_id);
}

hb_status_t hb_get_status(uint8_t slave_id) {
    if (slave_id < 1 || slave_id > MAX_SLAVES) return SLAVE_OFFLINE;
    return slave_heartbeats[slave_id - 1].status;
}
