#ifndef HB_MONITOR_H
#define HB_MONITOR_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#define MAX_SLAVES 3
#define HEARTBEAT_TIMEOUT pdMS_TO_TICKS(1000)  // 1 second before suspecting failure
#define PING_RETRY_TIMEOUT pdMS_TO_TICKS(500)  // 500ms wait for ping response

typedef enum {
    SLAVE_OFFLINE = 0,
    SLAVE_ONLINE = 1,
    SLAVE_PINGING = 2  // State while waiting for a ping response
} hb_status_t;

typedef struct {
    uint8_t slave_id;
    hb_status_t status;
    TimerHandle_t hb_timer;
    TimerHandle_t ping_timer;
} slave_heartbeat_t;

// Initializes heartbeat monitoring
void hb_init(void);

// Called when a heartbeat is received from a slave
void hb_register_heartbeat(uint8_t slave_id);

// Gets the current status of a slave
hb_status_t hb_get_status(uint8_t slave_id);

// Sends a ping when a heartbeat is missed
void hb_send_ping(uint8_t slave_id);

// Called when a ping response is received
void hb_register_ping_response(uint8_t slave_id);

// Logs an offline slave if no ping response is received
void hb_mark_offline(uint8_t slave_id);

#endif // HB_MONITOR_H
