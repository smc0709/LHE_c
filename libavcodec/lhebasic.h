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
#include "internal.h"
#include "bytestream.h"
            
//OpenCL
#if CONFIG_OPENCL
#include "lhebasic_opencl.h"
#else
#include "lhebasic_prec.h"
#endif

//Configuration 
#define MIDDLE_VALUE false
#define CHROMA_FACTOR_WIDTH 2
#define CHROMA_FACTOR_HEIGHT 1

#define BLOCK_WIDTH_Y 64
#define BLOCK_HEIGHT_Y 64
#define BLOCK_WIDTH_UV BLOCK_WIDTH_Y/CHROMA_FACTOR_WIDTH
#define BLOCK_HEIGHT_UV BLOCK_HEIGHT_Y/CHROMA_FACTOR_HEIGHT

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


#define NO_SYMBOL 20

//Huffman
#define LHE_MAX_HUFF_SIZE 9
#define LHE_HUFFMAN_NODE_BITS 4
#define LHE_HUFFMAN_TABLE_BITS LHE_MAX_HUFF_SIZE*LHE_HUFFMAN_NODE_BITS


typedef struct LheHuffEntry {
    uint8_t  sym;
    uint8_t  len;
    uint32_t code;
    uint64_t count;
} LheHuffEntry;

int lhe_generate_huffman_codes(LheHuffEntry *he);
double time_diff(struct timeval x , struct timeval y);
int count_bits (int num);


/**
 * Calculates lhe init cache
 */
void lhe_init_cache (LheBasicPrec *prec);