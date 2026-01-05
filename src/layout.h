#ifndef GF_CORE_LAYOUT_H
#define GF_CORE_LAYOUT_H

#include "config.h"
#include "types.h"

/* Forward declaration */
typedef struct gf_layout_engine gf_layout_engine_t;
struct gf_layout_engine
{
    void (*apply_layout) (const gf_layout_engine_t *engine,
                          const gf_window_info_t *windows, uint32_t count,
                          const gf_rect_t *workspace_bounds, gf_rect_t *out_rects);

    void (*set_padding) (gf_layout_engine_t *engine, uint32_t padding);
    void (*set_min_size) (gf_layout_engine_t *engine, uint32_t min_size);

    const gf_config_t *config;
    void *engine_data;
};

gf_layout_engine_t *gf_layout_engine_create (const gf_config_t *config);
gf_layout_engine_t *gf_layout_engine_create_grid (uint32_t columns,
                                                  const gf_config_t *config);

void gf_layout_engine_destroy (gf_layout_engine_t *engine);

gf_rect_t gf_rect_create (gf_coordinate_t x, gf_coordinate_t y, gf_dimension_t width,
                          gf_dimension_t height);
bool gf_rect_equals (const gf_rect_t *a, const gf_rect_t *b);
bool gf_rect_is_valid (const gf_rect_t *rect);
bool gf_rect_point_in (int x, int y, const gf_rect_t *rect);
bool gf_rect_intersects (const gf_rect_t *a, const gf_rect_t *b);
int gf_rect_intersection_area (const gf_rect_t *a, const gf_rect_t *b);

void gf_rect_apply_padding (gf_rect_t *rect, uint32_t padding);
void gf_rect_ensure_minimum_size (gf_rect_t *rect, uint32_t min_size);

#endif /* GF_CORE_LAYOUT_H */
