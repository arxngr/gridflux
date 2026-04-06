#include "../../utils/logger.h"
#include "internal.h"
#include <string.h>
#include <windows.h>

// Thread-local pointer to platform data for the WinEventHook callback.
// WinEventHook callbacks don't support user-data, so we use a static.
static gf_windows_platform_data_t *s_platform_data = NULL;

static gf_resize_dir_t
_detect_direction (const gf_rect_t *initial, const gf_rect_t *current)
{
    gf_resize_dir_t dir = GF_RESIZE_NONE;

    int32_t init_right = initial->x + (int32_t)initial->width;
    int32_t init_bottom = initial->y + (int32_t)initial->height;
    int32_t cur_right = current->x + (int32_t)current->width;
    int32_t cur_bottom = current->y + (int32_t)current->height;

    // Use a small threshold (2px) to ignore sub-pixel jitter
    if (abs (current->x - initial->x) > 2)
        dir |= GF_RESIZE_LEFT;
    if (abs (cur_right - init_right) > 2)
        dir |= GF_RESIZE_RIGHT;
    if (abs (current->y - initial->y) > 2)
        dir |= GF_RESIZE_TOP;
    if (abs (cur_bottom - init_bottom) > 2)
        dir |= GF_RESIZE_BOTTOM;

    return dir;
}

static void
_get_dwm_rect (HWND hwnd, gf_rect_t *out)
{
    RECT rect;
    if (SUCCEEDED (DwmGetWindowAttribute (hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect,
                                          sizeof (rect)))
        || GetWindowRect (hwnd, &rect))
    {
        out->x = rect.left;
        out->y = rect.top;
        out->width = (gf_dimension_t)(rect.right - rect.left);
        out->height = (gf_dimension_t)(rect.bottom - rect.top);
    }
}

static void CALLBACK
_resize_event_proc (HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject,
                    LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    (void)hook;
    (void)idChild;
    (void)dwEventThread;
    (void)dwmsEventTime;

    if (!s_platform_data || idObject != OBJID_WINDOW || !hwnd)
        return;

    gf_resize_state_t *rs = &s_platform_data->resize_state;

    switch (event)
    {
    case EVENT_SYSTEM_MOVESIZESTART:
    {
        rs->window = (gf_handle_t)hwnd;
        _get_dwm_rect (hwnd, &rs->initial_rect);
        rs->current_rect = rs->initial_rect;
        rs->direction = GF_RESIZE_NONE;
        rs->phase = GF_RESIZE_ACTIVE;
        rs->pending = true;
        GF_LOG_INFO ("[RESIZE] Start: window=%p rect=(%d,%d,%u,%u)", (void *)hwnd,
                     rs->initial_rect.x, rs->initial_rect.y, rs->initial_rect.width,
                     rs->initial_rect.height);
        break;
    }

    case EVENT_SYSTEM_MOVESIZEEND:
    {
        if (rs->window != (gf_handle_t)hwnd)
            break;

        _get_dwm_rect (hwnd, &rs->current_rect);

        // If width and height are unchanged, this was a MOVE, not a resize.
        // Reset to idle without emitting a resize event.
        if (rs->current_rect.width == rs->initial_rect.width
            && rs->current_rect.height == rs->initial_rect.height)
        {
            GF_LOG_DEBUG ("[RESIZE] Move detected (not resize), ignoring end event");
            rs->phase = GF_RESIZE_IDLE;
            rs->window = 0;
            rs->direction = GF_RESIZE_NONE;
            rs->pending = false;
            break;
        }

        rs->direction = _detect_direction (&rs->initial_rect, &rs->current_rect);
        rs->phase = GF_RESIZE_COMPLETE;
        rs->pending = true;
        GF_LOG_INFO ("[RESIZE] End: window=%p dir=%d rect=(%d,%d,%u,%u)", (void *)hwnd,
                     rs->direction, rs->current_rect.x, rs->current_rect.y,
                     rs->current_rect.width, rs->current_rect.height);
        break;
    }
    }
}

// Separate callback for EVENT_OBJECT_LOCATIONCHANGE (fired during drag)
static void CALLBACK
_location_change_proc (HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject,
                       LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    (void)hook;
    (void)event;
    (void)idChild;
    (void)dwEventThread;
    (void)dwmsEventTime;

    if (!s_platform_data || idObject != OBJID_WINDOW || !hwnd)
        return;

    gf_resize_state_t *rs = &s_platform_data->resize_state;

    // Only process if we're in an active resize for this window
    if (rs->phase != GF_RESIZE_ACTIVE || rs->window != (gf_handle_t)hwnd)
        return;

    gf_rect_t new_rect;
    _get_dwm_rect (hwnd, &new_rect);

    // If width and height haven't changed, this is a MOVE (title bar drag), not a resize.
    // Only border drags change the window dimensions.
    if (new_rect.width == rs->initial_rect.width
        && new_rect.height == rs->initial_rect.height)
    {
        return;
    }

    rs->current_rect = new_rect;
    rs->direction = _detect_direction (&rs->initial_rect, &rs->current_rect);
    rs->pending = true;
}

gf_err_t
gf_resize_hook_install (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return GF_ERROR_INVALID_PARAMETER;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    s_platform_data = data;
    memset (&data->resize_state, 0, sizeof (data->resize_state));

    // Hook 1: Resize start/end events (range 0x000A–0x000B)
    data->resize_hook = SetWinEventHook (
        EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, NULL, _resize_event_proc, 0,
        0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (!data->resize_hook)
    {
        GF_LOG_ERROR ("Failed to install resize start/end WinEventHook");
        return GF_ERROR_PLATFORM_ERROR;
    }

    // Hook 2: Location change events during drag (0x800B)
    data->location_hook = SetWinEventHook (
        EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, NULL,
        _location_change_proc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (!data->location_hook)
    {
        GF_LOG_WARN ("Failed to install location change WinEventHook (resize will "
                     "only apply on drag end)");
    }

    GF_LOG_INFO ("Resize WinEventHooks installed");
    return GF_SUCCESS;
}

void
gf_resize_hook_uninstall (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    if (data->resize_hook)
    {
        UnhookWinEvent (data->resize_hook);
        data->resize_hook = NULL;
    }

    if (data->location_hook)
    {
        UnhookWinEvent (data->location_hook);
        data->location_hook = NULL;
    }

    GF_LOG_INFO ("Resize WinEventHooks uninstalled");
    s_platform_data = NULL;
}

bool
gf_resize_poll (gf_platform_t *platform, gf_resize_event_t *event)
{
    if (!platform || !platform->platform_data || !event)
        return false;

    // Pump messages so WinEventHook callbacks fire
    MSG msg;
    while (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage (&msg);
        DispatchMessage (&msg);
    }

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    gf_resize_state_t *rs = &data->resize_state;

    if (!rs->pending)
    {
        event->phase = GF_RESIZE_IDLE;
        return false;
    }

    event->window = rs->window;
    event->phase = rs->phase;
    event->direction = rs->direction;
    event->initial_rect = rs->initial_rect;
    event->current_rect = rs->current_rect;

    event->dx = rs->current_rect.x - rs->initial_rect.x;
    event->dy = rs->current_rect.y - rs->initial_rect.y;
    event->dw = (int32_t)rs->current_rect.width - (int32_t)rs->initial_rect.width;
    event->dh = (int32_t)rs->current_rect.height - (int32_t)rs->initial_rect.height;

    rs->pending = false;

    // After COMPLETE, reset to IDLE
    if (rs->phase == GF_RESIZE_COMPLETE)
    {
        rs->phase = GF_RESIZE_IDLE;
        rs->window = 0;
        rs->direction = GF_RESIZE_NONE;
    }

    return true;
}
