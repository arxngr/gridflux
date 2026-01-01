#ifndef GF_CORE_GEOMETRY_H
#define GF_CORE_GEOMETRY_H
#include "../platform/platform.h"

gf_geometry_calculator_t *gf_bsp_geometry_calculator_create (const gf_config_t *config);
void gf_bsp_geometry_calculator_destroy (gf_geometry_calculator_t *calc);

gf_geometry_calculator_t *gf_grid_geometry_calculator_create (uint32_t columns,
                                                              const gf_config_t *config);
void gf_grid_geometry_calculator_destroy (gf_geometry_calculator_t *calc);

void gf_geometry_calculator_set_padding (gf_geometry_calculator_t *calc, uint32_t padding);
void gf_geometry_calculator_set_min_size (gf_geometry_calculator_t *calc, uint32_t min_size);
void gf_geometry_calculator_update_config (gf_geometry_calculator_t *calc, const gf_config_t *config);

gf_rect_t gf_rect_create (gf_coordinate_t x, gf_coordinate_t y, gf_dimension_t width,
                          gf_dimension_t height);
bool gf_rect_equals (const gf_rect_t *a, const gf_rect_t *b);
void gf_rect_apply_padding (gf_rect_t *rect, uint32_t padding);
bool gf_rect_is_valid (const gf_rect_t *rect);
void gf_rect_ensure_minimum_size (gf_rect_t *rect, uint32_t min_size);
bool gf_rect_point_in (int x, int y, const gf_rect_t *rect);
bool gf_rect_intersects (const gf_rect_t *a, const gf_rect_t *b);
int gf_rect_intersection_area (const gf_rect_t *a, const gf_rect_t *b);

#endif // GF_CORE_GEOMETRY_H
