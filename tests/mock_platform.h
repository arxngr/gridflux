#ifndef GF_TEST_MOCK_PLATFORM_H
#define GF_TEST_MOCK_PLATFORM_H

#include "../src/platform/platform.h"

typedef struct
{
    gf_handle_t id;
    unsigned long desktop; // raw _NET_WM_DESKTOP; 0xFFFFFFFF marks sticky
    bool desktop_readable;
    bool maximized;
    bool excluded;
    const char *cls; // class/name, used for rule matching
} mock_window_t;

#define MOCK_STICKY_DESKTOP 0xFFFFFFFFUL

typedef struct
{
    mock_window_t wins[64];
    uint32_t count;
    uint32_t workspace_count;
    gf_handle_t focused; // what window_get_focused reports
    int enumerate_calls; // lets a test assert single-pass vs per-workspace
    int unminimize_calls;
    int minimize_calls;
    int op_counter;       // monotonic tick for ordering assertions
    int dock_restore_seq; // op_counter when the dock was restored (-1 if never)
    int first_show_seq;   // op_counter when the first window was shown (-1 if never)
} mock_state_t;

void mock_use (mock_state_t *st);
void mock_add (mock_state_t *st, gf_handle_t id, unsigned long desktop, const char *cls);
gf_platform_t *mock_platform (void);

#endif // GF_TEST_MOCK_PLATFORM_H
