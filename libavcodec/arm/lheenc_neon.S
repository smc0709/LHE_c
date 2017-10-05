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
 * final results. VMOV LDRB STRB.
 */

  vld1.8     d0, [r0]             // Load original_color into a register
  vld1.8     d1, [r1]             // Load predicted_component into a register
  vmov.i8    d2, #0xFF            // Create the vector of min error filled with 255
  vpush      {d8}                    // Backup of d8 to preserve data.

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
  vpop       {d8}                 // Restore the bank d8
  ldr        r1, [sp]             // Read  the fifth argument from the stack

  vst1.8     d5, [r3]             // Save the hops in the memory.
  vst1.8     d6, [r1]             // Save the component prediction in the memory.

  bx         lr
endfunc

function calulate_hop_prediction_binary_neon_2, export=1
/*
 * This function takes the next parameters:
 * uint8_t *original_color             Stored in r0
 * uint8_t *predicted_component        Stored in r1
 * uint8_t *hop_1                      Stored in r2
 * uint8_t shifted_pix_original_data                    Stored in r3
 * uint8_t *hops                       Stored in the stack
 * uint8_t *component_prediction       Stored in the stack +1
 *
 * Pseudocode:
 * - Backup of the registers used.
 * - Load original_color using the shift into ARM registers.
 * - Load the content of the AMR reg into the NEON register.
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
 * - Save the hops in the ARM Registers.
 * - Save form the ARM registers following the shift.
 * - Save the component prediction in the ARM Registers.
 * - Save form the ARM registers following the shift.
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
 *  r4,r5, r6, r7, r8, r9, r10, r11 -> Used to load the values with the shift
 * TODO -> When storing review the shift that has to be done when storing the
 * final results. VMOV LDRB STRB.
 */

  push       {r4, r5. r6, r7, r8, r9, r10, r11} //Backup to preserve the data.
  vpush      {d8}                 // Backup of d8 to preserve data.

  ldrb       r4, [r0], r3         //
  ldrb       r5, [r0], r3         //
  ldrb       r6, [r0], r3         //
  ldrb       r7, [r0], r3         // Load Original color into ARM Registers
  ldrb       r8, [r0], r3         //
  ldrb       r9, [r0], r3         //
  ldrb       r10, [r0], r3        //
  ldrb       r11, [r0], r3        //
  vmov.8     d0[0], r4            //
  vmov.8     d0[1], r5            //
  vmov.8     d0[2], r6            //
  vmov.8     d0[3], r7            //
  vmov.8     d0[4], r8            //   Now from ARM Registers to d0 in order
  vmov.8     d0[5], r9            //
  vmov.8     d0[6], r10           //
  vmov.8     d0[7], r11           //

  vld1.8     d1, [r1]             // Load predicted_component into a register
  vmov.i8    d2, #0xFF            // Create the vector of min error filled with 255


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
  vpop       {d8}                 // Restore the bank d8
  ldr        r0, [sp, #-32]       // Read the fifth argument(hops) from the
                                  // stack. Right now we have 8 registers so the
                                  // shift is 8*4=32 bytes
  ldr        r1, [sp, #-36]       // Read the sixth argument(predicted) from
                                  // the stack. We have 9 registers so shift is
                                  // 9*4= 36 bytes.
  vmov.u8    r4, d5[0]
  vmov.u8    r5, d5[1]
  vmov.u8    r6, d5[2]
  vmov.u8    r7, d5[3]
  vmov.u8    r8, d5[4]
  vmov.u8    r9, d5[5]
  vmov.u8    r10, d5[6]
  vmov.u8    r11, d5[7]
  strb       r4, [r0], r3         //
  strb       r5, [r0], r3         //
  strb       r6, [r0], r3         //
  strb       r7, [r0], r3         // Save the hops in the memory.
  strb       r8, [r0], r3         //
  strb       r9, [r0], r3         //
  strb       r10, [r0], r3        //
  strb       r11, [r0], r3        //

  vmov.u8    r4, d6[0]
  vmov.u8    r5, d6[1]
  vmov.u8    r6, d6[2]
  vmov.u8    r7, d6[3]
  vmov.u8    r8, d6[4]
  vmov.u8    r9, d6[5]
  vmov.u8    r10, d6[6]
  vmov.u8    r11, d6[7]
  strb       r4, [r1], r3         //
  strb       r5, [r1], r3         //
  strb       r6, [r1], r3         //
  strb       r7, [r1], r3         // Save the component prediction in the memory.
  strb       r8, [r1], r3         //
  strb       r9, [r1], r3         //
  strb       r10, [r1], r3        //
  strb       r11, [r1], r3        //

  pop        {r4, r5. r6, r7, r8, r9, r10, r11} // Restore the bank d8

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
	/*if (hop_number<=HOP_POS_1 && hop_number>=HOP_NEG_1)*/
	/*small_hop=true o small_hop=false*/
	vld1.8		d0,[r0]			/*Cargo hop_number[] en d0*/
	vmov.i8	d1, #5				/*Cargo un 5 que es hop_pos_1*/
	vmov.i8 	d2, #3			/*Cargo un 3 que es hop_neg_1*/
	vcge.u8	d3,d1,d0		 	/*5>=hop_number[], devuelve todo 1s en las posciones que se cumple*/
	vcge.u8	d4,d0,d2		 	/*hop_number[]>=3, devuelve todo 1s en las posciones que se cumple*/
	vand 		d5,d3,d4			/*And de los anteriores, si se cumplen los dos (los dos son todo 1s) es true y devuelve un 1*/
	/*d5 contiene el array de small hop que vale 0 para false y 1 para true*/

	/*Segundo if*/
	/*if( (small_hop) && (last_small_hop))*/
	/*hop1=hop1-1 o hop_1=MAX_HOP1*/
	/*Tiene un if anidado que haré mas adelante*/
	vld1.8 		d0, [r1]			/*Cargo hop1[] en d0*/
	vld1.8 		d1, [r2]		 	/*Cargo last_small_hop[] en d1*/
	vand 		d2, d5, d1			/*And de small hop que es d5, y last_small_hop que es d1*/
	vabd.u8		d0, d0, d2 		/*Resta de hop1[] - d2, que es el resultado del and anterior*/
	/*Esta resta lo que hace es hop1=hop1-1 en los casos en que se cumple small_hop && last_small_hop*/
	/*Porque el and, devuelve un 1 cuando es true y un 0 cuando no, así que al restar el resultado del and*/
	/*Lo que hacemos es restar 1 en las posiciones que tenemos que restarlo*/
	vmov.i8 	d4, #10			/*En d4 ponemos todo 10, que es MAX_HOP1*/
	vmov.i8 	d3, #0 			/*0 auxiliar para la siguiente instruccion*/
	vcgt.u8 	d2, d2, d3 	/*chapucilla para pasar d2 de 1,0 a 255,0*/
	/*Comparamos si es > 0, cuando se cumple, en los 1s los convierte en 255 que nos viene bien ahora*/
	vbif 			d0, d4,d2 	/*Si el bit del segundo oper es 1, dejo el destino igual, si es 0, meto en el destino el primer oper*/
	/*Cuando es 0 el bit del segundo operando se mete en el primer operando el 10 que teniamos en d4*/
	/*En las posiciones en que en d2 hay 0s se meterá un 10, que es lo que dice la función, cuando no se cumple el segundo if: hop1=MAX_HOP1*/
	/*Esto tiene sentido porque en d2 estaba el resultado de ese and, dando 0 cuando no se cumplía y uno cuando sí*/
	/*Ahora d0 contiene hop1-1 cuando hace falta y max hop 1 cuando hace falta*/

	/*if anidado, si hop1 < min_hop1  entonces hop1 = min_hop1*/
	vmov.i8 	d3,#4			/*Metemos un 4 en d3 que es min_hop1*/
	vcgt.u8	d4, d3, d0	/*Comprobamos si 4>hop1[]*/
	vbit		d0, d3,d4 	/*Al reves que vbif, cuando sea mayor (todo 1s) mete un 4 en lugar de lo que había, que era mayor que 4 y por eso era todo 1s*/

	/*Ahora d0 que es hop1[] esta bien ajustado*/
	/*Lo que queda es devolver correctamente los arrays*/

	vst1.8 		d0, [r1]  	/*hop1 o column hop1*/
	vmov.i8 	d6, #254		/*Hago una resta saturada para pasar de 255 a 1*/
	vqsub.u8	d5, d5,d6
	vst1.8		d5, [r2]		/*last small hop que ahora es small hop*/
	bx	lr

endfunc
