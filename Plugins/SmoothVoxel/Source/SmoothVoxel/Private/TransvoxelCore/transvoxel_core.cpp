/* SPDX-License-Identifier: 0BSD */
#include "transvoxel.h"
#include "transvoxel_tables.h"

static const TvVec3 tv_regular_positions[TV_REGULAR_SAMPLE_COUNT] = {
    {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f},
    {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}
};

static const TvVec3 tv_transition_positions[TV_TRANSITION_SAMPLE_COUNT] = {
    {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {2.0f, 1.0f, 0.0f},
    {0.0f, 2.0f, 0.0f}, {1.0f, 2.0f, 0.0f}, {2.0f, 2.0f, 0.0f},
    {0.0f, 0.0f, 1.0f}, {2.0f, 0.0f, 1.0f},
    {0.0f, 2.0f, 1.0f}, {2.0f, 2.0f, 1.0f},
    {1.0f, 1.0f, 0.5f}
};

static TvTransitionBuilderFn tv_transition_backend_callback = 0;

TvVec3 tv_vec3(float x, float y, float z) {
    TvVec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

static TvVec3 tv_add(TvVec3 a, TvVec3 b) {
    TvVec3 out;
    out.x = a.x + b.x;
    out.y = a.y + b.y;
    out.z = a.z + b.z;
    return out;
}

static TvVec3 tv_mul(TvVec3 a, TvVec3 b) {
    TvVec3 out;
    out.x = a.x * b.x;
    out.y = a.y * b.y;
    out.z = a.z * b.z;
    return out;
}

static TvVec3 tv_lerp(TvVec3 a, TvVec3 b, float t) {
    TvVec3 out;
    out.x = a.x + (b.x - a.x) * t;
    out.y = a.y + (b.y - a.y) * t;
    out.z = a.z + (b.z - a.z) * t;
    return out;
}

static float tv_absf(float v) {
    return v < 0.0f ? -v : v;
}

static float tv_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static TvVec3 tv_world_pos(TvVec3 origin, TvVec3 scale, TvVec3 local) {
    return tv_add(origin, tv_mul(local, scale));
}

static TvVec3 tv_interp_position(TvVec3 a, TvVec3 b, float va, float vb, float iso_level) {
    float da = va - iso_level;
    float db = vb - iso_level;
    float denom = tv_absf(da) + tv_absf(db);
    float t = 0.5f;
    if (denom > 0.000001f) {
        t = tv_absf(da) / denom;
    }
    return tv_lerp(a, b, tv_clampf(t, 0.0f, 1.0f));
}

TvVec3 tv_regular_sample_position(int sample_id) {
    if (sample_id < 0 || sample_id >= TV_REGULAR_SAMPLE_COUNT) {
        return tv_vec3(0.0f, 0.0f, 0.0f);
    }
    return tv_regular_positions[sample_id];
}

TvVec3 tv_transition_sample_position(int sample_id) {
    if (sample_id < 0 || sample_id >= TV_TRANSITION_SAMPLE_COUNT) {
        return tv_vec3(0.0f, 0.0f, 0.0f);
    }
    return tv_transition_positions[sample_id];
}

int tv_regular_case_index(const float samples[TV_REGULAR_SAMPLE_COUNT], float iso_level) {
    int index = 0;
    int i;
    if (!samples) return 0;
    for (i = 0; i < TV_REGULAR_SAMPLE_COUNT; ++i) {
        if (samples[i] < iso_level) {
            index |= (1 << i);
        }
    }
    return index;
}

int tv_transition_case_index(const float samples[TV_TRANSITION_SAMPLE_COUNT], float iso_level) {
    int index = 0;
    int i;
    if (!samples) return 0;
    for (i = 0; i < TV_TRANSITION_HIGH_SAMPLE_COUNT; ++i) {
        if (samples[i] < iso_level) {
            index |= (1 << i);
        }
    }
    return index;
}

void tv_transition_fill_derived_samples(float samples[TV_TRANSITION_SAMPLE_COUNT]) {
    if (!samples) return;
    samples[9] = samples[0];
    samples[10] = samples[2];
    samples[11] = samples[6];
    samples[12] = samples[8];
    samples[13] = samples[4];
}

static TvBuildInfo tv_make_info(int result, int case_index, int vertex_count, int triangle_count) {
    TvBuildInfo info;
    info.result = result;
    info.case_index = case_index;
    info.vertex_count = vertex_count;
    info.triangle_count = triangle_count;
    return info;
}

int tv_set_transition_backend_callback(TvTransitionBuilderFn builder) {
    tv_transition_backend_callback = builder;
    return TV_OK;
}

TvTransitionBuilderFn tv_get_transition_backend_callback(void) {
    return tv_transition_backend_callback;
}

void tv_reset_transition_backend_callback(void) {
    tv_transition_backend_callback = 0;
}

int tv_transition_backend_is_custom(void) {
    return tv_transition_backend_callback != 0;
}

TvBuildInfo tv_build_regular_cell(
    const float sample_values[TV_REGULAR_SAMPLE_COUNT],
    float iso_level,
    TvVec3 origin,
    TvVec3 scale,
    TvVec3 *out_vertices,
    int max_vertices,
    TvTriangle *out_triangles,
    int max_triangles) {

    int case_index;
    uint16_t class_index;
    TVCClassData class_data;
    int i;

    if (!sample_values || !out_vertices || !out_triangles) {
        return tv_make_info(TV_ERROR_NULL, 0, 0, 0);
    }

    case_index = tv_regular_case_index(sample_values, iso_level);
    if (case_index < 0 || case_index >= TVC_REGULAR_CASE_COUNT) {
        return tv_make_info(TV_ERROR_BAD_CASE, case_index, 0, 0);
    }
    class_index = tvc_regular_case_class[case_index];
    class_data = tvc_regular_class_data[class_index];

    if (max_vertices < (int)class_data.vertex_count) {
        return tv_make_info(TV_ERROR_SMALL_VERTEX_BUFFER, case_index, (int)class_data.vertex_count, (int)class_data.triangle_count);
    }
    if (max_triangles < (int)class_data.triangle_count) {
        return tv_make_info(TV_ERROR_SMALL_TRIANGLE_BUFFER, case_index, (int)class_data.vertex_count, (int)class_data.triangle_count);
    }

    for (i = 0; i < (int)class_data.vertex_count; ++i) {
        TVCVertexRef ref = tvc_regular_vertex_refs[class_data.vertex_offset + i];
        int a = (int)ref.sample_a;
        int b = (int)ref.sample_b;
        TvVec3 pa = tv_world_pos(origin, scale, tv_regular_positions[a]);
        TvVec3 pb = tv_world_pos(origin, scale, tv_regular_positions[b]);
        out_vertices[i] = tv_interp_position(pa, pb, sample_values[a], sample_values[b], iso_level);
    }

    for (i = 0; i < (int)class_data.triangle_count; ++i) {
        TVCTriangle tri = tvc_regular_triangles[class_data.triangle_offset + i];
        out_triangles[i].a = tri.v0;
        out_triangles[i].b = tri.v1;
        out_triangles[i].c = tri.v2;
    }

    return tv_make_info(TV_OK, case_index, (int)class_data.vertex_count, (int)class_data.triangle_count);
}

TvBuildInfo tv_build_transition_cell(
    const float sample_values[TV_TRANSITION_SAMPLE_COUNT],
    float iso_level,
    TvVec3 origin,
    TvVec3 scale,
    TvVec3 *out_vertices,
    int max_vertices,
    TvTriangle *out_triangles,
    int max_triangles) {

    int case_index;
    uint16_t class_index;
    TVCClassData class_data;
    int i;

    if (tv_transition_backend_callback) {
        return tv_transition_backend_callback(
            sample_values,
            iso_level,
            origin,
            scale,
            out_vertices,
            max_vertices,
            out_triangles,
            max_triangles);
    }

    if (!sample_values || !out_vertices || !out_triangles) {
        return tv_make_info(TV_ERROR_NULL, 0, 0, 0);
    }

    case_index = tv_transition_case_index(sample_values, iso_level);
    if (case_index < 0 || case_index >= TVC_TRANSITION_CASE_COUNT) {
        return tv_make_info(TV_ERROR_BAD_CASE, case_index, 0, 0);
    }
    class_index = tvc_transition_case_class[case_index];
    class_data = tvc_transition_class_data[class_index];

    if (max_vertices < (int)class_data.vertex_count) {
        return tv_make_info(TV_ERROR_SMALL_VERTEX_BUFFER, case_index, (int)class_data.vertex_count, (int)class_data.triangle_count);
    }
    if (max_triangles < (int)class_data.triangle_count) {
        return tv_make_info(TV_ERROR_SMALL_TRIANGLE_BUFFER, case_index, (int)class_data.vertex_count, (int)class_data.triangle_count);
    }

    for (i = 0; i < (int)class_data.vertex_count; ++i) {
        TVCVertexRef ref = tvc_transition_vertex_refs[class_data.vertex_offset + i];
        int a = (int)ref.sample_a;
        int b = (int)ref.sample_b;
        TvVec3 pa = tv_world_pos(origin, scale, tv_transition_positions[a]);
        TvVec3 pb = tv_world_pos(origin, scale, tv_transition_positions[b]);
        out_vertices[i] = tv_interp_position(pa, pb, sample_values[a], sample_values[b], iso_level);
    }

    for (i = 0; i < (int)class_data.triangle_count; ++i) {
        TVCTriangle tri = tvc_transition_triangles[class_data.triangle_offset + i];
        out_triangles[i].a = tri.v0;
        out_triangles[i].b = tri.v1;
        out_triangles[i].c = tri.v2;
    }

    return tv_make_info(TV_OK, case_index, (int)class_data.vertex_count, (int)class_data.triangle_count);
}
