#ifndef GF_CORE_GEOMETRY_H
#define GF_CORE_GEOMETRY_H

#include "interfaces.h"

// Binary space partitioning geometry calculator
gf_geometry_calculator_t *gf_bsp_geometry_calculator_create(uint32_t padding);
void gf_bsp_geometry_calculator_destroy(gf_geometry_calculator_t *calc);

// Grid geometry calculator
gf_geometry_calculator_t *gf_grid_geometry_calculator_create(uint32_t columns,
                                                             uint32_t padding);
void gf_grid_geometry_calculator_destroy(gf_geometry_calculator_t *calc);

// Utility functions
gf_rect_t gf_rect_create(gf_coordinate_t x, gf_coordinate_t y,
                         gf_dimension_t width, gf_dimension_t height);
bool gf_rect_equals(const gf_rect_t *a, const gf_rect_t *b);
void gf_rect_apply_padding(gf_rect_t *rect, uint32_t padding);
bool gf_rect_is_valid(const gf_rect_t *rect);
void gf_rect_ensure_minimum_size(gf_rect_t *rect, uint32_t min_size);

#endif // GF_CORE_GEOMETRY_H
