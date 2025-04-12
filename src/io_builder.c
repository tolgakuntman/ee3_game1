#include "nrf_io.h"
#include <string.h>
#include "esp_log.h"
#include "led_matrix_ctrl.h"

bool send_robot_command(uint8_t slave_id, uint8_t ship_id, uint8_t row, uint8_t col, uint8_t not_vertical, uint8_t not_place) {
    io_message_t msg;
    memset(&msg, 0, sizeof(msg));

    strncpy(msg.message_type, "ROBOT", MESSAGE_TYPE_SIZE);
    
    msg.payload[0] = ship_id;
    msg.payload[1] = row;
    msg.payload[2] = col;
    msg.payload[3] = not_vertical;
    msg.payload[4] = not_place;
    msg.payload_length = 24; // Optional, you could make it 24 always
    msg.slave_id = slave_id;
    ESP_LOGI("IO_SEND", "Sending command to slave %d: %s", slave_id, msg.message_type);
    return io_enqueue_send(&msg);
}
bool send_led_matrix_update(uint8_t slave_id) {
    io_message_t msg;
    memset(&msg, 0, sizeof(msg));

    strncpy(msg.message_type, "LED", MESSAGE_TYPE_SIZE);

    uint16_t mat1[MATRIX_SIZE];
    uint16_t mat2[MATRIX_SIZE];

    getMatrix1Data(mat1);
    getMatrix2Data(mat2);

    // Fill payload: [mat1 LSB..MSB], [mat2 LSB..MSB]
    for (int i = 0; i < MATRIX_SIZE; i++) {
        msg.payload[i * 2]     = mat1[i] & 0xFF;
        msg.payload[i * 2 + 1] = (mat1[i] >> 8) & 0xFF;
    }

    for (int i = 0; i < MATRIX_SIZE; i++) {
        msg.payload[MATRIX_SIZE * 2 + i * 2]     = mat2[i] & 0xFF;
        msg.payload[MATRIX_SIZE * 2 + i * 2 + 1] = (mat2[i] >> 8) & 0xFF;
    }

    msg.payload_length = MATRIX_SIZE * 2 * 2; // 2 matrices * 5 rows * 2 bytes = 20
    msg.slave_id = slave_id;

    // ESP_LOGI("IO_SEND", "Sending LED matrix update to slave %d", slave_id);
    return io_enqueue_send(&msg);
}
