#include "odom.h"

#include <math.h>
#include <string.h>

#include "motion.h"

void motion_fk(const float v[3], float body_r_mm, float out[3])
{
    float vx = 0.0f, vy = 0.0f, sum = 0.0f;
    for (int i = 0; i < 3; i++) {
        float a = MOTION_WHEEL_ANGLE_DEG[i] * (float)M_PI / 180.0f;
        vx += v[i] * (-sinf(a));
        vy += v[i] * cosf(a);
        sum += v[i];
    }
    out[0] = vx * (2.0f / 3.0f);
    out[1] = vy * (2.0f / 3.0f);
    out[2] = (body_r_mm > 1e-3f) ? sum / (3.0f * body_r_mm) : 0.0f;   /* 防除零；1.0 是合法值(互逆自检用) */
}

void odom_step(odom_pose_t *p, const int32_t dcount[3], const odom_geom_t *g)
{
    if (g->counts_per_rev < 1.0f) {
        return;
    }
    float mm_per_count = 2.0f * (float)M_PI * g->wheel_r_mm / g->counts_per_rev;
    float d[3];
    for (int i = 0; i < 3; i++) {
        d[i] = (float)dcount[i] * mm_per_count;   /* 轮切向位移 mm */
    }
    float tw[3];
    motion_fk(d, g->body_r_mm, tw);   /* tw = {dx_body, dy_body, dth} */

    /* 中点航向积分：一步内航向变化很小（50Hz），中点法足够 */
    float th_mid = p->th_rad + tw[2] * 0.5f;
    float c = cosf(th_mid), s = sinf(th_mid);
    p->x_mm += tw[0] * c - tw[1] * s;
    p->y_mm += tw[0] * s + tw[1] * c;
    p->th_rad += tw[2];
}

void odom_reset(odom_pose_t *p)
{
    memset(p, 0, sizeof(*p));
}
