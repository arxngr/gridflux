#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    Display *d = XOpenDisplay(NULL);
    if (!d) {
        printf("Failed to open display\n");
        return 1;
    }
    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen);
    
    int sw = DisplayWidth(d, screen);
    int sh = DisplayHeight(d, screen);
    printf("Screen Size: %dx%d\n", sw, sh);

    Atom net_workarea = XInternAtom(d, "_NET_WORKAREA", True);
    Atom net_current_desktop = XInternAtom(d, "_NET_CURRENT_DESKTOP", True);
    
    if (net_workarea == None) {
        printf("_NET_WORKAREA atom not found\n");
    } else {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *prop = NULL;
        
        if (XGetWindowProperty(d, root, net_workarea, 0, 1024, False, XA_CARDINAL, 
                               &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
            printf("_NET_WORKAREA: nitems=%lu\n", nitems);
            long *workarea = (long *)prop;
            for (unsigned long i = 0; i < nitems; i+=4) {
                 if (i+3 < nitems)
                     printf("Desktop %lu: x=%ld, y=%ld, w=%ld, h=%ld\n", i/4, workarea[i], workarea[i+1], workarea[i+2], workarea[i+3]);
            }
            XFree(prop);
        } else {
            printf("Failed to get _NET_WORKAREA property\n");
        }
    }

    if (net_current_desktop != None) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *prop = NULL;
         if (XGetWindowProperty(d, root, net_current_desktop, 0, 1, False, XA_CARDINAL, 
                               &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
             if (nitems > 0)
                printf("_NET_CURRENT_DESKTOP: %lu\n", *(long*)prop);
             XFree(prop);
         }
    }

    // Struts
    Atom net_client_list = XInternAtom(d, "_NET_CLIENT_LIST", True);
    Atom net_wm_strut_partial = XInternAtom(d, "_NET_WM_STRUT_PARTIAL", True);
    
    if (net_client_list != None && net_wm_strut_partial != None) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *prop = NULL;
        
        if (XGetWindowProperty(d, root, net_client_list, 0, 4096, False, XA_WINDOW, 
                               &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
            Window *list = (Window*)prop;
            for (unsigned long i = 0; i < nitems; i++) {
                Atom type;
                int format;
                unsigned long nstrut, bytes;
                unsigned char *strut_prop = NULL;
                
                if (XGetWindowProperty(d, list[i], net_wm_strut_partial, 0, 12, False, XA_CARDINAL,
                                       &type, &format, &nstrut, &bytes, &strut_prop) == Success) {
                    if (nstrut >= 12) {
                        long *s = (long*)strut_prop;
                        if (s[0] > 0 || s[1] > 0 || s[2] > 0 || s[3] > 0) {
                             printf("Window %lu Strut: left=%ld, right=%ld, top=%ld, bottom=%ld\n", 
                                    (unsigned long)list[i], s[0], s[1], s[2], s[3]);
                        }
                    }
                    if (strut_prop) XFree(strut_prop);
                }
            }
            XFree(prop);
        }
    }

    XCloseDisplay(d);
    return 0;
}
