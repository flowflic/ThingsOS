// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_METALIO_TILT_MOUNT_H
#define THINGSOS_METALIO_TILT_MOUNT_H
/* Measured on Metalio in portrait: left wrist tilt increases chip Y; right
   tilt decreases Y. Its chip is rotated relative to Pico. Rotate raw axes
   by 90 degrees before the shared Pico screen transform: (Y,-X,Z).
   This preserves a proper rotation and the existing landscape transform. */
static inline void thingsos_metalio_tilt_mount(int x,int y,int z,int* canonical_x,int* canonical_y,int* canonical_z) {
    *canonical_x=y;*canonical_y=-x;*canonical_z=z;
}
#endif
