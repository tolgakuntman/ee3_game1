#pragma once
#include <stdint.h>
#include <stdbool.h>

bool send_robot_command(uint8_t slave_id, uint8_t ship_id, uint8_t row, uint8_t col, uint8_t not_vertical,uint8_t not_place);
bool send_led_matrix_update(uint8_t slave_id);