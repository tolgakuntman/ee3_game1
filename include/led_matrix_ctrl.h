#ifndef LED_MATRIX_CTRL_H
#define LED_MATRIX_CTRL_H

#include <stdint.h>
#include <stdbool.h>

#define MATRIX_SIZE 5

typedef enum {
    COLOR_OFF = 0,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE
} Color;

void led_matrix_init(void);
void setLEDColor(uint8_t matrixNum, uint8_t row, uint8_t col, Color color);
void setLEDColorAll(uint8_t matrixNum, Color color);
void setLEDColorAllRow(uint8_t matrixNum, uint8_t row, Color color);
void setLEDColorAllCol(uint8_t matrixNum, uint8_t col, Color color);
void getMatrix1Data(uint16_t* buffer);  // buffer[5]
void getMatrix2Data(uint16_t* buffer);  // buffer[5]
void sendMatricesOverNRF(uint8_t slave_id); // Uses NRF24 to send both

#endif // LED_MATRIX_CTRL_H
