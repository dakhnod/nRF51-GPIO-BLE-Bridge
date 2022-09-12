#include "encoding.h"

uint8_t encoding_get_pin_bits(uint8_t *pin_data, uint32_t pin_data_length, uint32_t pin_index){
        uint32_t bit_index_full = pin_index * 2;

        uint32_t byte_index = bit_index_full / 8;
        uint8_t bit_index = bit_index_full % 8;

        uint8_t current_byte = pin_data[byte_index];

        uint8_t output_bits = (current_byte >> (6 - bit_index)) & 0b00000011;

        return output_bits;
}