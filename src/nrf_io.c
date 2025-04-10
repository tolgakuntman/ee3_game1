#include "nrf_io.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include "hb_monitor.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include "mirf.h"

#define MAX_RETRIES 20
#define CONFIG_RADIO_CHANNEL 112
#define CONFIG_RETRANSMIT_DELAY 100
#define NRF_IRQ_GPIO GPIO_NUM_8
#define GPIO_INPUT_PIN_SEL (1ULL << NRF_IRQ_GPIO)
#define ESP_INTR_FLAG_DEFAULT 0

static esp_timer_handle_t retry_timer;
static io_message_t retry_msg;
static int retry_count = 0;
static NRF24_t nrf_device;
static const char *slave_addresses[4] = {"2RECV", "3RECV", "4RECV", "5RECV"};
static QueueHandle_t nrf_irq_queue = NULL;

QueueHandle_t io_send_queue;
QueueHandle_t io_receive_queue;
SemaphoreHandle_t nrf_mutex;

static void IRAM_ATTR nrf_gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(nrf_irq_queue, &gpio_num, NULL);
}

static void configure_nrf_irq_pin() {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = GPIO_INPUT_PIN_SEL,
        .pull_down_en = 0,
        .pull_up_en = 1
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(NRF_IRQ_GPIO, nrf_gpio_isr_handler, (void*)NRF_IRQ_GPIO);

    nrf_irq_queue = xQueueCreate(10, sizeof(uint32_t));
}

static void retry_timer_callback(void *arg) {
    if (retry_count < MAX_RETRIES) {
        retry_count++;

        if (xSemaphoreTake(nrf_mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t buffer[32];
            memset(buffer, 0, sizeof(buffer));
            memcpy(buffer, retry_msg.message_type, MESSAGE_TYPE_SIZE);
            memcpy(buffer + MESSAGE_TYPE_SIZE, retry_msg.payload, PAYLOAD_SIZE);

            Nrf24_setTADDR(&nrf_device, (uint8_t *)slave_addresses[retry_msg.slave_id - 2]);
            Nrf24_send(&nrf_device, buffer);

            if (!Nrf24_isSend(&nrf_device, 10)) {
                esp_timer_start_once(retry_timer, 50000);
            }

            xSemaphoreGive(nrf_mutex);
        } else {
            ESP_LOGW("RETRY", "NRF mutex unavailable — retry postponed");
        }
    } else {
        ESP_LOGE("RETRY", "Failed to send message after %d retries", MAX_RETRIES);
    }
}

void io_thread_init(void) {
    io_send_queue = xQueueCreate(IO_QUEUE_SIZE, sizeof(io_message_t));
    io_receive_queue = xQueueCreate(IO_QUEUE_SIZE, sizeof(io_message_t));
    nrf_mutex = xSemaphoreCreateMutex();

    Nrf24_init(&nrf_device);
    ESP_LOGE(pcTaskGetName(NULL), "nrf24l01 init");
    uint8_t payload = 32;
    uint8_t channel = CONFIG_RADIO_CHANNEL;
    Nrf24_config(&nrf_device, channel, payload);
    configure_nrf_irq_pin();
    ESP_LOGE(pcTaskGetName(NULL), "nrf24l01 config");
    esp_err_t ret = Nrf24_setRADDR(&nrf_device, (uint8_t *)"1RECV");
    if (ret != ESP_OK) {
        ESP_LOGE(pcTaskGetName(NULL), "nrf24l01 not installed");
        while(1) { vTaskDelay(1); }
    }
    ESP_LOGE(pcTaskGetName(NULL), "r1 added");
    for (int i = 0; i < 4; i++) {
        Nrf24_addRADDR(&nrf_device, i + 2, slave_addresses[i][0]);
    }
    ESP_LOGE(pcTaskGetName(NULL), "all rs added");
    ESP_LOGW(pcTaskGetName(NULL), "Set RF Data Ratio to 1MBps");
	Nrf24_SetSpeedDataRates(&nrf_device, 0);
    const esp_timer_create_args_t timer_args = {
        .callback = &retry_timer_callback,
        .name = "retry_timer"
    };
    esp_timer_create(&timer_args, &retry_timer);
    ESP_LOGE(pcTaskGetName(NULL), "timer init");
    uint8_t status_value = (1 << RX_DR) | (1 << TX_DS) | (1 << MAX_RT);
    Nrf24_configRegister(&nrf_device, STATUS, status_value);

    xTaskCreate(io_send_task, "IO_Send_Task", 4096, NULL, configMAX_PRIORITIES - 2, NULL);
    xTaskCreate(io_receive_task, "IO_Receive_Task", 4096, NULL, configMAX_PRIORITIES - 2, NULL);
}

bool io_enqueue_send(io_message_t *msg) {
    if (msg->slave_id >= 2 && msg->slave_id <= 5) {
        if (xQueueSend(io_send_queue, msg, portMAX_DELAY) == pdTRUE) {
            return true;
        }
    }
    return false;
}

bool io_dequeue_receive(io_message_t *msg) {
    return xQueueReceive(io_receive_queue, msg, portMAX_DELAY) == pdTRUE;
}

void io_send_task(void *pvParameters) {
    io_message_t msg;
    uint8_t buffer[32];

    while (1) {
        if (xQueueReceive(io_send_queue, &msg, portMAX_DELAY) == pdTRUE) {
            memset(buffer, 0, sizeof(buffer));
            memcpy(buffer, msg.message_type, MESSAGE_TYPE_SIZE);
            memcpy(buffer + MESSAGE_TYPE_SIZE, msg.payload, PAYLOAD_SIZE);

            retry_msg = msg;
            retry_count = 0;

            if (xSemaphoreTake(nrf_mutex, portMAX_DELAY) == pdTRUE) {
                Nrf24_setTADDR(&nrf_device, (uint8_t *)slave_addresses[msg.slave_id - 2]);
                Nrf24_send(&nrf_device, buffer);

                while (Nrf24_isSending(&nrf_device)) {
                    // esp_timer_start_once(retry_timer, 50000);
                    // ESP_LOGE("IO_SEND", "Sending");
                    if (retry_count < MAX_RETRIES) {
                    //     Nrf24_send(&nrf_device, buffer);
                        retry_count++;
                    } else {
                        ESP_LOGE("IO_SEND", "Failed to send message after %d retries", MAX_RETRIES);
                    //     uint8_t status = Nrf24_getStatus(&nrf_device);
                    //     /*
                    //         if sending successful (TX_DS) or max retries exceded (MAX_RT).
                    //     */
                    //     printf("status: %d\n",status);
                    //     break;
                        break;
                    
                    }
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                }
                xSemaphoreGive(nrf_mutex);
            } else {
                ESP_LOGE("IO_SEND", "Failed to acquire NRF mutex");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void io_receive_task(void *pvParameters) {
    ESP_LOGI(pcTaskGetName(NULL), "Started receive task");

    uint32_t io_num;
    uint8_t raw_data[32];
    io_message_t msg;

    while (1) {
        if (xQueueReceive(nrf_irq_queue, &io_num, portMAX_DELAY)) {
            if (xSemaphoreTake(nrf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (Nrf24_dataReady(&nrf_device)) {
                    Nrf24_getData(&nrf_device, raw_data);
                    xSemaphoreGive(nrf_mutex);

                    memcpy(msg.message_type, raw_data, 8);
                    msg.message_type[7] = '\0';
                    memcpy(msg.payload, raw_data + 8, 24);
                    msg.payload_length = 24;
                    msg.slave_id = Nrf24_getDataPipe(&nrf_device);

                    if (strcmp(msg.message_type, "PING") == 0) {
                        hb_register_ping_response(msg.slave_id);
                    } else {
                        char raw_str[33];
                        memcpy(raw_str, raw_data, 32);
                        raw_str[32] = '\0';  // Null-terminate for logging
                        ESP_LOGI("NRF_IO", "Received raw string (slave %d): \"%s\"", msg.slave_id, raw_str);
                        xQueueSend(io_receive_queue, &msg, portMAX_DELAY);
                    }
                }
                    xSemaphoreGive(nrf_mutex);  // Just in case data wasn't ready
                
            } else {
                ESP_LOGW("NRF_IO", "Could not acquire mutex in receive task");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}





