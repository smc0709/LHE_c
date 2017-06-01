/*
 * This file is part of the LHE implementation.
 *
 * It contains the funtions in assembly that will be called. Those functions 
 * are declared in lhe_init_neon.h and substitute funtions with assembly commands.
 *
 */

#include "libavutil/arm/asm.S"

function lhe_enc_predict_from_previous_neon, export=1
/*
 * This function takes the next parameters:
 * uint8_t predicted_component_neon[8]  -------Stored in r0
 * uint8_t component_prediction[]   	-------Stored in r1
 * int pix				-------Stored in r2
 *
 * It replaces:
 * 
 * predicted_component=lhe->component_prediction[pix-1] 
 *
 * It must be done for 8 pixeles of 1 byte each
 */
	add  	r1, r1, r2
	subs	r1, r1, #1
	
	vld1.8  d0, [r1]
	
	vst1.8  d0, [r0]

	bx      lr
endfunc


function lhe_enc_predict_from_neighbour_neon, export=1
/*
 * This function takes the next parameters:
 * uint8_t predicted_component_neon[8]  -------Stored in r0
 * uint8_t component_prediction[]   	-------Stored in r1
 * int pix				-------Stored in r2
 * uint32_t width			-------Stored in r3
 *
 * It replaces:
 * 
 * predicted_component=(lhe->component_prediction[pix-1]+lhe->component_prediction[pix+1-proc->width])>>1;
 *
 * It must be done for 8 pixeles of 1 byte each
 */
	
 	add  	r1, r1, r2
	subs	r1, r1, #1 
	vld1.8  d0, [r1]
	
	add	r2, r1, #2
	subs	r2, r2, r3
	vld1.8  d1, [r3]
	
	vhadd.u8 d2, d0, d1 /* Adds and halves at the same isntruction:D */
	
	vst1.8  d2, [r0]
 
 	bx              lr
endfunc
