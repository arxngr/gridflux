#include "../../utils/logger.h"
#include "platform.h"
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>
#include <string.h>

// The base modifier combination: Ctrl + Super.
#define GF_MOD_MASK (ControlMask | Mod4Mask)

// Mask to strip lock-key bits (NumLock=Mod2, CapsLock=Lock, ScrollLock=Mod3).
#define GF_LOCK_MASK (Mod2Mask | LockMask | Mod3Mask)

gf_err_t
gf_keymap_init (gf_platform_t *platform, gf_display_t display)
{
    if (!platform || !display)
        return GF_ERROR_INVALID_PARAMETER;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;

    // Check XInput2 availability.
    int xi_opcode, xi_event, xi_error;
    if (!XQueryExtension (display, "XInputExtension", &xi_opcode, &xi_event, &xi_error))
    {
        GF_LOG_WARN ("XInput extension not available — keymap disabled");
        return GF_ERROR_PLATFORM_ERROR;
    }

    int major = 2, minor = 0;
    if (XIQueryVersion (display, &major, &minor) != Success)
    {
        GF_LOG_WARN ("XInput2 not available — keymap disabled");
        return GF_ERROR_PLATFORM_ERROR;
    }

    data->xi_opcode = xi_opcode;

    // Select XI_RawKeyPress on the root window. Raw events are delivered
    // to all clients regardless of active grabs (unlike XGrabKey), so
    // this works on GNOME, KDE, and other desktop environments.
    unsigned char mask_data[XIMaskLen (XI_RawKeyPress)] = { 0 };
    XIEventMask mask;
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof (mask_data);
    mask.mask = mask_data;
    XISetMask (mask_data, XI_RawKeyPress);

    XISelectEvents (display, DefaultRootWindow (display), &mask, 1);
    XFlush (display);

    data->keymap_initialized = true;
    GF_LOG_INFO ("Keymap initialized (XInput2): Ctrl+Super+Left/Right to switch "
                 "workspace, Alt+E to exclude the focused app");

    return GF_SUCCESS;
}

void
gf_keymap_cleanup (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    if (!data->keymap_initialized || !data->display)
        return;

    // Deselect raw key events.
    unsigned char mask_data[XIMaskLen (XI_RawKeyPress)] = { 0 };
    XIEventMask mask;
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof (mask_data);
    mask.mask = mask_data;
    // All zeros = deselect everything.

    XISelectEvents (data->display, DefaultRootWindow (data->display), &mask, 1);

    data->keymap_initialized = false;
    GF_LOG_INFO ("Keymap cleaned up");
}

// Map a raw XI key-press cookie event to an action (or GF_KEY_NONE):
// Ctrl+Super+Left/Right switch workspace; Alt+E (Alt alone) excludes the
// focused window, captured here before arrangement can shift focus.
static gf_key_action_t
_keymap_action_from_raw (gf_display_t display, XEvent *ev, gf_linux_platform_data_t *data)
{
    XIRawEvent *raw = (XIRawEvent *)ev->xcookie.data;
    KeySym sym = XkbKeycodeToKeysym (display, raw->detail, 0, 0);

    // Read current modifier state from the keyboard. Raw events don't carry
    // modifier state, so we query it explicitly.
    Window root_ret, child_ret;
    int rx, ry, wx, wy;
    unsigned int mods = 0;
    XQueryPointer (display, DefaultRootWindow (display), &root_ret, &child_ret, &rx, &ry,
                   &wx, &wy, &mods);
    mods &= ~GF_LOCK_MASK;

    if (mods == GF_MOD_MASK)
    {
        if (sym == XK_Left)
        {
            data->pending_focus_window = gf_window_get_focused (display);
            return GF_KEY_WORKSPACE_PREV;
        }
        if (sym == XK_Right)
        {
            data->pending_focus_window = gf_window_get_focused (display);
            return GF_KEY_WORKSPACE_NEXT;
        }
        return GF_KEY_NONE;
    }

    if (mods == Mod1Mask && (sym == XK_e || sym == XK_E))
    {
        data->pending_focus_window = gf_window_get_focused (display);
        return GF_KEY_EXCLUDE_FOCUSED;
    }

    return GF_KEY_NONE;
}

gf_handle_t
gf_keymap_focused_window (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return 0;
    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    return data->pending_focus_window;
}

gf_key_action_t
gf_keymap_poll (gf_platform_t *platform, gf_display_t display)
{
    if (!platform || !platform->platform_data || !display)
        return GF_KEY_NONE;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    if (!data->keymap_initialized)
        return GF_KEY_NONE;

    XEvent ev;
    while (XCheckTypedEvent (display, GenericEvent, &ev))
    {
        if (ev.xcookie.extension != data->xi_opcode)
        {
            XPutBackEvent (display, &ev);
            break;
        }

        if (!XGetEventData (display, &ev.xcookie))
            continue;

        gf_key_action_t action = GF_KEY_NONE;
        if (ev.xcookie.evtype == XI_RawKeyPress)
            action = _keymap_action_from_raw (display, &ev, data);

        XFreeEventData (display, &ev.xcookie);

        if (action != GF_KEY_NONE)
            return action;
    }

    return GF_KEY_NONE;
}
