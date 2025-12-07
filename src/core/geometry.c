#include "../../include/core/geometry.h"
#include "../../include/core/config.h"
#include "../../include/core/logger.h"
#include "../../include/utils/memory.h"
#include <stdlib.h>

typedef struct
{
    gf_geometry_calculator_t base;
    uint32_t padding;
    uint32_t min_window_size;
} gf_bsp_geometry_calculator_t;

typedef struct
{
    gf_geometry_calculator_t base;
    uint32_t columns;
    uint32_t padding;
    uint32_t min_window_size;
} gf_grid_geometry_calculator_t;

static uint32_t
get_padding (const gf_geometry_calculator_t *calc_base)
{
    if (calc_base->config)
    {
        return calc_base->config->default_padding;
    }

    // Fallback to stored value
    if (calc_base->calculator_data)
    {
        gf_bsp_geometry_calculator_t *calc
            = (gf_bsp_geometry_calculator_t *)calc_base->calculator_data;
        return calc->padding;
    }

    return GF_DEFAULT_PADDING;
}

static uint32_t
get_min_window_size (const gf_geometry_calculator_t *calc_base)
{
    if (calc_base->config)
    {
        return calc_base->config->min_window_size;
    }

    if (calc_base->calculator_data)
    {
        gf_bsp_geometry_calculator_t *calc
            = (gf_bsp_geometry_calculator_t *)calc_base->calculator_data;
        return calc->min_window_size;
    }

    return GF_MIN_WINDOW_SIZE;
}

static void
gf_bsp_split_recursive (const gf_window_info_t *windows, uint32_t count,
                        const gf_rect_t *area, gf_rect_t *results, uint32_t padding,
                        uint32_t min_size, int depth)
{
    if (count == 0)
        return;

    if (count == 1)
    {
        results[0] = *area;
        gf_rect_apply_padding (&results[0], padding);
        gf_rect_ensure_minimum_size (&results[0], min_size);
        return;
    }

    bool split_vertically = (depth % 2 == 0);
    uint32_t left_count = count / 2;
    uint32_t right_count = count - left_count;

    if (split_vertically)
    {
        gf_dimension_t left_width = area->width / 2;
        gf_dimension_t right_width = area->width - left_width;

        gf_rect_t left_area = { area->x, area->y, left_width, area->height };
        gf_rect_t right_area
            = { area->x + left_width, area->y, right_width, area->height };

        gf_bsp_split_recursive (windows, left_count, &left_area, results, padding,
                                min_size, depth + 1);
        gf_bsp_split_recursive (windows + left_count, right_count, &right_area,
                                results + left_count, padding, min_size, depth + 1);
    }
    else
    {
        gf_dimension_t top_height = area->height / 2;
        gf_dimension_t bottom_height = area->height - top_height;

        gf_rect_t top_area = { area->x, area->y, area->width, top_height };
        gf_rect_t bottom_area
            = { area->x, area->y + top_height, area->width, bottom_height };

        gf_bsp_split_recursive (windows, left_count, &top_area, results, padding,
                                min_size, depth + 1);
        gf_bsp_split_recursive (windows + left_count, right_count, &bottom_area,
                                results + left_count, padding, min_size, depth + 1);
    }
}

static void
gf_bsp_calculate_layout (const gf_geometry_calculator_t *calc_base,
                         const gf_window_info_t *windows, uint32_t count,
                         const gf_rect_t *workspace_bounds, gf_rect_t *results)
{
    gf_bsp_geometry_calculator_t *calc
        = (gf_bsp_geometry_calculator_t *)calc_base->calculator_data;

    if (!calc)
    {
        GF_LOG_ERROR ("BSP calculator context is NULL");
        return;
    }

    // Get current values from config or fallback
    uint32_t padding = get_padding (calc_base);
    uint32_t min_size = get_min_window_size (calc_base);

    gf_bsp_split_recursive (windows, count, workspace_bounds, results, padding, min_size,
                            0);
}

static void
gf_grid_calculate_layout (const gf_geometry_calculator_t *calc_base,
                          const gf_window_info_t *windows, uint32_t count,
                          const gf_rect_t *workspace_bounds, gf_rect_t *results)
{
    gf_grid_geometry_calculator_t *calc
        = (gf_grid_geometry_calculator_t *)calc_base->calculator_data;

    if (!calc)
    {
        GF_LOG_ERROR ("Grid calculator context is NULL");
        return;
    }

    // Get current values from config or fallback
    uint32_t padding = get_padding (calc_base);
    uint32_t min_size = get_min_window_size (calc_base);

    uint32_t rows = (count + calc->columns - 1) / calc->columns;
    gf_dimension_t cell_width = workspace_bounds->width / calc->columns;
    gf_dimension_t cell_height = workspace_bounds->height / rows;

    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t col = i % calc->columns;
        uint32_t row = i / calc->columns;

        results[i] = gf_rect_create (workspace_bounds->x + col * cell_width,
                                     workspace_bounds->y + row * cell_height, cell_width,
                                     cell_height);

        gf_rect_apply_padding (&results[i], padding);
        gf_rect_ensure_minimum_size (&results[i], min_size);
    }
}

static void
gf_bsp_set_padding (gf_geometry_calculator_t *calc_base, uint32_t padding)
{
    gf_bsp_geometry_calculator_t *calc
        = (gf_bsp_geometry_calculator_t *)calc_base->calculator_data;

    if (calc)
    {
        calc->padding = padding;
        GF_LOG_DEBUG ("BSP calculator padding updated to: %u", padding);
    }
}

// Set min window size dynamically
static void
gf_bsp_set_min_size (gf_geometry_calculator_t *calc_base, uint32_t min_size)
{
    gf_bsp_geometry_calculator_t *calc
        = (gf_bsp_geometry_calculator_t *)calc_base->calculator_data;

    if (calc)
    {
        calc->min_window_size = min_size;
        GF_LOG_DEBUG ("BSP calculator min_window_size updated to: %u", min_size);
    }
}

static void
gf_grid_set_padding (gf_geometry_calculator_t *calc_base, uint32_t padding)
{
    gf_grid_geometry_calculator_t *calc
        = (gf_grid_geometry_calculator_t *)calc_base->calculator_data;

    if (calc)
    {
        calc->padding = padding;
        GF_LOG_DEBUG ("Grid calculator padding updated to: %u", padding);
    }
}

static void
gf_grid_set_min_size (gf_geometry_calculator_t *calc_base, uint32_t min_size)
{
    gf_grid_geometry_calculator_t *calc
        = (gf_grid_geometry_calculator_t *)calc_base->calculator_data;

    if (calc)
    {
        calc->min_window_size = min_size;
        GF_LOG_DEBUG ("Grid calculator min_window_size updated to: %u", min_size);
    }
}

gf_geometry_calculator_t *
gf_bsp_geometry_calculator_create (const gf_config_t *config)
{
    gf_bsp_geometry_calculator_t *calc
        = gf_malloc (sizeof (gf_bsp_geometry_calculator_t));
    if (!calc)
        return NULL;

    calc->base.calculate_layout = gf_bsp_calculate_layout;
    calc->base.set_padding = gf_bsp_set_padding;
    calc->base.set_min_size = gf_bsp_set_min_size;
    calc->base.calculator_data = calc;
    calc->base.config = config;

    // Initialize with config values or defaults
    calc->padding = config ? config->default_padding : GF_DEFAULT_PADDING;
    calc->min_window_size = config ? config->min_window_size : GF_MIN_WINDOW_SIZE;

    GF_LOG_DEBUG ("BSP calculator created with padding=%u, min_size=%u", calc->padding,
                  calc->min_window_size);

    return &calc->base;
}

void
gf_bsp_geometry_calculator_destroy (gf_geometry_calculator_t *calc)
{
    gf_free (calc);
}

gf_geometry_calculator_t *
gf_grid_geometry_calculator_create (uint32_t columns, const gf_config_t *config)
{
    gf_grid_geometry_calculator_t *calc
        = gf_malloc (sizeof (gf_grid_geometry_calculator_t));
    if (!calc)
        return NULL;

    calc->base.calculate_layout = gf_grid_calculate_layout;
    calc->base.set_padding = gf_grid_set_padding;
    calc->base.set_min_size = gf_grid_set_min_size;
    calc->base.calculator_data = calc;
    calc->base.config = config;
    calc->columns = columns > 0 ? columns : 2;

    // Initialize with config values or defaults
    calc->padding = config ? config->default_padding : GF_DEFAULT_PADDING;
    calc->min_window_size = config ? config->min_window_size : GF_MIN_WINDOW_SIZE;

    GF_LOG_DEBUG ("Grid calculator created with columns=%u, padding=%u, min_size=%u",
                  calc->columns, calc->padding, calc->min_window_size);

    return &calc->base;
}

void
gf_grid_geometry_calculator_destroy (gf_geometry_calculator_t *calc)
{
    gf_free (calc);
}

gf_rect_t
gf_rect_create (gf_coordinate_t x, gf_coordinate_t y, gf_dimension_t width,
                gf_dimension_t height)
{
    gf_rect_t rect = { x, y, width, height };
    return rect;
}

bool
gf_rect_equals (const gf_rect_t *a, const gf_rect_t *b)
{
    if (!a || !b)
        return false;
    return (a->x == b->x && a->y == b->y && a->width == b->width
            && a->height == b->height);
}

void
gf_rect_apply_padding (gf_rect_t *rect, uint32_t padding)
{
    if (!rect || !gf_rect_is_valid (rect))
        return;

    rect->x += padding;
    rect->y += padding;
    rect->width
        = (rect->width > padding * 2) ? rect->width - padding * 2 : GF_MIN_WINDOW_SIZE;
    rect->height
        = (rect->height > padding * 2) ? rect->height - padding * 2 : GF_MIN_WINDOW_SIZE;
}

bool
gf_rect_is_valid (const gf_rect_t *rect)
{
    return rect && rect->width > 0 && rect->height > 0;
}

void
gf_rect_ensure_minimum_size (gf_rect_t *rect, uint32_t min_size)
{
    if (!rect)
        return;

    if (rect->width < min_size)
        rect->width = min_size;
    if (rect->height < min_size)
        rect->height = min_size;
}

bool
gf_rect_point_in (int x, int y, const gf_rect_t *rect)
{
    return (x >= rect->x && x <= rect->x + rect->width && y >= rect->y
            && y <= rect->y + rect->height);
}

int
gf_rect_intersection_area (const gf_rect_t *a, const gf_rect_t *b)
{
    int x1 = (a->x > b->x) ? a->x : b->x;
    int y1 = (a->y > b->y) ? a->y : b->y;
    int x2 = (a->x + a->width < b->x + b->width) ? (a->x + a->width) : (b->x + b->width);
    int y2
        = (a->y + a->height < b->y + b->height) ? (a->y + a->height) : (b->y + b->height);

    if (x2 <= x1 || y2 <= y1)
        return 0;
    return (x2 - x1) * (y2 - y1);
}
