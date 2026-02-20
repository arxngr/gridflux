#include "../../utils/logger.h"
#include "../../utils/memory.h"
#include "internal.h"
#include <X11/extensions/shape.h>
#include <limits.h>
#include <string.h>

static bool
_rect_intersect (const gf_rect_t *a, const gf_rect_t *b, XRectangle *res)
{
    int x1 = (a->x > b->x) ? a->x : b->x;
    int y1 = (a->y > b->y) ? a->y : b->y;
    int x2 = (a->x + a->width < b->x + b->width) ? a->x + a->width : b->x + b->width;
    int y2 = (a->y + a->height < b->y + b->height) ? a->y + a->height : b->y + b->height;

    if (x1 < x2 && y1 < y2)
    {
        res->x = x1 - a->x;
        res->y = y1 - a->y;
        res->width = x2 - x1;
        res->height = y2 - y1;
        return true;
    }
    return false;
}

void
_apply_shape_mask (Display *dpy, Window overlay, int w, int h, int thickness, int frame_w,
                   int frame_h, const XRectangle *sub_rects, int sub_count)
{
    int shape_event_base, shape_error_base;
    if (!XShapeQueryExtension (dpy, &shape_event_base, &shape_error_base))
    {
        GF_LOG_WARN ("Shape extension not supported");
        return;
    }

    if (w <= 2 * thickness || h <= 2 * thickness)
        return;

    XRectangle hollow_rect;
    hollow_rect.x = thickness;
    hollow_rect.y = thickness;
    hollow_rect.width = frame_w;
    hollow_rect.height = frame_h;

    XShapeCombineMask (dpy, overlay, ShapeBounding, 0, 0, None, ShapeSet);
    XShapeCombineRectangles (dpy, overlay, ShapeBounding, 0, 0, &hollow_rect, 1,
                             ShapeSubtract, Unsorted);

    if (sub_count > 0 && sub_rects)
    {
        XShapeCombineRectangles (dpy, overlay, ShapeBounding, 0, 0,
                                 (XRectangle *)sub_rects, sub_count, ShapeSubtract,
                                 Unsorted);
    }

    XShapeCombineRectangles (dpy, overlay, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
}

void
_resize_border_overlay (Display *dpy, gf_border_t *b, const gf_rect_t *frame)
{
    int thickness = b->thickness;
    int x = frame->x - thickness;
    int y = frame->y - thickness;
    int w = frame->width + 2 * thickness;
    int h = frame->height + 2 * thickness;

    if (w > 0 && h > 0 && x >= SHRT_MIN && y >= SHRT_MIN && x <= SHRT_MAX && y <= SHRT_MAX
        && w <= USHRT_MAX && h <= USHRT_MAX)
    {
        XMoveResizeWindow (dpy, b->overlay, x, y, w, h);
    }

    b->last_rect = *frame;
}

Window
_create_border_overlay (Display *dpy, Window target, gf_color_t color, int thickness)
{
    gf_rect_t frame;
    if (!_get_frame_geometry (dpy, target, &frame))
    {
        GF_LOG_ERROR ("Failed to get frame geometry for target %lu",
                      (unsigned long)target);
        return None;
    }

    // Validate geometry
    if (frame.width <= 0 || frame.height <= 0 || thickness < 0)
    {
        GF_LOG_ERROR ("Invalid geometry for border overlay: w=%d, h=%d, thickness=%d",
                      frame.width, frame.height, thickness);
        return None;
    }

    int x = frame.x - thickness;
    int y = frame.y - thickness;
    int w = frame.width + 2 * thickness;
    int h = frame.height + 2 * thickness;

    Window root = DefaultRootWindow (dpy);
    XWindowAttributes root_attrs;
    if (!XGetWindowAttributes (dpy, root, &root_attrs))
        return None;

    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.background_pixel = color & 0x00FFFFFF;
    swa.border_pixel = 0;
    swa.save_under = True;
    swa.colormap = root_attrs.colormap;
    swa.bit_gravity = NorthWestGravity;

    Window overlay = XCreateWindow (
        dpy, root, x, y, w, h, 0, root_attrs.depth, InputOutput, root_attrs.visual,
        CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWSaveUnder | CWColormap,
        &swa);

    if (!overlay)
    {
        GF_LOG_ERROR ("Failed to create border overlay window");
        return None;
    }

    _apply_shape_mask (dpy, overlay, w, h, thickness, frame.width, frame.height, NULL, 0);
    XMapWindow (dpy, overlay);
    return overlay;
}

void
gf_border_add (gf_platform_t *platform, gf_handle_t window, gf_color_t color,
               int thickness)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    if (!data || !data->display || !data->borders)
        return;

    if (data->border_count >= GF_MAX_WINDOWS_PER_WORKSPACE * GF_MAX_WORKSPACES)
    {
        GF_LOG_WARN ("Border limit reached, cannot add more borders");
        return;
    }

    if (gf_window_is_excluded (data->display, (Window)window))
    {
        return;
    }

    // Check if border already exists for this window
    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i] && data->borders[i]->target == (Window)window)
        {
            GF_LOG_INFO ("Border already exists for window %lu, updating color",
                         (unsigned long)window);
            // Update existing border color
            gf_border_t *border = data->borders[i];
            border->color = color;
            if (border->overlay)
            {
                XSetWindowAttributes swa;
                swa.background_pixel = color & 0x00FFFFFF;
                XChangeWindowAttributes (data->display, border->overlay, CWBackPixel,
                                         &swa);
                XClearWindow (data->display, border->overlay);
            }
            return;
        }
    }

    Window overlay = _create_border_overlay (data->display, window, color, thickness);
    if (!overlay)
    {
        GF_LOG_WARN ("Failed to create border overlay for window %lu",
                     (unsigned long)window);
        return;
    }

    gf_border_t *border = gf_malloc (sizeof (gf_border_t));
    if (!border)
    {
        XDestroyWindow (data->display, overlay);
        return;
    }

    border->target = (Window)window;
    border->overlay = overlay;
    border->color = color;
    border->thickness = thickness;
    border->last_intersect_count = 0;

    // Init last_rect
    XWindowAttributes attrs;
    if (XGetWindowAttributes (data->display, (Window)window, &attrs))
    {
        Window root = DefaultRootWindow (data->display);
        int abs_x, abs_y;
        Window child;
        XTranslateCoordinates (data->display, (Window)window, root, 0, 0, &abs_x, &abs_y,
                               &child);

        // Store Frame Geometry as last_rect
        int left_ext = 0, right_ext = 0, top_ext = 0, bottom_ext = 0;
        bool is_csd = false;
        gf_platform_get_frame_extents (data->display, (Window)window, &left_ext,
                                       &right_ext, &top_ext, &bottom_ext, &is_csd);

        if (is_csd)
        {
            border->last_rect.x = abs_x + left_ext;
            border->last_rect.y = abs_y + top_ext;
            border->last_rect.width = attrs.width - left_ext - right_ext;
            border->last_rect.height = attrs.height - top_ext - bottom_ext;
        }
        else
        {
            border->last_rect.x = abs_x - left_ext;
            border->last_rect.y = abs_y - top_ext;
            border->last_rect.width = attrs.width + left_ext + right_ext;
            border->last_rect.height = attrs.height + top_ext + bottom_ext;
        }
    }

    data->borders[data->border_count++] = border;
    XFlush (data->display);
    GF_LOG_INFO ("Border added for window %lu (overlay %lu)", (unsigned long)window,
                 (unsigned long)overlay);
}

void
gf_border_cleanup (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    if (!data->borders)
        return;

    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i])
        {
            if (data->borders[i]->overlay)
                XDestroyWindow (data->display, data->borders[i]->overlay);
            gf_free (data->borders[i]);
        }
    }
    data->border_count = 0;
}

void
gf_border_remove (gf_platform_t *platform, gf_handle_t window)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Window target = (Window)window;

    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i] && data->borders[i]->target == target)
        {
            if (data->borders[i]->overlay)
                XDestroyWindow (data->display, data->borders[i]->overlay);
            gf_free (data->borders[i]);

            // Shift
            for (int j = i; j < data->border_count - 1; j++)
                data->borders[j] = data->borders[j + 1];

            data->border_count--;
            return;
        }
    }
}

void
gf_border_update (gf_platform_t *platform, const gf_config_t *config)
{
    if (!platform || !platform->platform_data || !config)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Display *dpy = data->display;
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();

    // Find all GUI windows geometries
    gf_rect_t gui_geoms[16];
    int gui_count = 0;

    Window root = DefaultRootWindow (dpy);
    unsigned char *prop_data = NULL;
    unsigned long nitems = 0;
    if (gf_platform_get_window_property (dpy, root, atoms->net_client_list, XA_WINDOW,
                                         &prop_data, &nitems)
        == GF_SUCCESS)
    {
        Window *clients = (Window *)prop_data;
        for (unsigned long i = 0; i < nitems && gui_count < 16; i++)
        {
            if (gf_window_is_gui (dpy, clients[i]))
            {
                if (_get_frame_geometry (dpy, clients[i], &gui_geoms[gui_count]))
                {
                    gui_count++;
                }
            }
        }
        XFree (prop_data);
    }

    for (int i = 0; i < data->border_count;)
    {
        gf_border_t *b = data->borders[i];
        if (!b)
        {
            i++;
            continue;
        }

        XWindowAttributes attrs;
        if (!XGetWindowAttributes (dpy, b->target, &attrs))
        {
            // Window is definitely gone - removing
            XDestroyWindow (dpy, b->overlay);
            gf_free (b);
            // Shift array
            for (int j = i; j < data->border_count - 1; j++)
                data->borders[j] = data->borders[j + 1];
            data->border_count--;
            continue;
        }

        if (attrs.map_state == IsUnmapped || gf_window_is_minimized (dpy, b->target))
        {
            XUnmapWindow (dpy, b->overlay);
            i++;
            continue;
        }

        XMapWindow (dpy, b->overlay);

        // Update Color dynamically
        if (b->color != config->border_color)
        {
            b->color = config->border_color;
            XSetWindowAttributes swa;
            swa.background_pixel = b->color & 0x00FFFFFF;
            XChangeWindowAttributes (dpy, b->overlay, CWBackPixel, &swa);
            XClearWindow (dpy, b->overlay);
        }

        gf_rect_t frame;
        if (!_get_frame_geometry (dpy, b->target, &frame))
        {
            i++;
            continue;
        }

        int thickness = b->thickness;
        int win_x = frame.x - thickness;
        int win_y = frame.y - thickness;
        int win_w = frame.width + 2 * thickness;
        int win_h = frame.height + 2 * thickness;

        // Find intersections with GUI windows to subtract from shape
        XRectangle intersections[16];
        int intersect_count = 0;
        gf_rect_t border_rect = { win_x, win_y, win_w, win_h };

        for (int g = 0; g < gui_count; g++)
        {
            if (_rect_intersect (&border_rect, &gui_geoms[g],
                                 &intersections[intersect_count]))
            {
                intersect_count++;
            }
        }

        bool geom_changed = (frame.x != b->last_rect.x || frame.y != b->last_rect.y
                             || frame.width != b->last_rect.width
                             || frame.height != b->last_rect.height);

        bool shape_changed = geom_changed || (intersect_count != b->last_intersect_count);

        if (!shape_changed && intersect_count > 0)
        {
            for (int k = 0; k < intersect_count; k++)
            {
                if (intersections[k].x != b->last_intersections[k].x
                    || intersections[k].y != b->last_intersections[k].y
                    || intersections[k].width != b->last_intersections[k].width
                    || intersections[k].height != b->last_intersections[k].height)
                {
                    shape_changed = true;
                    break;
                }
            }
        }

        if (shape_changed)
        {
            if (geom_changed)
                XMoveResizeWindow (dpy, b->overlay, win_x, win_y, win_w, win_h);

            _apply_shape_mask (dpy, b->overlay, win_w, win_h, thickness, frame.width,
                               frame.height, intersections, intersect_count);

            b->last_rect = frame;
            b->last_intersect_count = intersect_count;
            if (intersect_count > 0)
            {
                memcpy (b->last_intersections, intersections,
                        intersect_count * sizeof (XRectangle));
            }
        }

        i++;
    }
    XFlush (dpy);
}
