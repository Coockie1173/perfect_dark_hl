#ifndef PORT_BGCOLLISION_H
#define PORT_BGCOLLISION_H

#ifndef PLATFORM_N64

#include <ultra64.h>
#include "data.h"
#include "types.h"

void bgColPlayerHorizMove(struct coord *pos, RoomNum *rooms, struct coord *inout_delta,
                          f32 radius, f32 ymax, f32 ymin);

f32 bgColPlayerFindGround(struct coord *pos, RoomNum *rooms, f32 radius);

bool bgColPlayerHasCeiling(struct coord *pos, RoomNum *rooms, f32 radius, f32 abs_top_y);

#endif

#endif
