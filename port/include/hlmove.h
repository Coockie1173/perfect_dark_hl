#ifndef PORT_HLMOVE_H
#define PORT_HLMOVE_H

#ifndef PLATFORM_N64

#include "types.h"

#ifdef __cplusplus
extern "C"
{

#endif

struct HlMoveCfg {
    f32 maxspeed;
    f32 bhop_maxspeed;
    f32 friction;
    f32 stopspeed;
    f32 accelerate;
    f32 airaccelerate;
    f32 air_wishspd_cap;
    f32 jump_speed_utick;
    f32 jump_lift;
    s32 jump_grace_ticks;
};

extern struct HlMoveCfg g_HlMoveCfg;

void hlmoveCfgSetDefaults(void);
void hlmoveInit(void);
void hlmoveGetDelta(struct coord *out_delta);
void hlmoveHandleJump(void);
int hlmoveJumpHeld(void);
void hlmoveHandleCrouch(void);

#ifdef __cplusplus
}
#endif

#endif
#endif
