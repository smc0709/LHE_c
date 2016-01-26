
//Linearlized caches
#define PREC_LUMINANCE_CACHE_SIZE Y_MAX_COMPONENT*RATIO*H1_RANGE*NUMBER_OF_HOPS
#define BEST_HOP_CACHE_SIZE RATIO*H1_RANGE*Y_MAX_COMPONENT*Y_MAX_COMPONENT
#define H1_ADAPTATION_CACHE_SIZE H1_RANGE*NUMBER_OF_HOPS*NUMBER_OF_HOPS

//Params for precomputation
#define H1_RANGE 20
#define Y_MAX_COMPONENT 256
#define R_MIN 20
#define R_MAX 40
#define RATIO R_MAX
#define NUMBER_OF_HOPS 9
#define SIGN 2

#define INDEX_PREC_LUMINANCE(hop0_Y, rmax, hop_1, hop)          \
         RATIO*H1_RANGE*NUMBER_OF_HOPS*hop0_Y + H1_RANGE*NUMBER_OF_HOPS*rmax + NUMBER_OF_HOPS*hop_1 + hop
            
#define INDEX_BEST_HOP(ratio, hop_1, original_color, hop0_Y)    \
         H1_RANGE*Y_MAX_COMPONENT*Y_MAX_COMPONENT*ratio + Y_MAX_COMPONENT*Y_MAX_COMPONENT*hop_1 + Y_MAX_COMPONENT*original_color + hop0_Y
            
#define INDEX_H1_ADAPTATION(hop_1,hop_prev,hop)                 \
         NUMBER_OF_HOPS*NUMBER_OF_HOPS*hop_1 + NUMBER_OF_HOPS*hop_prev + hop
         


typedef struct LheBasicPrec {
    uint8_t prec_luminance[PREC_LUMINANCE_CACHE_SIZE]; // precomputed luminance component
    uint8_t best_hop [BEST_HOP_CACHE_SIZE]; //ratio - h1 - original color - prediction
    uint8_t h1_adaptation [H1_ADAPTATION_CACHE_SIZE]; //h1 adaptation cache
} LheBasicPrec; 