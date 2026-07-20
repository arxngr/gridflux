// Characterization tests for gf_wm_watch: they pin which windows end up tracked
// and where, so the single-pass refactor stays behaviour-preserving.

#include "framework.h"
#include "mock_platform.h"

#include "../src/core/internal.h"
#include "../src/core/wm.h"
#include "../src/utils/list.h"

#include <string.h>

static gf_layout_engine_t g_layout; // unused by watch; gf_wm_create needs non-NULL
static char g_display_sentinel;     // a non-NULL display the mock ignores

static gf_wm_t *
make_wm (mock_state_t *st, gf_config_t *cfg)
{
    memset (cfg, 0, sizeof (*cfg));
    cfg->max_windows_per_workspace = 5;
    cfg->max_workspaces = 10;

    mock_use (st);

    gf_wm_t *m = NULL;
    gf_wm_create (&m, mock_platform (), &g_layout);
    m->config = cfg;
    m->display = (gf_display_t)(void *)&g_display_sentinel;
    return m;
}

static gf_win_info_t *
tracked (gf_wm_t *m, gf_handle_t id)
{
    return gf_window_list_find_by_window_id (wm_windows (m), id);
}

static uint32_t
tracked_count (gf_wm_t *m)
{
    return wm_windows (m)->count;
}

// Three ordinary windows all get tracked exactly once.
static void
test_basic_placement (void)
{
    mock_state_t st = { .workspace_count = 4 };
    mock_add (&st, 10, 0, "term");
    mock_add (&st, 11, 1, "editor");
    mock_add (&st, 12, 2, "browser");

    gf_config_t cfg;
    gf_wm_t *m = make_wm (&st, &cfg);
    gf_wm_watch (m);

    CHECK_EQ (tracked_count (m), 3, "all three windows tracked");
    CHECK (tracked (m, 10) && tracked (m, 10)->is_valid, "window 10 tracked+valid");
    CHECK (tracked (m, 11) && tracked (m, 11)->is_valid, "window 11 tracked+valid");
    CHECK (tracked (m, 12) && tracked (m, 12)->is_valid, "window 12 tracked+valid");

    gf_wm_destroy (m);
}

// Excluded windows are never tracked.
static void
test_excluded_skipped (void)
{
    mock_state_t st = { .workspace_count = 4 };
    mock_add (&st, 20, 0, "term");
    mock_add (&st, 21, 0, "panel");
    st.wins[1].excluded = true;

    gf_config_t cfg;
    gf_wm_t *m = make_wm (&st, &cfg);
    gf_wm_watch (m);

    CHECK_EQ (tracked_count (m), 1, "only the non-excluded window is tracked");
    CHECK (tracked (m, 20) != NULL, "window 20 tracked");
    CHECK (tracked (m, 21) == NULL, "excluded window 21 not tracked");

    gf_wm_destroy (m);
}

static void
test_unreadable_desktop_skipped (void)
{
    mock_state_t st = { .workspace_count = 4 };
    mock_add (&st, 30, 0, "term");
    mock_add (&st, 31, 0, "ghost");
    st.wins[1].desktop_readable = false;

    gf_config_t cfg;
    gf_wm_t *m = make_wm (&st, &cfg);
    gf_wm_watch (m);

    CHECK_EQ (tracked_count (m), 1, "unreadable-desktop window skipped");
    CHECK (tracked (m, 31) == NULL, "window 31 not tracked");

    gf_wm_destroy (m);
}

static void
test_existing_workspace_preserved (void)
{
    mock_state_t st = { .workspace_count = 4 };
    mock_add (&st, 40, 0, "term");

    gf_config_t cfg;
    gf_wm_t *m = make_wm (&st, &cfg);

    gf_win_info_t seed = { .id = 40, .workspace_id = 3, .is_valid = true };
    gf_window_list_add (wm_windows (m), &seed);

    gf_wm_watch (m);

    gf_win_info_t *w = tracked (m, 40);
    CHECK (w != NULL, "window 40 still tracked");
    CHECK_EQ (w ? w->workspace_id : -1, 3, "workspace 3 preserved, not reassigned");

    gf_wm_destroy (m);
}

static void
test_rescan_is_idempotent (void)
{
    mock_state_t st = { .workspace_count = 4 };
    mock_add (&st, 50, 0, "term");
    mock_add (&st, 51, 1, "editor");

    gf_config_t cfg;
    gf_wm_t *m = make_wm (&st, &cfg);
    gf_wm_watch (m);
    uint32_t after_first = tracked_count (m);
    gf_wm_watch (m);

    CHECK_EQ (after_first, 2, "two windows after first scan");
    CHECK_EQ (tracked_count (m), 2, "still two after rescan (no duplicates)");

    gf_wm_destroy (m);
}

// A maximized window is tracked and flagged maximized.
static void
test_maximized_tracked (void)
{
    mock_state_t st = { .workspace_count = 4 };
    mock_add (&st, 60, 0, "video");
    st.wins[0].maximized = true;

    gf_config_t cfg;
    gf_wm_t *m = make_wm (&st, &cfg);
    gf_wm_watch (m);

    gf_win_info_t *w = tracked (m, 60);
    CHECK (w != NULL, "maximized window tracked");
    CHECK (w && w->is_maximized, "maximized flag set");

    gf_wm_destroy (m);
}

// The scan enumerates the platform exactly once, regardless of workspace count.
static void
test_single_enumeration (void)
{
    mock_state_t st = { .workspace_count = 6 };
    mock_add (&st, 70, 0, "term");
    mock_add (&st, 71, 3, "editor");

    gf_config_t cfg;
    gf_wm_t *m = make_wm (&st, &cfg);
    gf_wm_watch (m);

    CHECK_EQ (st.enumerate_calls, 1, "window_enumerate called once for the whole scan");

    gf_wm_destroy (m);
}

// Switching to a workspace un-minimizes every window on it, not just the active
// one, and minimizes the windows on the workspaces left behind.
static void
test_switch_restores_all_windows (void)
{
    mock_state_t st = { .workspace_count = 3, .focused = 101 };

    gf_config_t cfg;
    gf_wm_t *m = make_wm (&st, &cfg);

    for (gf_ws_id_t ws = 1; ws <= 3; ws++)
        gf_workspace_list_ensure (wm_workspaces (m), ws, cfg.max_windows_per_workspace);

    // Three windows on workspace 2 (101 is active), one on workspace 1.
    gf_win_info_t on_ws2[] = { { .id = 100, .workspace_id = 2, .is_valid = true },
                               { .id = 101, .workspace_id = 2, .is_valid = true },
                               { .id = 102, .workspace_id = 2, .is_valid = true } };
    gf_win_info_t on_ws1 = { .id = 200, .workspace_id = 1, .is_valid = true };
    for (int i = 0; i < 3; i++)
        gf_window_list_add (wm_windows (m), &on_ws2[i]);
    gf_window_list_add (wm_windows (m), &on_ws1);

    switch_workspace (m, 2);

    CHECK_EQ (st.unminimize_calls, 3, "all three windows on workspace 2 are shown");
    CHECK (st.minimize_calls >= 1, "windows on other workspaces are minimized");

    gf_wm_destroy (m);
}

// Leaving a maximized workspace restores the dock before showing windows, so
// they tile against the final work area.
static void
test_dock_restored_before_windows (void)
{
    mock_state_t st = { .workspace_count = 3, .focused = 101 };
    gf_config_t cfg;
    gf_wm_t *m = make_wm (&st, &cfg);
    m->state.dock_hidden = true; // as if we just left a maximized workspace

    for (gf_ws_id_t ws = 1; ws <= 3; ws++)
        gf_workspace_list_ensure (wm_workspaces (m), ws, cfg.max_windows_per_workspace);

    gf_win_info_t bg = { .id = 100, .workspace_id = 2, .is_valid = true };
    gf_win_info_t active = { .id = 101, .workspace_id = 2, .is_valid = true };
    gf_window_list_add (wm_windows (m), &bg);
    gf_window_list_add (wm_windows (m), &active);

    switch_workspace (m, 2);

    CHECK (st.dock_restore_seq > 0, "dock was restored");
    CHECK (st.first_show_seq > 0, "a window was shown");
    CHECK (st.dock_restore_seq < st.first_show_seq,
           "dock restored before any window is shown");

    gf_wm_destroy (m);
}

int
main (void)
{
    RUN_TEST (test_basic_placement);
    RUN_TEST (test_single_enumeration);
    RUN_TEST (test_switch_restores_all_windows);
    RUN_TEST (test_dock_restored_before_windows);
    RUN_TEST (test_excluded_skipped);
    RUN_TEST (test_unreadable_desktop_skipped);
    RUN_TEST (test_existing_workspace_preserved);
    RUN_TEST (test_rescan_is_idempotent);
    RUN_TEST (test_maximized_tracked);
    return TEST_SUMMARY ();
}
