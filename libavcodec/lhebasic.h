/*
 * LHE format definitions
 */

/**
 * @file
 * LHE format definitions.
 */

#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <sys/time.h>
#include "huffman.h"

//Configuration 
#define MIDDLE_VALUE false

//Params for precomputation
#define H1_RANGE 20
#define Y_MAX_COMPONENT 256
#define R_MIN 20
#define R_MAX 40
#define RATIO R_MAX
#define NUMBER_OF_HOPS 9
#define SIGN 2
    
//Param definitions
#define POSITIVE 0
#define NEGATIVE 1

//Color component values
#define MAX_COMPONENT_VALUE 255
#define MIN_COMPONENT_VALUE 0

//LHE params
#define MAX_HOP_1 10
#define MIN_HOP_1 4
#define START_HOP_1 (MAX_HOP_1 + MIN_HOP_1) / 2
#define PARAM_R 25

//Hops
#define HOP_NEG_4 0 // h-4 
#define HOP_NEG_3 1 // h-3 
#define HOP_NEG_2 2 // h-2 
#define HOP_NEG_1 3 // h-1 
#define HOP_0 4     // h0 
#define HOP_POS_1 5 // h1 
#define HOP_POS_2 6 // h2 
#define HOP_POS_3 7 // h3 
#define HOP_POS_4 8 // h4 

//File symbols
#define NO_SYMBOL 20
#define SYM_HOP_O 0
#define SYM_HOP_UP 1
#define SYM_HOP_POS_1 2
#define SYM_HOP_NEG_1 3
#define SYM_HOP_POS_2 4 
#define SYM_HOP_NEG_2 5
#define SYM_HOP_POS_3 6
#define SYM_HOP_NEG_3 7
#define SYM_HOP_POS_4 8
#define SYM_HOP_NEG_4 9

//Huffman
#define LHE_MAX_HUFF_SIZE 10
#define LHE_HUFFMAN_TABLE_SIZE_BITS LHE_MAX_HUFF_SIZE * 4
#define LHE_HUFFMAN_TABLE_SIZE_BYTES  LHE_HUFFMAN_TABLE_SIZE_BITS/ 8 //size in bytes
#define LHE_HUFFMAN_NODE_BITS 4
#define LHE_MAX_BITS 9

static const uint8_t lhe_huff_coeff_map[] = {
    SYM_HOP_O, SYM_HOP_UP, SYM_HOP_POS_1, SYM_HOP_NEG_1, SYM_HOP_POS_2, SYM_HOP_NEG_2,
    SYM_HOP_POS_3, SYM_HOP_NEG_3,SYM_HOP_POS_4, SYM_HOP_NEG_4
};

typedef struct LheBasicPrec {
    uint8_t prec_luminance[H1_RANGE][Y_MAX_COMPONENT][RATIO][NUMBER_OF_HOPS]; // precomputed luminance component
    uint8_t best_hop [Y_MAX_COMPONENT][H1_RANGE][Y_MAX_COMPONENT][RATIO]; //original color- h1 - prediction - r
} LheBasicPrec; 

double time_diff(struct timeval x , struct timeval y);
int count_bits (int num);

void lhe_init_best_hop (LheBasicPrec* prec, int hop0_Y, int hop_1, int r_max);
/**
 * Calculates color component value for each hop.
 * Final color component ( luminance or chrominance) depends on hop1
 * Color component for negative hops is calculated as: hopi_Y = hop0_Y - hi
 * Color component for positive hops is calculated as: hopi_Y = hop0_Y + hi
 * where hop0_Y is hop0 component color value 
 * and hi is the luminance distance from hop0_Y to hopi_Y
 */
void lhe_init_hop_color_component_value (LheBasicPrec *prec, int hop0_Y, int hop1, int rmax,
                                                uint8_t hop_neg_4 [H1_RANGE][Y_MAX_COMPONENT], 
                                                uint8_t hop_neg_3 [H1_RANGE][Y_MAX_COMPONENT], 
                                                uint8_t hop_neg_2 [H1_RANGE][Y_MAX_COMPONENT],
                                                uint8_t hop_pos_2 [H1_RANGE][Y_MAX_COMPONENT],
                                                uint8_t hop_pos_3 [H1_RANGE][Y_MAX_COMPONENT],
                                                uint8_t hop_pos_4 [H1_RANGE][Y_MAX_COMPONENT]);

/**
 * Calculates color component value in the middle of the interval for each hop.
 * This method improves quality. Bassically this method init the value
 * of luminance of hops by the intermediate value between hops frontiers.
 *
 * h0--)(-----h1----center-------------)(---------h2--------center----------------)
 */
void lhe_init_hop_center_color_component_value (LheBasicPrec *prec, int hop0_Y, int hop1, int rmax,
                                                    uint8_t hop_neg_4 [H1_RANGE][Y_MAX_COMPONENT], 
                                                    uint8_t hop_neg_3 [H1_RANGE][Y_MAX_COMPONENT], 
                                                    uint8_t hop_neg_2 [H1_RANGE][Y_MAX_COMPONENT],
                                                    uint8_t hop_pos_2 [H1_RANGE][Y_MAX_COMPONENT],
                                                    uint8_t hop_pos_3 [H1_RANGE][Y_MAX_COMPONENT],
                                                    uint8_t hop_pos_4 [H1_RANGE][Y_MAX_COMPONENT]);


/**
 * Calculates lhe init cache
 */
void lhe_init_cache (LheBasicPrec *prec);