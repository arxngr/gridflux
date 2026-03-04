#include "../../utils/logger.h"
#include "internal.h"
#include "platform.h"
#include <stdbool.h>
#include <windows.h>

static HHOOK g_keymap_hook = NULL;
static gf_key_action_t g_pending_action = GF_KEY_NONE;

static LRESULT CALLBACK
LowLevelKeyboardProc (int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
        {
            KBDLLHOOKSTRUCT *p = (KBDLLHOOKSTRUCT *)lParam;

            bool ctrl_pressed = (GetAsyncKeyState (VK_CONTROL) & 0x8000) != 0;
            bool win_pressed = (GetAsyncKeyState (VK_LWIN) & 0x8000) != 0
                               || (GetAsyncKeyState (VK_RWIN) & 0x8000) != 0;

            if (ctrl_pressed && win_pressed)
            {
                if (p->vkCode == VK_LEFT)
                {
                    g_pending_action = GF_KEY_WORKSPACE_PREV;
                    return 1; // Consume key to prevent Windows Virtual Desktop switch
                }
                else if (p->vkCode == VK_RIGHT)
                {
                    g_pending_action = GF_KEY_WORKSPACE_NEXT;
                    return 1; // Consume key
                }
            }
        }
    }
    return CallNextHookEx (g_keymap_hook, nCode, wParam, lParam);
}

gf_err_t
gf_keymap_init (gf_platform_t *platform, gf_display_t display)
{
    (void)platform;
    (void)display;

    g_pending_action = GF_KEY_NONE;

    // Use a Low-Level Keyboard Hook instead of RegisterHotKey to bypass
    // the native Windows 10/11 reserved Ctrl+Win+Left/Right behavior.
    g_keymap_hook = SetWindowsHookEx (WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                      GetModuleHandle (NULL), 0);

    if (!g_keymap_hook)
    {
        GF_LOG_WARN ("Failed to install WH_KEYBOARD_LL hotkey hook");
        return GF_ERROR_PLATFORM_ERROR;
    }

    GF_LOG_INFO ("Keymap initialized (WH_KEYBOARD_LL): Ctrl+Win+Left/Right for workspace "
                 "switching");
    return GF_SUCCESS;
}

void
gf_keymap_cleanup (gf_platform_t *platform)
{
    (void)platform;

    if (g_keymap_hook)
    {
        UnhookWindowsHookEx (g_keymap_hook);
        g_keymap_hook = NULL;
    }

    GF_LOG_INFO ("Keymap cleaned up");
}

gf_key_action_t
gf_keymap_poll (gf_platform_t *platform, gf_display_t display)
{
    (void)platform;
    (void)display;

    MSG msg;
    gf_key_action_t action = GF_KEY_NONE;

    // Process all pending messages to ensure the hook thread doesn't freeze
    // Limit to 10 messages per poll to avoid infinite spinning (fast event loop)
    int max_msgs = 10;
    while (max_msgs-- > 0 && PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage (&msg);
        DispatchMessage (&msg);
    }

    // Check if the keyboard hook intercepted a workspace switch command
    if (g_pending_action != GF_KEY_NONE)
    {
        action = g_pending_action;
        g_pending_action = GF_KEY_NONE;
    }

    return action;
}
