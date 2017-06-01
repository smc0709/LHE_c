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

  void lhe_enc_predict_from_previous_neon(uint8_t predicted_component_neon[],uint8_t component_prediction[],int pix);
  void lhe_enc_predict_from_neighbour_neon(uint8_t predicted_component_neon[],uint8_t component_prediction[],int pix,uint32_t width);
