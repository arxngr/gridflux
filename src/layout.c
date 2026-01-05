#include "layout.h"
#include "memory.h"
#include <string.h>

typedef struct
{
    gf_layout_engine_t base;
    uint32_t padding;
    uint32_t min_window_size;
} layout_engine_t;

typedef struct
{
    gf_layout_engine_t base;
    uint32_t columns;
    uint32_t padding;
    uint32_t min_window_size;
} grid_layout_engine_t;

static uint32_t
get_padding (const gf_layout_engine_t *engine)
{
    if (engine->config)
        return engine->config->default_padding;

    if (engine->engine_data)
        return ((layout_engine_t *)engine->engine_data)->padding;

    return GF_DEFAULT_PADDING;
}

static uint32_t
get_min_size (const gf_layout_engine_t *engine)
{
    if (engine->config)
        return engine->config->min_window_size;

    if (engine->engine_data)
        return ((layout_engine_t *)engine->engine_data)->min_window_size;

    return GF_MIN_WINDOW_SIZE;
}

static void
split_layout (const gf_window_info_t *windows, uint32_t count, const gf_rect_t *area,
              gf_rect_t *out, uint32_t padding, uint32_t min_size, int depth)
{
    if (count == 0)
        return;

    if (count == 1)
    {
        out[0] = *area;
        gf_rect_apply_padding (&out[0], padding);
        gf_rect_ensure_minimum_size (&out[0], min_size);
        return;
    }

    bool vertical = (depth % 2) == 0;
    uint32_t left = count / 2;
    uint32_t right = count - left;

    if (vertical)
    {
        gf_dimension_t w1 = area->width / 2;
        gf_dimension_t w2 = area->width - w1;

        gf_rect_t a1 = { area->x, area->y, w1, area->height };
        gf_rect_t a2 = { area->x + w1, area->y, w2, area->height };

        split_layout (windows, left, &a1, out, padding, min_size, depth + 1);
        split_layout (windows + left, right, &a2, out + left, padding, min_size,
                      depth + 1);
    }
    else
    {
        gf_dimension_t h1 = area->height / 2;
        gf_dimension_t h2 = area->height - h1;

        gf_rect_t a1 = { area->x, area->y, area->width, h1 };
        gf_rect_t a2 = { area->x, area->y + h1, area->width, h2 };

        split_layout (windows, left, &a1, out, padding, min_size, depth + 1);
        split_layout (windows + left, right, &a2, out + left, padding, min_size,
                      depth + 1);
    }
}

static void
apply_layout (const gf_layout_engine_t *engine, const gf_window_info_t *windows,
              uint32_t count, const gf_rect_t *bounds, gf_rect_t *out)
{
    uint32_t padding = get_padding (engine);
    uint32_t min_size = get_min_size (engine);

    split_layout (windows, count, bounds, out, padding, min_size, 0);
}

static void
grid_apply_layout (const gf_layout_engine_t *engine, const gf_window_info_t *windows,
                   uint32_t count, const gf_rect_t *bounds, gf_rect_t *out)
{
    (void)windows;

    grid_layout_engine_t *grid = (grid_layout_engine_t *)engine->engine_data;

    uint32_t padding = get_padding (engine);
    uint32_t min_size = get_min_size (engine);

    uint32_t rows = (count + grid->columns - 1) / grid->columns;
    gf_dimension_t cell_w = bounds->width / grid->columns;
    gf_dimension_t cell_h = bounds->height / rows;

    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t col = i % grid->columns;
        uint32_t row = i / grid->columns;

        out[i] = gf_rect_create (bounds->x + col * cell_w, bounds->y + row * cell_h,
                                 cell_w, cell_h);

        gf_rect_apply_padding (&out[i], padding);
        gf_rect_ensure_minimum_size (&out[i], min_size);
    }
}

static void
engine_set_padding (gf_layout_engine_t *engine, uint32_t padding)
{
    if (engine && engine->engine_data)
        ((layout_engine_t *)engine->engine_data)->padding = padding;
}

static void
engine_set_min_size (gf_layout_engine_t *engine, uint32_t min_size)
{
    if (engine && engine->engine_data)
        ((layout_engine_t *)engine->engine_data)->min_window_size = min_size;
}

gf_layout_engine_t *
gf_layout_engine_create (const gf_config_t *config)
{
    layout_engine_t *eng = gf_malloc (sizeof (*eng));
    if (!eng)
        return NULL;

    memset (eng, 0, sizeof (*eng));

    eng->base.apply_layout = apply_layout;
    eng->base.set_padding = engine_set_padding;
    eng->base.set_min_size = engine_set_min_size;
    eng->base.engine_data = eng;
    eng->base.config = config;

    eng->padding = config ? config->default_padding : GF_DEFAULT_PADDING;
    eng->min_window_size = config ? config->min_window_size : GF_MIN_WINDOW_SIZE;

    return &eng->base;
}

gf_layout_engine_t *
gf_layout_engine_create_grid (uint32_t columns, const gf_config_t *config)
{
    grid_layout_engine_t *eng = gf_malloc (sizeof (*eng));
    if (!eng)
        return NULL;

    memset (eng, 0, sizeof (*eng));

    eng->base.apply_layout = grid_apply_layout;
    eng->base.set_padding = engine_set_padding;
    eng->base.set_min_size = engine_set_min_size;
    eng->base.engine_data = eng;
    eng->base.config = config;

    eng->columns = columns ? columns : 2;
    eng->padding = config ? config->default_padding : GF_DEFAULT_PADDING;
    eng->min_window_size = config ? config->min_window_size : GF_MIN_WINDOW_SIZE;

    return &eng->base;
}

void
gf_layout_engine_destroy (gf_layout_engine_t *engine)
{
    gf_free (engine);
}

gf_rect_t
gf_rect_create (gf_coordinate_t x, gf_coordinate_t y, gf_dimension_t width,
                gf_dimension_t height)
{
    return (gf_rect_t){ x, y, width, height };
}

bool
gf_rect_equals (const gf_rect_t *a, const gf_rect_t *b)
{
    return a && b && a->x == b->x && a->y == b->y && a->width == b->width
           && a->height == b->height;
}

bool
gf_rect_is_valid (const gf_rect_t *r)
{
    return r && r->width > 0 && r->height > 0;
}

void
gf_rect_apply_padding (gf_rect_t *r, uint32_t padding)
{
    if (!gf_rect_is_valid (r))
        return;

    r->x += padding;
    r->y += padding;
    r->width = (r->width > padding * 2) ? r->width - padding * 2 : r->width;
    r->height = (r->height > padding * 2) ? r->height - padding * 2 : r->height;
}

void
gf_rect_ensure_minimum_size (gf_rect_t *r, uint32_t min_size)
{
    if (!r)
        return;

    if (r->width < min_size)
        r->width = min_size;
    if (r->height < min_size)
        r->height = min_size;
}

bool
gf_rect_point_in (int x, int y, const gf_rect_t *r)
{
    return r && x >= r->x && x <= r->x + r->width && y >= r->y && y <= r->y + r->height;
}

bool
gf_rect_intersects (const gf_rect_t *a, const gf_rect_t *b)
{
    return a && b
           && !(a->x + a->width <= b->x || b->x + b->width <= a->x
                || a->y + a->height <= b->y || b->y + b->height <= a->y);
}

int
gf_rect_intersection_area (const gf_rect_t *a, const gf_rect_t *b)
{
    if (!gf_rect_intersects (a, b))
        return 0;

    int x1 = (a->x > b->x) ? a->x : b->x;
    int y1 = (a->y > b->y) ? a->y : b->y;
    int x2
        = ((a->x + a->width) < (b->x + b->width)) ? (a->x + a->width) : (b->x + b->width);
    int y2 = ((a->y + a->height) < (b->y + b->height)) ? (a->y + a->height)
                                                       : (b->y + b->height);

    return (x2 - x1) * (y2 - y1);
}
