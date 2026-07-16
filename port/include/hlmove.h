#ifndef PORT_HLMOVE_H
#define PORT_HLMOVE_H

#ifndef PLATFORM_N64

#include "types.h"

#ifdef __cplusplus
extern "C"
{

#endif

void hlmoveInit(void);

void hlmoveGetDelta(struct coord *out_delta);

void hlmoveHandleJump(void);

int hlmoveJumpHeld(void);

#ifdef __cplusplus
}
#endif

#endif /* !PLATFORM_N64 */
#endif /* PORT_HLMOVE_H */
