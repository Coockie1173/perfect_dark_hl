#ifndef PLATFORM_N64

#include <ultra64.h>
#include "constants.h"
#include "bss.h"
#include "game/bg.h"
#include "data.h"
#include "types.h"
#include "../include/bgcollision.h"

#define BGCOL_WALL_NY_MAX   0.707f
#define BGCOL_FLOOR_NY_MIN  0.5f
#define BGCOL_CEIL_NY_MAX   (-0.5f)
#define BGCOL_MAX_ITERS     3
#define BGCOL_NUM_AZI       8
#define BGCOL_NUM_HEIGHTS   3
#define BGCOL_CAST_FAR      50000.0f

static const float s_azi_cos[BGCOL_NUM_AZI] = {
    1.0f, 0.707107f, 0.0f, -0.707107f,
    -1.0f, -0.707107f, 0.0f, 0.707107f
};
static const float s_azi_sin[BGCOL_NUM_AZI] = {
    0.0f, 0.707107f, 1.0f, 0.707107f,
    0.0f, -0.707107f, -1.0f, -0.707107f
};

static bool bgcol_test_rooms(struct coord *from, struct coord *to, RoomNum *rooms,
                             struct hitthing *outhit, float *outsqdist)
{
    RoomNum *rptr;
    struct hitthing hit;
    bool found = false;
    float best = 1e30f;

    for (rptr = rooms; *rptr != -1; rptr++)
    {
        if (bgTestHitInRoom(from, to, *rptr, &hit))
        {
            float ex = hit.pos.x - from->x;
            float ey = hit.pos.y - from->y;
            float ez = hit.pos.z - from->z;
            float sq = ex * ex + ey * ey + ez * ez;
            if (sq < best)
            {
                best = sq;
                *outhit = hit;
                found = true;
            }
        }
    }

    if (outsqdist)
    {
        *outsqdist = best;
    }
    return found;
}

void bgColPlayerHorizMove(struct coord *pos, RoomNum *rooms, struct coord *inout_delta,
                          f32 radius, f32 ymax, f32 ymin)
{
    int iter;
    float dx = inout_delta->x;
    float dz = inout_delta->z;

    float heights[BGCOL_NUM_HEIGHTS];
    heights[0] = ymin + (ymax - ymin) * 0.15f;
    heights[1] = ymin + (ymax - ymin) * 0.5f;
    heights[2] = ymin + (ymax - ymin) * 0.85f;

    for (iter = 0; iter < BGCOL_MAX_ITERS; iter++)
    {
        float movlen2 = dx * dx + dz * dz;
        if (movlen2 < 0.0001f)
        {
            break;
        }

        float wall_nx = 0.0f;
        float wall_nz = 0.0f;
        bool blocked = false;
        float best_sqdist = 1e30f;
        int hi;
        int ai;

        for (hi = 0; hi < BGCOL_NUM_HEIGHTS; hi++)
        {
            float y = heights[hi];
            struct coord from;
            struct coord to;
            struct hitthing hit;
            float sq;

            from.x = pos->x;
            from.y = y;
            from.z = pos->z;
            to.x = pos->x + dx;
            to.y = y;
            to.z = pos->z + dz;

            if (bgcol_test_rooms(&from, &to, rooms, &hit, &sq))
            {
                float ny_abs = hit.unk0c.y < 0.0f ? -hit.unk0c.y : hit.unk0c.y;
                if (ny_abs < BGCOL_WALL_NY_MAX && sq < best_sqdist)
                {
                    float hn = hit.unk0c.x * hit.unk0c.x + hit.unk0c.z * hit.unk0c.z;
                    if (hn > 0.0001f)
                    {
                        float inv_n;
                        hn = (float) sqrtf(hn);
                        inv_n = 1.0f / hn;
                        best_sqdist = sq;
                        wall_nx = hit.unk0c.x * inv_n;
                        wall_nz = hit.unk0c.z * inv_n;
                        blocked = true;
                    }
                }
            }

            for (ai = 0; ai < BGCOL_NUM_AZI; ai++)
            {
                float ox = s_azi_cos[ai] * radius;
                float oz = s_azi_sin[ai] * radius;
                if (ox * dx + oz * dz < 0.0f)
                {
                    continue;
                }
                from.x = pos->x + ox;
                from.y = y;
                from.z = pos->z + oz;
                to.x = from.x + dx;
                to.y = y;
                to.z = from.z + dz;
                if (bgcol_test_rooms(&from, &to, rooms, &hit, &sq))
                {
                    float ny_abs = hit.unk0c.y < 0.0f ? -hit.unk0c.y : hit.unk0c.y;
                    if (ny_abs < BGCOL_WALL_NY_MAX && sq < best_sqdist)
                    {
                        float hn = hit.unk0c.x * hit.unk0c.x + hit.unk0c.z * hit.unk0c.z;
                        if (hn > 0.0001f)
                        {
                            float inv_n;
                            hn = (float) sqrtf(hn);
                            inv_n = 1.0f / hn;
                            best_sqdist = sq;
                            wall_nx = hit.unk0c.x * inv_n;
                            wall_nz = hit.unk0c.z * inv_n;
                            blocked = true;
                        }
                    }
                }
            }
        }

        if (!blocked)
        {
            break;
        }

        {
            float dot = dx * wall_nx + dz * wall_nz;
            if (dot < 0.0f)
            {
                dx -= dot * wall_nx;
                dz -= dot * wall_nz;
            } else
            {
                break;
            }
        }
    }

    inout_delta->x = dx;
    inout_delta->z = dz;
}

f32 bgColPlayerFindGround(struct coord *pos, RoomNum *rooms, f32 radius)
{
    float best_y = -4294967296.0f;
    int i;
    float half = radius * 0.5f;

    const float offx[5] = {0.0f, half, -half, 0.0f, 0.0f};
    const float offz[5] = {0.0f, 0.0f, 0.0f, half, -half};

    for (i = 0; i < 5; i++)
    {
        struct coord from;
        struct coord to;
        struct hitthing hit;
        from.x = pos->x + offx[i];
        from.y = pos->y + 10.0f;
        from.z = pos->z + offz[i];
        to.x = from.x;
        to.y = pos->y - BGCOL_CAST_FAR;
        to.z = from.z;
        if (bgcol_test_rooms(&from, &to, rooms, &hit, NULL))
        {
            if (hit.unk0c.y > BGCOL_FLOOR_NY_MIN && hit.pos.y > best_y)
            {
                best_y = hit.pos.y;
            }
        }
    }

    return best_y;
}

bool bgColPlayerHasCeiling(struct coord *pos, RoomNum *rooms, f32 radius, f32 abs_top_y)
{
    int i;
    float half = radius * 0.5f;

    const float offx[5] = {0.0f, half, -half, 0.0f, 0.0f};
    const float offz[5] = {0.0f, 0.0f, 0.0f, half, -half};

    for (i = 0; i < 5; i++)
    {
        struct coord from;
        struct coord to;
        struct hitthing hit;
        from.x = pos->x + offx[i];
        from.y = pos->y + 1.0f;
        from.z = pos->z + offz[i];
        to.x = from.x;
        to.y = abs_top_y + 1.0f;
        to.z = from.z;
        if (bgcol_test_rooms(&from, &to, rooms, &hit, NULL))
        {
            if (hit.unk0c.y < BGCOL_CEIL_NY_MAX)
            {
                return true;
            }
        }
    }

    return false;
}

#endif
