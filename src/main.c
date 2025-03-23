#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "mirf.h"
#include "nrf_io.h"
#include "hb_monitor.h"


void app_main() {
    io_thread_init();
    hb_init();
    while(1){
        vTaskDelay(10);
    }
}