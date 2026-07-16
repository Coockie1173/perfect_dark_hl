/*
 * hlmove.c - Half-Life / Quake movement physics for Perfect Dark PC port
 *
 * Physics ported from Valve's pm_shared.c (Half-Life 1).
 *
 * Unit system
 * -----------
 *   All velocities are stored in world-units per second (u/s).
 *   Frame delta = velocity × dt, where dt = lvupdate60freal / 60  (seconds).
 *   bdeltapos.y (vertical) remains in PD's native u/tick convention.
 *
 *   PD gravity: 0.277777 u/tick² × (60 tick/s)² = 1000 u/s²
 *   Hard-land threshold: -13.333 u/tick = -800 u/s
 *
 * HL1 reference constants (all in u/s system)
 * --------------------------------------------
 *   sv_maxspeed      250 u/s
 *   sv_friction        4.0  (coefficient, per second)
 *   sv_stopspeed     100 u/s
 *   sv_accelerate     10.0
 *   sv_airaccelerate  10.0
 *   air wishspeed cap 30 u/s  (the bhop / strafe-jump mechanic lives here)
 *
 * Jump speed derivation
 * ---------------------
 *   target height ≈ 45 u, PD gravity = 1000 u/s²
 *   v0 = sqrt(2 × 1000 × 45) = 300 u/s = 5.0 u/tick @ 60 fps
 */

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
#include "data.h"
#include "types.h"
#include "hlmove.h"

#define HL_MAXSPEED          500.0f   /* u/s – ground max speed             */
#define HL_FRICTION            4.0f   /* ground friction coefficient         */
#define HL_STOPSPEED         50.0f   /* u/s – full-friction threshold       */
#define HL_ACCELERATE         20.0f   /* ground acceleration multiplier      */
#define HL_AIRACCELERATE      30.0f   /* air acceleration multiplier         */


#define HL_AIR_WISHSPD_CAP   50.0f


#define HL_JUMP_SPEED_UTICK   5.0f


#define HL_JUMP_LIFT          3.0


/* Horizontal velocity in world-units per second */
static float s_velX = 0.0f;
static float s_velZ = 0.0f;

/* Jump button state from previous tick (unused currently, kept for reference) */
static int s_prevJumpHeld = 0;

/* Elapsed time in seconds for this tick */
static float frametime(void)
{
    return g_Vars.lvupdate60freal / 60.0f;
}

/* 2-D dot product */
static float dot2(float ax, float az, float bx, float bz)
{
    return ax * bx + az * bz;
}

/* Normalise a 2-D vector; returns original length */
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

//TODO: FIX
static int isOnGround(void)
{
    float diff = g_Vars.currentplayer->vv_manground
                 - g_Vars.currentplayer->vv_ground;
    return diff <= 2.0f && g_Vars.currentplayer->bdeltapos.y <= 0.001f;
}

static void readMoveInput(float *fmove, float *smove)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    float kf = 0.0f, ks = 0.0f;

    if (keys)
    {
        //TODO: move to the in built system
        if (keys[SDL_SCANCODE_W]) kf += 1.0f;
        if (keys[SDL_SCANCODE_S]) kf -= 1.0f;
        if (keys[SDL_SCANCODE_D]) ks += 1.0f;
        if (keys[SDL_SCANCODE_A]) ks -= 1.0f;
    }

    float sf = 0.0f, ss = 0.0f;
    SDL_GameController *gc = SDL_GameControllerOpen(0);
    if (gc)
    {
        const float DEAD = 0.12f;
        float lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        float ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
        SDL_GameControllerClose(gc);
        if (lx < -DEAD || lx > DEAD) ss = lx;
        if (ly < -DEAD || ly > DEAD) sf = -ly; /* SDL Y is inverted */
    }

    /* Prefer whichever source has larger magnitude on each axis */
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

    float control = (speed < HL_STOPSPEED) ? HL_STOPSPEED : speed;
    float drop = control * HL_FRICTION * dt;
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
    float wishspd_capped = (wishspeed > HL_AIR_WISHSPD_CAP)
                               ? HL_AIR_WISHSPD_CAP
                               : wishspeed;

    float currentspeed = dot2(s_velX, s_velZ, wdx, wdz);
    float addspeed = wishspd_capped - currentspeed;
    if (addspeed <= 0.0f) return;

    /* Use full wishspeed for the rate so air-strafing feels snappy */
    float accelspeed = accel * wishspeed * dt;
    if (accelspeed > addspeed) accelspeed = addspeed;

    s_velX += wdx * accelspeed;
    s_velZ += wdz * accelspeed;
}

void hlmoveInit(void)
{
    s_velX = 0.0f;
    s_velZ = 0.0f;
    s_prevJumpHeld = 0;
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
    float wishspeed = wv_len * HL_MAXSPEED;
    if (wishspeed > HL_MAXSPEED) wishspeed = HL_MAXSPEED;

    float wdx = wvx, wdz = wvz;
    normalize2(&wdx, &wdz);

    int onground = isOnGround();

    if (onground)
    {
        applyGroundFriction(dt);
        applyAccelerate(wdx, wdz, wishspeed, HL_ACCELERATE, dt);
    } else
    {
        applyAirAccelerate(wdx, wdz, wishspeed, HL_AIRACCELERATE, dt);
    }

    out_delta->x = s_velX * dt;
    out_delta->z = s_velZ * dt;

    float speed2d = sqrtf(s_velX * s_velX + s_velZ * s_velZ);
    float norm = (HL_MAXSPEED > 0.0f) ? (1.0f / HL_MAXSPEED) : 1.0f;

    g_Vars.currentplayer->speedforwards = dot2(s_velX, s_velZ, fw_x, fw_z) * norm;
    g_Vars.currentplayer->speedsideways = dot2(s_velX, s_velZ, rt_x, rt_z) * norm;
    g_Vars.currentplayer->speedgo = speed2d * norm;
}

//TODO: FEED INTO EXISTING MOVEMENT SYSTEM
int hlmoveJumpHeld(void)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (!keys) return 0;
    return keys[SDL_SCANCODE_SPACE] != 0;
}

void hlmoveHandleJump(void)
{
    int jumpHeld = hlmoveJumpHeld();

    if (jumpHeld && isOnGround())
    {
        g_Vars.currentplayer->bdeltapos.y = HL_JUMP_SPEED_UTICK;
        g_Vars.currentplayer->vv_manground = g_Vars.currentplayer->vv_ground + HL_JUMP_LIFT;
    }

    s_prevJumpHeld = jumpHeld;
}

#endif /* !PLATFORM_N64 */
