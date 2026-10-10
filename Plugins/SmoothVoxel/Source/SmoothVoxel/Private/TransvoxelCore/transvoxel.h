/* SPDX-License-Identifier: 0BSD
 * Public C API for the independent 0BSD Transvoxel-style core.
 *
 * This is the small file users include from their engine. It has no dependency
 * on Godot and no third-party library requirement.
 */
#ifndef BOQSC_TRANSVOXEL_H
#define BOQSC_TRANSVOXEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TV_REGULAR_SAMPLE_COUNT 8
#define TV_TRANSITION_HIGH_SAMPLE_COUNT 9
#define TV_TRANSITION_SAMPLE_COUNT 14

#define TV_REGULAR_MAX_VERTICES 12
#define TV_REGULAR_MAX_TRIANGLES 5
#define TV_TRANSITION_MAX_VERTICES 12
#define TV_TRANSITION_MAX_TRIANGLES 12

typedef struct TvVec3 {
    float x;
    float y;
    float z;
} TvVec3;

typedef struct TvTriangle {
    uint32_t a;
    uint32_t b;
    uint32_t c;
} TvTriangle;

typedef enum TvResult {
    TV_OK = 0,
    TV_ERROR_NULL = -1,
    TV_ERROR_SMALL_VERTEX_BUFFER = -2,
    TV_ERROR_SMALL_TRIANGLE_BUFFER = -3,
    TV_ERROR_BAD_CASE = -4
} TvResult;

typedef struct TvBuildInfo {
    int result;
    int case_index;
    int vertex_count;
    int triangle_count;
} TvBuildInfo;

typedef TvBuildInfo (*TvTransitionBuilderFn)(
    const float sample_values[TV_TRANSITION_SAMPLE_COUNT],
    float iso_level,
    TvVec3 origin,
    TvVec3 scale,
    TvVec3 *out_vertices,
    int max_vertices,
    TvTriangle *out_triangles,
    int max_triangles);

/* Utility constructors. */
TvVec3 tv_vec3(float x, float y, float z);

/* Case indexing. A sample is inside when value < iso_level. */
int tv_regular_case_index(const float samples[TV_REGULAR_SAMPLE_COUNT], float iso_level);
int tv_transition_case_index(const float samples[TV_TRANSITION_SAMPLE_COUNT], float iso_level);

/* Fill transition samples 9..13 from the clean-room table's documented source
 * rules: 9<-0, 10<-2, 11<-6, 12<-8, 13<-4. This helper is useful for tests
 * and for engines that want the same conservative transition boundary contract
 * as the included proof suite. Engines with true coarse samples may set all
 * 14 samples themselves instead. The default clean-room M4 transition backend
 * uses samples 0..12. Sample 13 is retained for public ABI compatibility with
 * earlier 14-sample callers and is ignored by the default backend.
 */
void tv_transition_fill_derived_samples(float samples[TV_TRANSITION_SAMPLE_COUNT]);

/* Build one regular cell.
 * sample_values: 8 scalar values at the regular sample positions.
 * origin/scale: final position = origin + local_position * scale.
 * out_vertices: must have capacity at least TV_REGULAR_MAX_VERTICES for worst case.
 * out_triangles: must have capacity at least TV_REGULAR_MAX_TRIANGLES for worst case.
 */
TvBuildInfo tv_build_regular_cell(
    const float sample_values[TV_REGULAR_SAMPLE_COUNT],
    float iso_level,
    TvVec3 origin,
    TvVec3 scale,
    TvVec3 *out_vertices,
    int max_vertices,
    TvTriangle *out_triangles,
    int max_triangles);

/* Build one transition cell.
 * sample_values: 14 values at the transition sample positions.
 * case index uses samples 0..8. The default clean-room M4 backend uses
 * samples 9..12 for interpolation and ignores sample 13.
 */
TvBuildInfo tv_build_transition_cell(
    const float sample_values[TV_TRANSITION_SAMPLE_COUNT],
    float iso_level,
    TvVec3 origin,
    TvVec3 scale,
    TvVec3 *out_vertices,
    int max_vertices,
    TvTriangle *out_triangles,
    int max_triangles);

/* Optional transition backend hook.
 *
 * By default this is unset and tv_build_transition_cell() uses the clean-room
 * M4 published-topology transition backend from generated/transvoxel_tables.h.
 * Projects that compile a custom or legacy backend may install it here so
 * existing calls to tv_build_transition_cell() route through that backend.
 * Passing NULL resets to the default clean-room M4 backend.
 */
int tv_set_transition_backend_callback(TvTransitionBuilderFn builder);
TvTransitionBuilderFn tv_get_transition_backend_callback(void);
void tv_reset_transition_backend_callback(void);
int tv_transition_backend_is_custom(void);

/* Local sample positions used by the generated tables. These are exposed so an
 * engine can sample its own scalar field before calling the builders.
 */
TvVec3 tv_regular_sample_position(int sample_id);
TvVec3 tv_transition_sample_position(int sample_id);

#ifdef __cplusplus
}
#endif

#endif /* BOQSC_TRANSVOXEL_H */
