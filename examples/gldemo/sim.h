#ifndef SIM_H_INCLUDED
#define SIM_H_INCLUDED

#include <memory.h>
#include <math.h>

#include "../../include/GL/gl.h"

#include "mymath.h"

#define SIM_MAX_POINTS (100)
#define MAX_SPRINGS (20)

struct Spring {
    int from;
    int to;
    float length;
};

struct SimConfig
{
    float root[3];
    bool root_is_static;
};

struct Simulation {
    float x[SIM_MAX_POINTS*3];
    float oldx[SIM_MAX_POINTS*3];
    int num_points;

    struct Spring  springs[MAX_SPRINGS];

    bool spring_visible[MAX_SPRINGS];

    int num_springs;
    int num_updates_done;

    struct SimConfig config;

    struct {
        int u_inds[2]; // from-to points that make up the u vector
        int v_inds[2];
        int attach_top_index; // where the gem is attached
        int attach_tri_inds[3]; // gem centroid points

        float u[3];
        float v[3];
        float n[3];

        float origin[3];
        float ulength;
    } pose;

    struct {
        bool show_wires;
    } debug;
};

void sim_init(struct Simulation* s, struct SimConfig config)
{
    memset(s, 0, sizeof(*s));
    s->config = config;

    memcpy(s->oldx, s->x, sizeof(s->x));

    for (int i=0;i<MAX_SPRINGS;i++) {
        s->spring_visible[i] = false;
    }

    const float rope_segment = 0.5f;
    const float side = 2.0f;

    int h=-1;
    for (int i=1;i<4;i++) {
        s->spring_visible[s->num_springs] = true;
        s->springs[s->num_springs++] = (struct Spring){i-1, i, rope_segment};
        h = i;
    }

    s->springs[s->num_springs++] = (struct Spring){h, h+1, side};
    s->springs[s->num_springs++] = (struct Spring){h, h+2, side};
    s->springs[s->num_springs++] = (struct Spring){h, h+3, side};
    s->springs[s->num_springs++] = (struct Spring){h+1, h+2, side};
    s->springs[s->num_springs++] = (struct Spring){h+2, h+3, side};
    s->springs[s->num_springs++] = (struct Spring){h+3, h+1, side};

    s->springs[s->num_springs++] = (struct Spring){h+1, h+4, side};
    s->springs[s->num_springs++] = (struct Spring){h+2, h+4, side};
    s->springs[s->num_springs++] = (struct Spring){h+3, h+4, side};

    s->springs[s->num_springs++] = (struct Spring){h+1, h+5, side};
    s->springs[s->num_springs++] = (struct Spring){h+2, h+5, side};
    s->springs[s->num_springs++] = (struct Spring){h+3, h+5, side};
    s->springs[s->num_springs++] = (struct Spring){h, h+5, side*1.6}; // not the real height based on geometry but seems stable


    s->pose.u_inds[0] = h+1;
    s->pose.u_inds[1] = h+2;
    s->pose.v_inds[0] = h+1;
    s->pose.v_inds[1] = h+3;

    s->pose.attach_top_index = h;
    s->pose.attach_tri_inds[0] = h+1;
    s->pose.attach_tri_inds[1] = h+2;
    s->pose.attach_tri_inds[2] = h+3;

    s->num_points = h+6;

    for (int i=0;i<s->num_points-1;i++) {
        int idx = i*3;
        s->x[idx + 0] = i*0.5f;
        s->x[idx + 1] = i*0.1f;
        s->x[idx + 2] = i*0.15f;
    }

    s->debug.show_wires = false;
}

void sim_update(struct Simulation* s)
{
    const bool verbose = false;
    const int num_iters = 3;
    const float gravity = -0.02f;
    const float velocity_damping = 1.0f;
    const float constraint_damping = 0.5f;

    // Verlet integration

    if (s->config.root_is_static) {
        s->x[0] = s->config.root[0];
        s->x[1] = s->config.root[1];
        s->x[2] = s->config.root[2];
    }

    for (int i = 0; i < s->num_points; i++) {
        int idx = i * 3;
        float* pos = &s->x[idx];
        float* old = &s->oldx[idx];

        float acc[3] = {0.0f, gravity, 0.0f};
        float temp[3];

        temp[0] = pos[0];
        temp[1] = pos[1];
        temp[2] = pos[2];

        pos[0] += velocity_damping * (pos[0] - old[0]) + acc[0];
        pos[1] += velocity_damping * (pos[1] - old[1]) + acc[1];
        pos[2] += velocity_damping * (pos[2] - old[2]) + acc[2];

        old[0] = temp[0];
        old[1] = temp[1];
        old[2] = temp[2];
    }

    // Satisfy constraints

    for (int iter = 0; iter < num_iters; iter++) {
        for (int i=0;i<s->num_points;i++) {
            float* p = &s->x[i*3];
            if (p[1] < 0.f) {
                float depth = -p[1];
                p[1] = 0.0f;

                float* oldp = &s->oldx[i*3];
                float tvel[3]; // tangential velocity after projection
                vec3_sub(p, oldp, tvel);

                const float friction = 0.19f;
                float shorten = depth > 0.0f ? depth * friction : 0.0f;
                float length = vec3_length(tvel);
                if (verbose) debugf("[%d] depth=%f, friction=%f, length=%f, shorten=%f", i, depth, friction, length, shorten);

                if (length <= shorten) {
                    // zero velocity
                    oldp[0] = p[0];
                    oldp[1] = p[1];
                    oldp[2] = p[2];
                    if (verbose) debugf(" -> zero'd\n");
                } else {
                    if (verbose) debugf(" -> apply friction\n");
                    // scale velocity vector to be of length "friction"
                    // but we don't have any explicit velocity, just two points
                    // so we scale the velocity by moving the new point towards the old one
                    vec3_normalize_(tvel);
                    vec3_scale(shorten, tvel);
                    vec3_add(oldp, tvel, oldp);
                }
            }
        }

        for (int i=0;i<s->num_springs;i++) {
            struct Spring spring = s->springs[i];
            float* pa = &s->x[spring.from*3];
            float* pb = &s->x[spring.to*3];

            float delta[3] = {
                pb[0] - pa[0],
                pb[1] - pa[1],
                pb[2] - pa[2]
            };

            float length = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
            float diff = length == 0.0f ? 0.0f : (length - spring.length) / length;

            float scale = constraint_damping * 0.5f * diff; 
            pa[0] += scale * delta[0];
            pa[1] += scale * delta[1];
            pa[2] += scale * delta[2];
            pb[0] -= scale * delta[0];
            pb[1] -= scale * delta[1];
            pb[2] -= scale * delta[2];
        }
    }

    if (s->config.root_is_static) {
        s->x[0] = s->config.root[0];
        s->x[1] = s->config.root[1];
        s->x[2] = s->config.root[2];
    }

    // Update object attachment

    // Compute the average position of the "attachment triangle" set in simulation pose struct.
    const float* a1 = &s->x[3 * s->pose.attach_tri_inds[0]];
    const float* a2 = &s->x[3 * s->pose.attach_tri_inds[1]];
    const float* a3 = &s->x[3 * s->pose.attach_tri_inds[2]];

    float centroid[3];
    vec3_copy(a1, centroid);
    vec3_add(a1, a2, centroid);
    vec3_add(centroid, a3, centroid);
    vec3_scale(0.33333f, centroid);

    s->pose.origin[0] = centroid[0];
    s->pose.origin[1] = centroid[1];
    s->pose.origin[2] = centroid[2];

    const float* ufrom   = &s->x[3 * s->pose.u_inds[0]];
    const float* uto     = &s->x[3 * s->pose.u_inds[1]];
    //const float* vfrom   = &s->x[3 * s->pose.v_inds[0]];
    //const float* vto     = &s->x[3 * s->pose.v_inds[1]];
    const float* nfrom   = centroid;
    const float* nto     = &s->x[3 * s->pose.attach_top_index];

    float* uvec = s->pose.u;
    float* vvec = s->pose.v;
    float* nvec = s->pose.n;

    vec3_sub(uto, ufrom, uvec);
    //vec3_sub(vto, vfrom, vvec);
    vec3_sub(nto, nfrom, nvec);

    s->pose.ulength = vec3_length(uvec);

    vec3_normalize_(uvec);
    vec3_normalize_(nvec);

    //vec3_normalize_(vvec);
     vec3_cross(uvec, nvec, vvec);

    // Debug movement

    s->num_updates_done++;

    if (false) {
        if (s->num_updates_done % 75 == 0) {
            s->config.root[0] = ((float)rand() / RAND_MAX) * 6.0f - 3.0f;
            if (verbose) debugf("root[0] = %f\n", s->config.root[0]);
        }

        if ((s->num_updates_done / 20) % 10 < 3) {
            for (int idx = 0; idx < s->num_points; idx += 7) {
                s->x[idx * 3 + 0] = s->x[idx * 3 + 0] * 0.5f;
                s->x[idx * 3 + 1] = s->x[idx * 3 + 1] * 0.5f + 0.5f * 5.0f;
                s->x[idx * 3 + 2] = s->x[idx * 3 + 2] * 0.5f;
            }
            // s->oldx[idx*3+0] = s->x[idx*3+0];
            // s->oldx[idx*3+1] = s->x[idx*3+1];
            // s->oldx[idx*3+2] = s->x[idx*3+2];
        }

        for (int i = 0; i < s->num_points; i++) {
            // float* pos = &s->x[i*3];
            // debugf("[%d] (%f, %f, %f)\n", i,
            //     pos[0], pos[1], pos[2]
            // );
        }
    }
}

#endif