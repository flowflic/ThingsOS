// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Keyboard-centre measurements on the connected Metalio, 2026-10-31:
   raw Q/T/P=(41,804)/(315,812)/(659,795), A/G/L=(61,901)/
   (354,891)/(615,888), Z/M=(126,969)/(550,961), space=(335,1055),
   delete=(620,971). Least-squares X is 0.89592*raw+29.25956;
   mean Y error is +18.3 px. Rounded fixed point keeps this device calibration
   out of shared key geometry. It is only applied to the editor's lower band;
   no measurements support extrapolating it to reading or other pages. */
static inline void thingsos_metalio_keyboard_coordinates(int* x,int* y) {
    *x=(*x*896+500)/1000+29;
    *y-=18;
}
