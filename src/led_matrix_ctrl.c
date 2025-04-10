#include "led_matrix_ctrl.h"
#include "nrf_io.h"
#include <string.h>
#include "io_builder.h"
static uint16_t ledMatrix1[MATRIX_SIZE];
static uint16_t ledMatrix2[MATRIX_SIZE];

void led_matrix_init(void) {
    for (uint8_t row = 0; row < MATRIX_SIZE; row++) {
        ledMatrix1[row] = 0xFFFF;
        ledMatrix2[row] = 0xFFFF;

        for (uint8_t col = 0; col < MATRIX_SIZE; col++) {
            // Matrix 1 (like PIC18): Blue ON (bit 2), others OFF
            ledMatrix1[row] &= ~(1 << (col * 3 + 1));
            ledMatrix1[row] |=  (1 << (col * 3));
            ledMatrix1[row] |=  (1 << (col * 3 + 2));

            // Matrix 2: Blue ON (bit 1), others OFF
            ledMatrix2[row] &= ~(1 << (col * 3 + 2));
            ledMatrix2[row] |=  (1 << (col * 3));
            ledMatrix2[row] |=  (1 << (col * 3 + 1));
        }
    }
    send_led_matrix_update(3); // Send update to slave 3
}

void setLEDColor(uint8_t matrixNum, uint8_t row, uint8_t col, Color color) {
    if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) return;

    uint16_t* matrix = (matrixNum == 1) ? ledMatrix1 : ledMatrix2;
    uint8_t bit0, bit1, bit2;

    if (matrixNum == 1) {
        // Same as PIC: R=0, G=1, B=2
        bit0 = col * 3;
        bit1 = col * 3 + 1;
        bit2 = col * 3 + 2;
    } else {
        // Matrix 2 on PIC: R=0, B=1, G=2
        bit0 = col * 3;
        bit1 = col * 3 + 1;
        bit2 = col * 3 + 2;
    }

    switch (color) {
        case COLOR_RED:
            matrix[row] |=  (1 << bit1); // off blue
            matrix[row] |=  (1 << bit2); // off green
            matrix[row] &= ~(1 << bit0); // on red
            break;
        case COLOR_GREEN:
            matrix[row] |=  (1 << bit0); // off red
            matrix[row] |=  (1 << bit2); // off blue
            matrix[row] &= ~(1 << bit1); // on green
            break;
        case COLOR_BLUE:
            matrix[row] |=  (1 << bit0); // off red
            matrix[row] |=  (1 << bit1); // off green
            matrix[row] &= ~(1 << bit2); // on blue
            break;
        default: // COLOR_OFF
            matrix[row] |= (1 << bit0) | (1 << bit1) | (1 << bit2);
            break;
    }
    send_led_matrix_update(3); // Send update to slave 3
}
void setLEDColorAll(uint8_t matrixNum, Color color) {
    for (uint8_t row = 0; row < MATRIX_SIZE; row++) {
        for (uint8_t col = 0; col < MATRIX_SIZE; col++) {
            setLEDColor(matrixNum, row, col, color);
        }
    }
}
void setLEDColorAllRow(uint8_t matrixNum, uint8_t row, Color color) {
    for (uint8_t col = 0; col < MATRIX_SIZE; col++) {
        setLEDColor(matrixNum, row, col, color);
    }
}
void setLEDColorAllCol(uint8_t matrixNum, uint8_t col, Color color) {
    for (uint8_t row = 0; row < MATRIX_SIZE; row++) {
        setLEDColor(matrixNum, row, col, color);
    }
}


void getMatrix1Data(uint16_t* buffer) {
    memcpy(buffer, ledMatrix1, MATRIX_SIZE * sizeof(uint16_t));
}

void getMatrix2Data(uint16_t* buffer) {
    memcpy(buffer, ledMatrix2, MATRIX_SIZE * sizeof(uint16_t));
}

void sendMatricesOverNRF(uint8_t slave_id) {
    uint8_t payload[1 + MATRIX_SIZE * 2 * sizeof(uint16_t)];
    uint8_t index = 0;

    payload[index++] = 0x01;  // CMD_FULL_MATRIX_UPDATE

    for (uint8_t i = 0; i < MATRIX_SIZE; i++) {
        payload[index++] = ledMatrix1[i] & 0xFF;
        payload[index++] = (ledMatrix1[i] >> 8) & 0xFF;
    }

    for (uint8_t i = 0; i < MATRIX_SIZE; i++) {
        payload[index++] = ledMatrix2[i] & 0xFF;
        payload[index++] = (ledMatrix2[i] >> 8) & 0xFF;
    }

    // nrf_send_to_slave(slave_id, payload, index); // Your existing NRF function
}
