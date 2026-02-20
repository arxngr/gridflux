
#include "gesture.h"
#include "../../utils/logger.h"
#include "platform.h"

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <string.h>

gf_err_t
gf_gesture_init (gf_platform_t *platform, gf_display_t display)
{
    if (!platform || !platform->platform_data || !display)
        return GF_ERROR_INVALID_PARAMETER;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    gf_gesture_state_t *state = &data->gesture;

    memset (state, 0, sizeof (*state));

    int xi_event, xi_error;
    if (!XQueryExtension (display, "XInputExtension", &state->xi_opcode, &xi_event,
                          &xi_error))
    {
        GF_LOG_WARN ("XInput extension not available — gestures disabled");
        return GF_ERROR_PLATFORM_ERROR;
    }

    int major = 2, minor = 4;
    if (XIQueryVersion (display, &major, &minor) != Success)
    {
        GF_LOG_WARN ("XInput2 2.4+ not available (got %d.%d) — gestures disabled", major,
                     minor);
        return GF_ERROR_PLATFORM_ERROR;
    }

    GF_LOG_INFO ("XInput2 version %d.%d — gesture support available", major, minor);

    Window root = DefaultRootWindow (display);

    unsigned char mask_data[(XI_LASTEVENT >> 3) + 1];
    memset (mask_data, 0, sizeof (mask_data));

    XISetMask (mask_data, XI_GestureSwipeBegin);
    XISetMask (mask_data, XI_GestureSwipeUpdate);
    XISetMask (mask_data, XI_GestureSwipeEnd);

    XIEventMask evmask;
    evmask.deviceid = XIAllMasterDevices;
    evmask.mask_len = sizeof (mask_data);
    evmask.mask = mask_data;

    XISelectEvents (display, root, &evmask, 1);
    XFlush (display);

    state->initialized = true;
    GF_LOG_INFO ("Gesture detection initialized on root window");

    return GF_SUCCESS;
}

void
gf_gesture_cleanup (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    memset (&data->gesture, 0, sizeof (data->gesture));
}

bool
gf_gesture_poll (gf_platform_t *platform, gf_display_t display,
                 gf_gesture_event_t *event_out)
{
    if (!platform || !platform->platform_data || !display || !event_out)
        return false;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    gf_gesture_state_t *state = &data->gesture;
    gf_gesture_event_t *event = (gf_gesture_event_t *)event_out;

    if (!state->initialized)
        return false;

    memset (event, 0, sizeof (*event));

    while (XPending (display))
    {
        XEvent xev;
        XPeekEvent (display, &xev);

        if (xev.type != GenericEvent || xev.xcookie.extension != state->xi_opcode)
        {
            break;
        }

        XNextEvent (display, &xev);

        if (!XGetEventData (display, &xev.xcookie))
            continue;

        int evtype = xev.xcookie.evtype;

        if (evtype == XI_GestureSwipeBegin)
        {
            XIGestureSwipeEvent *ge = (XIGestureSwipeEvent *)xev.xcookie.data;

            state->in_swipe = true;
            state->finger_count = ge->detail;
            state->total_dx = 0.0;
            state->total_dy = 0.0;

            event->type = GF_GESTURE_SWIPE_BEGIN;
            event->fingers = ge->detail;
            event->dx = 0.0;
            event->dy = 0.0;
            event->total_dx = 0.0;
            event->total_dy = 0.0;

            XFreeEventData (display, &xev.xcookie);

            GF_LOG_DEBUG ("Gesture swipe begin: %d fingers", ge->detail);
            return true;
        }
        else if (evtype == XI_GestureSwipeUpdate)
        {
            XIGestureSwipeEvent *ge = (XIGestureSwipeEvent *)xev.xcookie.data;

            if (state->in_swipe)
            {
                state->total_dx += ge->delta_x;
                state->total_dy += ge->delta_y;

                event->type = GF_GESTURE_SWIPE_UPDATE;
                event->fingers = state->finger_count;
                event->dx = ge->delta_x;
                event->dy = ge->delta_y;
                event->total_dx = state->total_dx;
                event->total_dy = state->total_dy;

                XFreeEventData (display, &xev.xcookie);
                return true;
            }
        }
        else if (evtype == XI_GestureSwipeEnd)
        {
            XIGestureSwipeEvent *ge = (XIGestureSwipeEvent *)xev.xcookie.data;

            if (state->in_swipe)
            {
                bool cancelled = (ge->flags & XIGestureSwipeEventCancelled);
                state->in_swipe = false;

                event->type = cancelled ? GF_GESTURE_SWIPE_CANCEL : GF_GESTURE_SWIPE_END;
                event->fingers = state->finger_count;
                event->dx = 0.0;
                event->dy = 0.0;
                event->total_dx = state->total_dx;
                event->total_dy = state->total_dy;

                state->total_dx = 0.0;
                state->total_dy = 0.0;

                XFreeEventData (display, &xev.xcookie);

                GF_LOG_DEBUG ("Gesture swipe end: %s",
                              cancelled ? "cancelled" : "committed");
                return true;
            }
        }

        XFreeEventData (display, &xev.xcookie);
    }

    return false;
}
