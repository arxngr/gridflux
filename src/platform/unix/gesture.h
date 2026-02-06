#ifndef GF_GESTURE_H
#define GF_GESTURE_H

#include "../platform.h"
#include <stdbool.h>

typedef struct
{
    bool initialized;
    int xi_opcode;

    bool in_swipe;
    int finger_count;
    double total_dx;
    double total_dy;
} gf_gesture_state_t;

gf_error_code_t gf_gesture_init (gf_platform_interface_t *platform,
                                  gf_display_t display);
void gf_gesture_cleanup (gf_platform_interface_t *platform);
bool gf_gesture_poll (gf_platform_interface_t *platform, gf_display_t display,
                      void *event_out);

#endif /* GF_GESTURE_H */
