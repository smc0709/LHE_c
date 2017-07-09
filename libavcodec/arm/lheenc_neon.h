/*
 * This file is part of the LHE implementation.
 *
 * It contains the declarations of the functions written in ARM assembly using
 * NEON instructions. Only those are declarated in the case it is compiled with NEON flag.
 * The funtion we want to modify is "lhe_basic_encode_one_hop_per_pixel_block".
 *
 */

#include "config.h"
#include <stdint.h>

void calulate_hop_prediction_binary_neon( uint8_t *original_color,
                                          uint8_t *predicted_component,
                                          uint8_t *hop_1, uint8_t *hops,
                                          uint8_t *component_prediction);

void calulate_hop_prediction_binary_neon_2( uint8_t *original_color,
                                          uint8_t *predicted_component,
                                          uint8_t *hop_1, uint8_t distance,
                                          uint8_t *hops,
                                          uint8_t *component_prediction);

void lhe_neon_prediction( uint8_t pixel_before[], uint8_t pixel_upafter[],
                          uint8_t pixel_predicted[]);

void lhe_h1adapt_neon( uint8_t *hop_number, uint8_t *column_hop_1,
                       uint8_t *column_last_small_hop);
