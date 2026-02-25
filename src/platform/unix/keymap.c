#include "../../utils/logger.h"
#include "platform.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <string.h>

/* Modifier masks we need to grab through (NumLock, CapsLock, ScrollLock). */
static unsigned int _lock_masks[] = { 0, Mod2Mask, LockMask, Mod2Mask | LockMask };
#define NUM_LOCK_MASKS (sizeof (_lock_masks) / sizeof (_lock_masks[0]))

/* The base modifier combination: Ctrl + Super. */
#define GF_MOD_MASK (ControlMask | Mod4Mask)

gf_err_t
gf_keymap_init (gf_platform_t *platform, gf_display_t display)
{
    if (!platform || !display)
        return GF_ERROR_INVALID_PARAMETER;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Window root = DefaultRootWindow (display);

    KeyCode left = XKeysymToKeycode (display, XK_Left);
    KeyCode right = XKeysymToKeycode (display, XK_Right);

    if (!left || !right)
    {
        GF_LOG_WARN ("Failed to resolve Left/Right keycodes — keymap disabled");
        return GF_ERROR_PLATFORM_ERROR;
    }

    /* Grab Ctrl+Super+Left and Ctrl+Super+Right through all lock-key
     * combinations so that NumLock / CapsLock don't block the binding. */
    for (unsigned int i = 0; i < NUM_LOCK_MASKS; i++)
    {
        XGrabKey (display, left, GF_MOD_MASK | _lock_masks[i], root, True, GrabModeAsync,
                  GrabModeAsync);
        XGrabKey (display, right, GF_MOD_MASK | _lock_masks[i], root, True, GrabModeAsync,
                  GrabModeAsync);
    }

    XFlush (display);

    data->keymap_initialized = true;
    GF_LOG_INFO ("Keymap initialized: Ctrl+Super+Left/Right for workspace switching");

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

    Window root = DefaultRootWindow (data->display);

    KeyCode left = XKeysymToKeycode (data->display, XK_Left);
    KeyCode right = XKeysymToKeycode (data->display, XK_Right);

    for (unsigned int i = 0; i < NUM_LOCK_MASKS; i++)
    {
        if (left)
            XUngrabKey (data->display, left, GF_MOD_MASK | _lock_masks[i], root);
        if (right)
            XUngrabKey (data->display, right, GF_MOD_MASK | _lock_masks[i], root);
    }

    data->keymap_initialized = false;
    GF_LOG_INFO ("Keymap cleaned up");
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
    while (XCheckTypedEvent (display, KeyPress, &ev))
    {
        KeySym sym = XLookupKeysym (&ev.xkey, 0);

        /* Strip lock-key bits so we only compare Ctrl+Super. */
        unsigned int mods = ev.xkey.state & ~(Mod2Mask | LockMask);

        if (mods == GF_MOD_MASK)
        {
            if (sym == XK_Left)
                return GF_KEY_WORKSPACE_PREV;
            if (sym == XK_Right)
                return GF_KEY_WORKSPACE_NEXT;
        }

        /* Not our key — put it back so other handlers can process it. */
        XPutBackEvent (display, &ev);
        break;
    }

    return GF_KEY_NONE;
}
