/*
 * This file is part of the LHE implementation.
 *
 * It contains the funtions in assembly that will be called. Those functions
 * are declared in lhe_init_neon.h and substitute funtions with assembly commands.
 *
 */

#include "libavutil/arm/asm.S"


function calulate_hop_prediction_binary_neon, export=1
/*
 * This function takes the next parameters:
 * uint8_t *original_color             Stored in r0
 * uint8_t *predicted_component        Stored in r1
 * uint8_t *hop_1                      Stored in r2
 * uint8_t *hops                       Stored in r3
 * uint8_t *component_prediction       Stored in the stack
 *
 * Pseudocode:
 * - Backup of the registers used.
 * - Load original_color into a register
 * - Load predicted_component into a register
 * - Create the vector of min error filled with 255
 * - For each of the hops we have
 *   - Load and calculate the current hop
 *   - Compute value with the prediction and the hop_1- saturating operation
 *   - Compute the absolute error.
 *   - Create a mask comparing min error and error.
 *   - Update the hops with the mask.
 *   - Update the component_prediction with the mask.
 *   - Update the min _error.
 * - Save the hops in the memory.
 * - Save the component prediction in the memory.
 * - Restore the registers used.
 *
 * Registers use are:
 *  d0 -> original_color
 *  d1 -> predicted_component
 *  d2 -> min_error
 *  d3 -> error
 *  d4 -> mask of errors
 *  d5 -> hops
 *  d6 -> component prediction
 *  d7 -> hop_1 (later become HOP_POS_x)
 *  d8 -> predicted_component_w_hop
 *
 * TODO -> When storing review the shift that has to be done when storing the
 * final results.
 */

	vld1.8     d0, [r0]             // Load original_color into a register
	vld1.8     d1, [r1]             // Load predicted_component into a register
  vmov.i8    d2, #0xFF            // Create the vector of min error filled with 255
  vst1.8     d8, [sp]!            // Backup of d8 to preserve data.

 // HOP_0
  vabd.u8    d3, d0, d1           // Compute the absolute error.
  vcgt.u8    d4, d2, d3           // Create a mask comparing min error and error.

  vbit       d2, d3, d4           // Update the min _error.
  vbit       d6, d1, d4           // Update the component_prediction with the mask.
  vmov.i8    d3, #0x04
  vbit       d5, d3, d4           // Update the hops with the mask.


 // HOP_POS_1
  vld1.8     d7, [r2]             // Load and calculate the current hop
  vqadd.u8   d8, d7, d1           // Compute value with the prediction and the current hop.
  vabd.u8    d3, d0, d8           // Compute the absolute error.
  vcgt.u8    d4, d2, d3           // Create a mask comparing min error and error.

  vbit       d2, d3, d4           // Update the min _error.
  vbit       d6, d8, d4           // Update the component_prediction with the mask.
  vmov.i8    d3, #0x05
  vbit       d5, d3, d4           // Update the hops with the mask.

// HOP_POS_2
  vqshl.u8   d7, d7, #0x01        // Load and calculate the current hop
  vqadd.u8   d8, d7, d1           // Compute value with the prediction and the current hop.
  vabd.u8    d3, d0, d8           // Compute the absolute error.
  vcgt.u8    d4, d2, d3           // Create a mask comparing min error and error.

  vbit       d2, d3, d4           // Update the min _error.
  vbit       d6, d8, d4           // Update the component_prediction with the mask.
  vmov.i8    d3, #0x06
  vbit       d5, d3, d4           // Update the hops with the mask.

 // HOP_POS_3
  vqshl.u8   d7, d7, #0x01        // Load and calculate the current hop
  vqadd.u8   d8, d7, d1           // Compute value with the prediction and the current hop.
  vabd.u8    d3, d0, d8           // Compute the absolute error.
  vcgt.u8    d4, d2, d3           // Create a mask comparing min error and error.

  vbit       d2, d3, d4           // Update the min _error.
  vbit       d6, d8, d4           // Update the component_prediction with the mask.
  vmov.i8    d3, #0x07
  vbit       d5, d3, d4           // Update the hops with the mask.

 // HOP_POS_4
  vqshl.u8   d7, d7, #0x01        // Load and calculate the current hop
  vqadd.u8   d8, d7, d1           // Compute value with the prediction and the current hop.
  vabd.u8    d3, d0, d8           // Compute the absolute error.
  vcgt.u8    d4, d2, d3           // Create a mask comparing min error and error.

  vbit       d2, d3, d4           // Update the min _error.
  vbit       d6, d8, d4           // Update the component_prediction with the mask.
  vmov.i8    d3, #0x08
  vbit       d5, d3, d4           // Update the hops with the mask.

 // HOP_NEG_1
  vld1.8     d7, [r2]             // Load and calculate the current hop
  vqsub.u8   d8, d1, d7           // Compute value with the prediction and the current hop.
  vabd.u8    d3, d0, d8           // Compute the absolute error.
  vcgt.u8    d4, d2, d3           // Create a mask comparing min error and error.

  vbit       d2, d3, d4           // Update the min _error.
  vbit       d6, d8, d4           // Update the component_prediction with the mask.
  vmov.i8    d3, #0x03
  vbit       d5, d3, d4           // Update the hops with the mask.

 // HOP_NEG_2
  vqshl.u8   d7, d7, #0x01        // Load and calculate the current hop
  vqsub.u8   d8, d1, d7           // Compute value with the prediction and the current hop.
  vabd.u8    d3, d0, d8           // Compute the absolute error.
  vcgt.u8    d4, d2, d3           // Create a mask comparing min error and error.

  vbit       d2, d3, d4           // Update the min _error.
  vbit       d6, d8, d4           // Update the component_prediction with the mask.
  vmov.i8    d3, #0x02
  vbit       d5, d3, d4           // Update the hops with the mask.

 // HOP_NEG_3
  vqshl.u8   d7, d7, #0x01        // Load and calculate the current hop
  vqsub.u8   d8, d1, d7           // Compute value with the prediction and the current hop.
  vabd.u8    d3, d0, d8           // Compute the absolute error.
  vcgt.u8    d4, d2, d3           // Create a mask comparing min error and error.

  vbit       d2, d3, d4           // Update the min _error.
  vbit       d6, d8, d4           // Update the component_prediction with the mask.
  vmov.i8    d3, #0x01
  vbit       d5, d3, d4           // Update the hops with the mask.

// HOP_NEG_4
  vqshl.u8   d7, d7, #0x01        // Load and calculate the current hop
  vqsub.u8   d8, d1, d7           // Compute value with the prediction and the current hop.
  vabd.u8    d3, d0, d8           // Compute the absolute error.
  vcgt.u8    d4, d2, d3           // Create a mask comparing min error and error.

  vbit       d2, d3, d4           // Update the min _error.
  vbit       d6, d8, d4           // Update the component_prediction with the mask.
  vmov.i8    d3, #0x00
  vbit       d5, d3, d4           // Update the hops with the mask.

// Save results and restore state.
  vld1.8     d8, [sp]!             // Restore the bank d8- TODO -Review VPUSH instruction
  ldmia      sp!, r0              // pop argument from the stack

  vst1.8     d5, [r3]             // Save the hops in the memory.
  vst1.8     d6, [r0]             // Save the component prediction in the memory.

  bx         lr
endfunc

function lhe_neon_prediction, export=1

    vld1.8		d0, [r0]

    vld1.8		d2, [r1]

    vmovl.u8	q0, d0
    vmovl.u8	q1, d2

    vhadd.u16		q2, q0, q1

    vmovn.u16		d0, q2

    vst1.8		d0, [r2]!

    bx	lr
endfunc

function lhe_h1adapt_neon, export =1
	/*Primer If*/
	vld1.8		d0,[r0]			 /*Cargo hop_number[]*/
	vmov.i8	d1, #5
	vmov.i8 	d2, #3
	vcge.u8	d3,d1,d0		 /*5>=hop_number[]*/
	vcge.u8	d4,d0,d2		 /*hop_number[]>=3*/
	vand 		d5,d3,d4		/*And de los anteriores*/
	/*d5 contiene el array de small hop*/

	/*Segundo if*/
	vld1.8 		d0, [r1]	 /*Cargo hop1[]*/
	vld1.8 		d1, [r2]		 /*Cargo last_small_hop[]*/
	vand 		d2, d5, d1
	vabd.u8		d0, d0, d2 /*En d0 con h1-1 si es necesario*/
	vmov.i8 	d4, #10 /*Max hop 1*/
	vmov.i8 	d3, #0 /*0 auxiliar para la siguiente instruccion*/
	vcgt.u8 	d2, d2, d3 /*chapucilla para pasar d2 de 1,0 a 255,0*/
	vbif 			d0, d4,d2 /*Si el bit del segundo oper es 1, dejo el destino igual, si es 0, meto en el destino el primer oper*/
	/*Ahora d0 contiene hop1-1 cuando hace falta y max hop 1 cuando hace falta*/

	/*if anidado, si hop1 < min_hop1  entonces hop1 = min_hop1*/
	vmov.i8 	d3,#4
	vcgt.u8	d4, d3, d0
	vbit		d0, d3,d4 /*Al reves que vbif*/

	/*Ahora d0 que es hop1[] esta bien ajustado*/
	/*Lo que queda es devolver correctamente los arrays*/

	vst1.8 		d0, [r1]  	/*hop1 o column hop1*/
	vmov.i8 	d6, #254	/*Hago una resta saturada para pasar de 255 a 1*/
	vqsub.u8	d5, d5,d6
	vst1.8		d5, [r2]		/*last small hop que ahora es small hop*/
	bx	lr

endfunc
