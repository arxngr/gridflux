#ifndef GF_GUI_PLATFORM_H
#define GF_GUI_PLATFORM_H

#include "../../core/types.h"
#include <gtk/gtk.h>
#include <stdbool.h>

typedef struct gf_gui_platform gf_gui_platform_t;

struct gf_gui_platform
{
    // --- Lifecycle ---
    bool (*init) (gf_gui_platform_t *platform);
    void (*cleanup) (gf_gui_platform_t *platform);

    // --- Application Icons & Discovery ---
    GdkPaintable *(*get_app_icon) (gf_gui_platform_t *platform, const char *wm_class);
    void (*populate_app_dropdown) (gf_gui_platform_t *platform, GtkStringList *model);

    // --- Window Information ---
    GdkPaintable *(*get_window_icon) (gf_gui_platform_t *platform, gf_handle_t window);

    void *platform_data;
};

// Factory method implemented by the specific platform (windows/gui_platform.c or
// unix/gui_platform.c)
gf_gui_platform_t *gf_gui_platform_create (void);
void gf_gui_platform_destroy (gf_gui_platform_t *platform);

#endif // GF_GUI_PLATFORM_H
