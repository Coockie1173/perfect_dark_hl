#ifndef PLATFORM_N64

#include <ultra64.h>
#include <math.h>
#include <SDL.h>

#include "constants.h"
#include "bss.h"
#include "game/bondmove.h"
#include "game/bondwalk.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "game/options.h"
#include "data.h"
#include "types.h"
#include "lib/joy.h"
#include "hlmove.h"

struct HlMoveCfg g_HlMoveCfg;

void hlmoveCfgSetDefaults(void)
{
    g_HlMoveCfg.maxspeed = 500.0f;
    g_HlMoveCfg.bhop_maxspeed = 3500.0f;
    g_HlMoveCfg.friction = 4.0f;
    g_HlMoveCfg.stopspeed = 50.0f;
    g_HlMoveCfg.accelerate = 20.0f;
    g_HlMoveCfg.airaccelerate = 30.0f;
    g_HlMoveCfg.air_wishspd_cap = 50.0f;
    g_HlMoveCfg.jump_speed_utick = 6.5f;
    g_HlMoveCfg.jump_lift = 6.0f;
    g_HlMoveCfg.jump_grace_ticks = 8;
}

static int s_justJumped = 0;
static float s_velX = 0.0f;
static float s_velZ = 0.0f;

static float frametime(void)
{
    return g_Vars.lvupdate60freal / 60.0f;
}

static float dot2(float ax, float az, float bx, float bz)
{
    return ax * bx + az * bz;
}

static float normalize2(float *x, float *z)
{
    float len = sqrtf((*x) * (*x) + (*z) * (*z));
    if (len > 1e-6f)
    {
        *x /= len;
        *z /= len;
    }
    return len;
}

static int isOnGround(void)
{
    if (s_justJumped > 0)
        return 0;

    float diff = g_Vars.currentplayer->vv_manground
                 - g_Vars.currentplayer->vv_ground;
    return diff <= 1.0f;
}

static s8 getContpad(void)
{
    return optionsGetContpadNum1(g_Vars.currentplayerstats->mpindex);
}

static void readMoveInput(float *fmove, float *smove)
{
    s8 contpad = getContpad();

    float sf = joyGetStickY(contpad) / 80.0f;
    float ss = joyGetStickX(contpad) / 80.0f;
    if (sf > 1.0f) sf = 1.0f;
    if (sf < -1.0f) sf = -1.0f;
    if (ss > 1.0f) ss = 1.0f;
    if (ss < -1.0f) ss = -1.0f;

    u32 btns = joyGetButtons(contpad, U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS);
    float kf = 0.0f, ks = 0.0f;
    if (btns & U_CBUTTONS) kf += 1.0f;
    if (btns & D_CBUTTONS) kf -= 1.0f;
    if (btns & L_CBUTTONS) ks -= 1.0f;
    if (btns & R_CBUTTONS) ks += 1.0f;

    *fmove = (fabsf(kf) >= fabsf(sf)) ? kf : sf;
    *smove = (fabsf(ks) >= fabsf(ss)) ? ks : ss;
}

static void applyGroundFriction(float dt)
{
    float speed = sqrtf(s_velX * s_velX + s_velZ * s_velZ);
    if (speed < 0.1f)
    {
        s_velX = s_velZ = 0.0f;
        return;
    }

    float control = (speed < g_HlMoveCfg.stopspeed) ? g_HlMoveCfg.stopspeed : speed;
    float drop = control * g_HlMoveCfg.friction * dt;
    float newspeed = speed - drop;
    if (newspeed < 0.0f) newspeed = 0.0f;

    float scale = newspeed / speed;
    s_velX *= scale;
    s_velZ *= scale;
}

static void applyAccelerate(float wdx, float wdz, float wishspeed,
                            float accel, float dt)
{
    float currentspeed = dot2(s_velX, s_velZ, wdx, wdz);
    float addspeed = wishspeed - currentspeed;
    if (addspeed <= 0.0f) return;

    float accelspeed = accel * dt * wishspeed;
    if (accelspeed > addspeed) accelspeed = addspeed;

    s_velX += wdx * accelspeed;
    s_velZ += wdz * accelspeed;
}

static void applyAirAccelerate(float wdx, float wdz, float wishspeed,
                               float accel, float dt)
{
    float wishspd_capped = (wishspeed > g_HlMoveCfg.air_wishspd_cap)
                               ? g_HlMoveCfg.air_wishspd_cap
                               : wishspeed;

    float currentspeed = dot2(s_velX, s_velZ, wdx, wdz);
    float addspeed = wishspd_capped - currentspeed;
    if (addspeed <= 0.0f) return;

    float accelspeed = accel * wishspeed * dt;
    if (accelspeed > addspeed) accelspeed = addspeed;

    s_velX += wdx * accelspeed;
    s_velZ += wdz * accelspeed;
}

void hlmoveInit(void)
{
    s_velX = 0.0f;
    s_velZ = 0.0f;
    s_justJumped = 0;
}

void hlmoveGetDelta(struct coord *out_delta)
{
    out_delta->x = 0.0f;
    out_delta->y = 0.0f;
    out_delta->z = 0.0f;

    float dt = frametime();
    if (dt <= 0.0f) return;

    float fmove, smove;
    readMoveInput(&fmove, &smove);

    float fw_x = g_Vars.currentplayer->bond2.unk00.f[0];
    float fw_z = g_Vars.currentplayer->bond2.unk00.f[2];
    float rt_x = -fw_z;
    float rt_z = fw_x;

    float wvx = fw_x * fmove + rt_x * smove;
    float wvz = fw_z * fmove + rt_z * smove;

    float wv_len = sqrtf(wvx * wvx + wvz * wvz);
    float wishspeed = wv_len * g_HlMoveCfg.maxspeed;
    if (wishspeed > g_HlMoveCfg.maxspeed) wishspeed = g_HlMoveCfg.maxspeed;

    float wdx = wvx, wdz = wvz;
    normalize2(&wdx, &wdz);

    int onground = isOnGround();

    if (onground)
    {
        applyGroundFriction(dt);
        applyAccelerate(wdx, wdz, wishspeed, g_HlMoveCfg.accelerate, dt);
    } else
    {
        float air_wishspeed = wv_len * g_HlMoveCfg.bhop_maxspeed;
        if (air_wishspeed > g_HlMoveCfg.bhop_maxspeed) air_wishspeed = g_HlMoveCfg.bhop_maxspeed;
        applyAirAccelerate(wdx, wdz, air_wishspeed, g_HlMoveCfg.airaccelerate, dt);
    }

    out_delta->x = s_velX * dt;
    out_delta->z = s_velZ * dt;

    float speed2d = sqrtf(s_velX * s_velX + s_velZ * s_velZ);
    float norm = (g_HlMoveCfg.maxspeed > 0.0f) ? (1.0f / g_HlMoveCfg.maxspeed) : 1.0f;

    g_Vars.currentplayer->speedforwards = dot2(s_velX, s_velZ, fw_x, fw_z) * norm;
    g_Vars.currentplayer->speedsideways = dot2(s_velX, s_velZ, rt_x, rt_z) * norm;
    g_Vars.currentplayer->speedgo = speed2d * norm;
}

int hlmoveJumpHeld(void)
{
    s8 contpad = getContpad();

    if (joyGetButtons(contpad, A_BUTTON | BUTTON_HALF_CROUCH))
        return 1;

    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (keys && keys[SDL_SCANCODE_SPACE])
        return 1;

    return 0;
}

void hlmoveHandleJump(void)
{
    if (s_justJumped > 0)
        s_justJumped--;

    if (hlmoveJumpHeld() && isOnGround())
    {
        g_Vars.currentplayer->bdeltapos.y = g_HlMoveCfg.jump_speed_utick;
        g_Vars.currentplayer->vv_manground = g_Vars.currentplayer->vv_ground
                                             + g_HlMoveCfg.jump_lift;
        s_justJumped = g_HlMoveCfg.jump_grace_ticks;
    }
}

void hlmoveHandleCrouch(void)
{
    s8 contpad = getContpad();
    u32 btns = joyGetButtons(contpad, BUTTON_FULL_CROUCH);

    s32 targetpos;
    f32 targetoffset;

    if (btns & BUTTON_FULL_CROUCH)
    {
        targetpos = CROUCHPOS_SQUAT;
        targetoffset = -90.0f;
    } else
    {
        targetpos = CROUCHPOS_STAND;
        targetoffset = 0.0f;
    }

    if (targetpos > g_Vars.currentplayer->crouchpos && !bwalkCanUncrouch())
        return;

    g_Vars.currentplayer->crouchpos = targetpos;
    g_Vars.currentplayer->crouchoffset = targetoffset;
    g_Vars.currentplayer->crouchspeed = 0.0f;

    bwalkUpdateCrouchOffsetReal();

    g_Vars.currentplayer->guncloseroffset = targetoffset / -90.0f;
}

#endif
