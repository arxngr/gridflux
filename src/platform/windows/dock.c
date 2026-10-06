#include "internal.h"
#include "taskbar.h"
#include <string.h>

// Explorer's auto-hide preference is desktop-wide. Manage individual taskbar
// visibility instead, leaving the user's preference and work areas unchanged.
// Work-area changes during maximize previously invalidated unrelated layouts.
// The core expands visible maximized apps separately into their taskbar gap.
static bool
gf_taskbar_native_autohide (void)
{
    APPBARDATA abd
        = { .cbSize = sizeof (abd), .hWnd = FindWindowA ("Shell_TrayWnd", NULL) };
    return abd.hWnd && (SHAppBarMessage (ABM_GETSTATE, &abd) & ABS_AUTOHIDE);
}

static gf_taskbar_state_t *
gf_taskbar_find (gf_windows_platform_data_t *data, HWND window)
{
    for (uint32_t i = 0; i < data->taskbar_count; i++)
        if (data->taskbars[i].window == window)
            return &data->taskbars[i];
    return NULL;
}

static bool
gf_taskbar_interacting (HWND window, gf_monitor_id_t monitor_id,
                        gf_windows_platform_data_t *data, POINT cursor)
{
    HWND foreground = GetForegroundWindow ();
    if (foreground && GetAncestor (foreground, GA_ROOTOWNER) == window)
        return true;
    GUITHREADINFO info = { .cbSize = sizeof (info) };
    DWORD thread = GetWindowThreadProcessId (window, NULL);
    if (thread && GetGUIThreadInfo (thread, &info)
        && (info.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE)) && info.hwndMenuOwner
        && GetAncestor (info.hwndMenuOwner, GA_ROOTOWNER) == window)
        return true;
    gf_rect_t full = data->monitors[monitor_id].full_bounds;
    bool cursor_on_monitor = cursor.x >= full.x && cursor.y >= full.y
                             && cursor.x < full.x + (int32_t)full.width
                             && cursor.y < full.y + (int32_t)full.height;
    return cursor_on_monitor
           && ((GetAsyncKeyState (VK_LWIN) & 0x8000)
               || (GetAsyncKeyState (VK_RWIN) & 0x8000));
}

static bool
gf_taskbar_edge (gf_taskbar_state_t *bar, const gf_rect_t *full, POINT cursor)
{
    RECT monitor = { full->x, full->y, full->x + (int32_t)full->width,
                     full->y + (int32_t)full->height };
    if (!PtInRect (&monitor, cursor))
        return false;
    if (PtInRect (&bar->rect, cursor))
        return true;
    bool horizontal
        = bar->rect.right - bar->rect.left >= bar->rect.bottom - bar->rect.top;
    if (horizontal)
    {
        bool top = bar->rect.top + bar->rect.bottom < monitor.top + monitor.bottom;
        return top ? cursor.y < monitor.top + 2 : cursor.y >= monitor.bottom - 2;
    }
    bool left = bar->rect.left + bar->rect.right < monitor.left + monitor.right;
    return left ? cursor.x < monitor.left + 2 : cursor.x >= monitor.right - 2;
}

static void
gf_taskbar_sync (gf_platform_t *platform, HWND window, const bool *hide_on_monitor,
                 uint32_t monitor_count, bool native_autohide, POINT cursor,
                 ULONGLONG now)
{
    if (!window)
        return;
    gf_windows_platform_data_t *data = platform->platform_data;
    gf_monitor_id_t id = gf_monitor_from_window (platform, window);
    bool hide = !native_autohide && id < monitor_count
                && id < data->enumerated_monitor_count && hide_on_monitor[id];
    gf_taskbar_state_t *bar = gf_taskbar_find (data, window);
    if (!hide)
    {
        gf_taskbar_restore_window (window);
        if (bar)
        {
            uint32_t index = (uint32_t)(bar - data->taskbars);
            memmove (bar, bar + 1, (data->taskbar_count - index - 1) * sizeof (*bar));
            data->taskbar_count--;
        }
        return;
    }
    if (!bar)
    {
        // Never revive a bar that was hidden before GridFlux took ownership.
        if (!IsWindowVisible (window) || data->taskbar_count >= GF_MAX_MONITORS)
            return;
        RECT rect;
        if (!GetWindowRect (window, &rect)
            || !SetPropA (window, GF_TASKBAR_HIDDEN_PROP, (HANDLE)(INT_PTR)1))
            return;
        bar = &data->taskbars[data->taskbar_count++];
        *bar = (gf_taskbar_state_t){ .window = window, .rect = rect, .monitor_id = id };
    }
    bar->monitor_id = id;
    // Explorer can move the taskbar after a display or DPI change while our
    // visibility override is active. Follow its current edge even when hidden.
    GetWindowRect (window, &bar->rect);
    if (gf_taskbar_edge (bar, &data->monitors[id].full_bounds, cursor)
        || gf_taskbar_interacting (window, id, data, cursor))
        bar->reveal_until = now + 300;
    bool reveal = now < bar->reveal_until;
    if (reveal != (IsWindowVisible (window) != FALSE))
        ShowWindow (window, reveal ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void
gf_dock_sync (gf_platform_t *platform, const bool *hide_on_monitor,
              uint32_t monitor_count)
{
    if (!platform || !platform->platform_data || !hide_on_monitor)
        return;
    gf_windows_platform_data_t *data = platform->platform_data;
    for (uint32_t i = 0; i < data->taskbar_count;)
    {
        if (IsWindow (data->taskbars[i].window))
        {
            i++;
            continue;
        }
        memmove (&data->taskbars[i], &data->taskbars[i + 1],
                 (--data->taskbar_count - i) * sizeof (data->taskbars[0]));
    }
    POINT cursor;
    if (!GetCursorPos (&cursor))
        return;
    bool native_autohide = gf_taskbar_native_autohide ();
    ULONGLONG now = GetTickCount64 ();
    gf_taskbar_sync (platform, FindWindowA ("Shell_TrayWnd", NULL), hide_on_monitor,
                     monitor_count, native_autohide, cursor, now);
    HWND secondary = NULL;
    while ((secondary = FindWindowExA (NULL, secondary, "Shell_SecondaryTrayWnd", NULL)))
        gf_taskbar_sync (platform, secondary, hide_on_monitor, monitor_count,
                         native_autohide, cursor, now);
}

void
gf_dock_hide (gf_platform_t *platform)
{
    bool hide_on_monitor[GF_MAX_MONITORS];
    for (uint32_t i = 0; i < GF_MAX_MONITORS; i++)
        hide_on_monitor[i] = true;
    gf_dock_sync (platform, hide_on_monitor, GF_MAX_MONITORS);
}

void
gf_dock_restore (gf_platform_t *platform)
{
    gf_taskbar_restore_all ();
    if (platform && platform->platform_data)
        ((gf_windows_platform_data_t *)platform->platform_data)->taskbar_count = 0;
}
