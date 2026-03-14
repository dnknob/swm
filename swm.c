/*
 * Event flow:
 *   MapRequest      → manage new window
 *   UnmapNotify     → unmanage window
 *   DestroyNotify   → unmanage window
 *   KeyPress        → dispatch keybind action
 *   ButtonPress     → begin drag (move / resize)
 *   MotionNotify    → update drag
 *   ButtonRelease   → end drag
 *   ConfigureRequest→ honour geometry requests from unmanaged windows
 *   EnterNotify     → optional sloppy focus
 */

#include <X11/extensions/Xinerama.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include "swm.h"
#include "mng.h"
#include "util.h"

static void cfg_defaults(Config *c);
static int  cfg_load(Config *c, const char *path);
static void cfg_apply_defaults_if_empty(Config *c);

static void grab_keys(WM *wm);
static void grab_buttons(WM *wm, Window w);

static void handle_map_request(WM *wm, XMapRequestEvent *e);
static void handle_unmap_notify(WM *wm, XUnmapEvent *e);
static void handle_destroy_notify(WM *wm, XDestroyWindowEvent *e);
static void handle_key_press(WM *wm, XKeyEvent *e);
static void handle_button_press(WM *wm, XButtonEvent *e);
static void handle_motion_notify(WM *wm, XMotionEvent *e);
static void handle_button_release(WM *wm, XButtonEvent *e);
static void handle_configure_request(WM *wm, XConfigureRequestEvent *e);
static void handle_enter_notify(WM *wm, XCrossingEvent *e);

static void action_spawn(WM *wm, const char *arg);
static void action_kill(WM *wm, const char *arg);
static void action_focus_next(WM *wm, const char *arg);
static void action_focus_prev(WM *wm, const char *arg);
static void action_fullscreen(WM *wm, const char *arg);
static void action_move(WM *wm, const char *arg);
static void action_resize(WM *wm, const char *arg);
static void action_quit(WM *wm, const char *arg);

static void          unmanage(WM *wm, Window win);
static void          set_focus(WM *wm, Client *c);
static void          swm_tile(WM *wm);
static void          swm_restore_float(WM *wm);
static void          action_tile_toggle(WM *wm, const char *arg);
static Monitor      *swm_monitor_at(WM *wm, int x, int y);
static Monitor      *swm_monitor_for_client(WM *wm, Client *c);
static void          ewmh_init(WM *wm);
static void          ewmh_update_client_list(WM *wm);
static void          ewmh_update_active_window(WM *wm, Client *c);
static void          ewmh_set_wm_state(WM *wm, Client *c, int fullscreen);
static void          handle_client_message(WM *wm, XClientMessageEvent *e);

static int
x_error_handler(Display *dpy, XErrorEvent *e)
{
    char msg[128];
    XGetErrorText(dpy, e->error_code, msg, sizeof(msg));
    util_log("X error: %s (request %d)", msg, e->request_code);
    return 0;
}

static int
x_error_start(Display *dpy, XErrorEvent *e)
{
    (void)dpy; (void)e;
    util_die("another window manager is already running");
    return 0;
}

int
swm_init(WM *wm, const char *config_path)
{
    memset(wm, 0, sizeof(*wm));

    cfg_defaults(&wm->cfg);
    cfg_load(&wm->cfg, config_path);
    cfg_apply_defaults_if_empty(&wm->cfg);

    wm->dpy = XOpenDisplay(NULL);
    if (wm->dpy == NULL) {
        util_log("cannot open display");
        return -1;
    }

    wm->screen = DefaultScreen(wm->dpy);
    wm->root   = RootWindow(wm->dpy, wm->screen);
    wm->sw     = DisplayWidth(wm->dpy, wm->screen);
    wm->sh     = DisplayHeight(wm->dpy, wm->screen);

    int xin_n = 0;
    XineramaScreenInfo *xin = NULL;
    if (XineramaIsActive(wm->dpy) &&
        (xin = XineramaQueryScreens(wm->dpy, &xin_n)) != NULL) {
        wm->nmonitors = (xin_n > SWM_MAX_MONITORS)
                        ? SWM_MAX_MONITORS : xin_n;
        for (int i = 0; i < wm->nmonitors; i++) {
            wm->monitors[i].x     = xin[i].x_org;
            wm->monitors[i].y     = xin[i].y_org;
            wm->monitors[i].w     = xin[i].width;
            wm->monitors[i].h     = xin[i].height;
            wm->monitors[i].bar_h = 0;
        }
        XFree(xin);
    } else {
        wm->nmonitors       = 1;
        wm->monitors[0].x   = 0;
        wm->monitors[0].y   = 0;
        wm->monitors[0].w   = wm->sw;
        wm->monitors[0].h   = wm->sh;
        wm->monitors[0].bar_h = 0;
    }

    XSetErrorHandler(x_error_start);
    XSelectInput(wm->dpy, wm->root,
                 SubstructureRedirectMask |
                 SubstructureNotifyMask   |
                 ButtonPressMask          |
                 PointerMotionMask        |
                 EnterWindowMask);
    XSync(wm->dpy, False);
    XSetErrorHandler(x_error_handler);

    wm->color_border = util_parse_color(wm->dpy, wm->screen,
                                        wm->cfg.border_color);
    wm->color_focus  = util_parse_color(wm->dpy, wm->screen,
                                   wm->cfg.focus_color);

    Cursor cursor = XCreateFontCursor(wm->dpy, XC_left_ptr);
    XDefineCursor(wm->dpy, wm->root, cursor);

    grab_keys(wm);

    XGrabButton(wm->dpy, Button1, wm->cfg.mod, wm->root,
                False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(wm->dpy, Button3, wm->cfg.mod, wm->root,
                False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);

    ewmh_init(wm);
    XSync(wm->dpy, False);
    return 0;
}

void
swm_run(WM *wm)
{
    XEvent ev;

    for (;;) {
        XNextEvent(wm->dpy, &ev);

        switch (ev.type) {
        case MapRequest:
            handle_map_request(wm, &ev.xmaprequest);
            break;
        case UnmapNotify:
            handle_unmap_notify(wm, &ev.xunmap);
            break;
        case DestroyNotify:
            handle_destroy_notify(wm, &ev.xdestroywindow);
            break;
        case KeyPress:
            handle_key_press(wm, &ev.xkey);
            break;
        case ButtonPress:
            handle_button_press(wm, &ev.xbutton);
            break;
        case MotionNotify:
            /* Collapse multiple motion events — use only the latest */
            while (XCheckTypedEvent(wm->dpy, MotionNotify, &ev))
                ;
            handle_motion_notify(wm, &ev.xmotion);
            break;
        case ButtonRelease:
            handle_button_release(wm, &ev.xbutton);
            break;
        case ConfigureRequest:
            handle_configure_request(wm, &ev.xconfigurerequest);
            break;
        case EnterNotify:
            handle_enter_notify(wm, &ev.xcrossing);
            break;
        case ClientMessage:
            handle_client_message(wm, &ev.xclient);
            break;
        default:
            break;
        }

        while (waitpid(-1, NULL, WNOHANG) > 0);
    }
}

void
swm_cleanup(WM *wm)
{
    /* Free all clients */
    Client *c = wm->clients;
    while (c != NULL) {
        Client *next = c->next;
        free(c);
        c = next;
    }
    wm->clients = NULL;

    if (wm->dpy != NULL) {
        XCloseDisplay(wm->dpy);
        wm->dpy = NULL;
    }
}

static void
handle_map_request(WM *wm, XMapRequestEvent *e)
{
    /* Ignore windows we already manage */
    if (mng_find(wm->clients, e->window) != NULL)
        return;

    XWindowAttributes wa;
    if (!XGetWindowAttributes(wm->dpy, e->window, &wa))
        return;

    if (wa.override_redirect)
        return;

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    if (XGetWindowProperty(wm->dpy, e->window,
                           wm->ewmh.net_wm_window_type, 0, 1, False,
                           XA_ATOM, &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop) == Success && prop) {
        Atom type = *(Atom *)prop;
        XFree(prop);
        if (type == wm->ewmh.net_wm_window_type_dock ||
            type == wm->ewmh.net_wm_window_type_panel) {
            XMapWindow(wm->dpy, e->window);
            Monitor *bm = swm_monitor_at(wm,
                              wa.x + wa.width  / 2,
                              wa.y + wa.height / 2);
            if ((int)wa.height > bm->bar_h)
                bm->bar_h = (int)wa.height;
            return;
        }
    }

    XSetWindowBorderWidth(wm->dpy, e->window,
                          (unsigned int)wm->cfg.border_width);
    XSetWindowBorder(wm->dpy, e->window, wm->color_border);

    /*
     * Two attributes must be set together to eliminate resize flicker:
     *
     *   background_pixmap = None
     *     Stops X from filling newly-exposed areas with a background colour
     *     on every ConfigureNotify/Expose during fast resize. Without this,
     *     the border colour (or black) smears across the window while the
     *     client races to repaint. Setting CWBackPixel alone causes the same
     *     problem, so we explicitly suppress it here.
     *
     *   bit_gravity = NorthWestGravity
     *     Preserves existing pixel content anchored to the top-left corner
     *     when the window grows. Only the newly revealed strip on the right
     *     or bottom edge is exposed, so the client repaints a tiny area
     *     rather than the whole window.
     *
     * Neither attribute alone is sufficient -- both must be applied together.
     */
    XSetWindowAttributes swa;
    swa.bit_gravity = NorthWestGravity;
    XChangeWindowAttributes(wm->dpy, e->window,
                            CWBitGravity, &swa);

    {
        Window rr, cr;
        int cx = 0, cy = 0, wx, wy;
        unsigned int pm;
        XQueryPointer(wm->dpy, wm->root, &rr, &cr,
                      &cx, &cy, &wx, &wy, &pm);
        Monitor *mon = swm_monitor_at(wm, cx, cy);
        if (wa.x == 0 && wa.y == 0) {
            wa.x = mon->x + (mon->w - wa.width)  / 2;
            wa.y = mon->y + mon->bar_h
                   + (mon->h - mon->bar_h - wa.height) / 2;
            XMoveWindow(wm->dpy, e->window, wa.x, wa.y);
        }
    }

    Client *c = mng_add(&wm->clients, e->window,
                        wa.x, wa.y,
                        (unsigned int)wa.width,
                        (unsigned int)wa.height);
    if (c == NULL)
        return;

    {
        XSizeHints hints;
        long supplied = 0;
        if (XGetWMNormalHints(wm->dpy, e->window, &hints, &supplied)) {
            if (supplied & PBaseSize) {
                c->base_w = hints.base_width;
                c->base_h = hints.base_height;
            }
            if (supplied & PMinSize) {
                c->min_w = hints.min_width;
                c->min_h = hints.min_height;
            }
            if (supplied & PMaxSize) {
                c->max_w = hints.max_width;
                c->max_h = hints.max_height;
            }
            if (supplied & PResizeInc) {
                c->inc_w = hints.width_inc;
                c->inc_h = hints.height_inc;
            }
        }
    }

    XSelectInput(wm->dpy, e->window,
                 EnterWindowMask | FocusChangeMask | PropertyChangeMask);

    grab_buttons(wm, e->window);

    {
        long desk = 0;
        XChangeProperty(wm->dpy, e->window, wm->ewmh.net_wm_desktop,
                        XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *)&desk, 1);
    }

    XMapWindow(wm->dpy, e->window);
    ewmh_update_client_list(wm);
    set_focus(wm, c);
    if (wm->tiling)
        swm_tile(wm);
}

static void
handle_unmap_notify(WM *wm, XUnmapEvent *e)
{
    unmanage(wm, e->window);
    if (wm->tiling)
        swm_tile(wm);
}

static void
handle_destroy_notify(WM *wm, XDestroyWindowEvent *e)
{
    unmanage(wm, e->window);
    if (wm->tiling)
        swm_tile(wm);
}

static void
handle_key_press(WM *wm, XKeyEvent *e)
{
    KeySym keysym = XLookupKeysym(e, 0);
    unsigned int mods = e->state & ~(LockMask | Mod2Mask); /* strip Caps/Num */

    for (int i = 0; i < wm->cfg.nkeys; i++) {
        Keybind *kb = &wm->cfg.keys[i];
        if (kb->keysym == keysym && kb->mod == mods) {
            /* Dispatch by action name */
            if      (strcmp(kb->action, "spawn")        == 0)
                action_spawn(wm, kb->arg);
            else if (strcmp(kb->action, "kill")         == 0)
                action_kill(wm, kb->arg);
            else if (strcmp(kb->action, "focus_next")   == 0)
                action_focus_next(wm, kb->arg);
            else if (strcmp(kb->action, "focus_prev")   == 0)
                action_focus_prev(wm, kb->arg);
            else if (strcmp(kb->action, "fullscreen")   == 0)
                action_fullscreen(wm, kb->arg);
            else if (strcmp(kb->action, "move")         == 0)
                action_move(wm, kb->arg);
            else if (strcmp(kb->action, "resize")       == 0)
                action_resize(wm, kb->arg);
            else if (strcmp(kb->action, "quit")         == 0)
                action_quit(wm, kb->arg);
            else if (strcmp(kb->action, "tile_toggle")  == 0)
                action_tile_toggle(wm, kb->arg);
            return;
        }
    }
}

static void
handle_button_press(WM *wm, XButtonEvent *e)
{
    if (!(e->state & wm->cfg.mod))
        return;

    Client *c = mng_find(wm->clients, e->subwindow);
    if (c == NULL)
        c = mng_find(wm->clients, e->window);
    if (c == NULL)
        return;

    set_focus(wm, c);

    if (wm->tiling)
        return;

    XRaiseWindow(wm->dpy, c->win);

    if (XGrabPointer(wm->dpy, wm->root, False,
                     ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync,
                     None, None, CurrentTime) != GrabSuccess)
        return;

    wm->drag_active  = 1;
    wm->drag_button  = (int)e->button;
    wm->drag_win     = c->win;
    wm->drag_start_x = e->x_root;
    wm->drag_start_y = e->y_root;
    wm->drag_win_x   = c->x;
    wm->drag_win_y   = c->y;
    wm->drag_win_w   = c->w;
    wm->drag_win_h   = c->h;
}

static void
handle_motion_notify(WM *wm, XMotionEvent *e)
{
    if (!wm->drag_active)
        return;

    Client *c = mng_find(wm->clients, wm->drag_win);
    if (c == NULL) {
        wm->drag_active = 0;
        return;
    }

    int dx = e->x_root - wm->drag_start_x;
    int dy = e->y_root - wm->drag_start_y;

    if (wm->drag_button == Button1) {
        /* Move */
        c->x = wm->drag_win_x + dx;
        c->y = wm->drag_win_y + dy;
        XMoveWindow(wm->dpy, c->win, c->x, c->y);
    } else if (wm->drag_button == Button3) {
        int nw = (int)wm->drag_win_w + dx;
        int nh = (int)wm->drag_win_h + dy;
        unsigned int w = (unsigned int)(nw < 64 ? 64 : nw);
        unsigned int h = (unsigned int)(nh < 64 ? 64 : nh);
        mng_apply_hints(c, &w, &h);
        c->w = w;
        c->h = h;
        XResizeWindow(wm->dpy, c->win, c->w, c->h);
    }
}

static void
handle_button_release(WM *wm, XButtonEvent *e)
{
    (void)e;
    if (wm->drag_active) {
        XUngrabPointer(wm->dpy, CurrentTime);
        wm->drag_active = 0;
    }
}

static void
handle_configure_request(WM *wm, XConfigureRequestEvent *e)
{
    XWindowChanges wc;
    wc.x            = e->x;
    wc.y            = e->y;
    wc.width        = e->width;
    wc.height       = e->height;
    wc.border_width = e->border_width;
    wc.sibling      = e->above;
    wc.stack_mode   = e->detail;

    XConfigureWindow(wm->dpy, e->window,
                     (unsigned int)e->value_mask, &wc);

    /* Sync our internal geometry if we track this window */
    Client *c = mng_find(wm->clients, e->window);
    if (c != NULL) {
        if (e->value_mask & CWX)      c->x = e->x;
        if (e->value_mask & CWY)      c->y = e->y;
        if (e->value_mask & CWWidth)  c->w = (unsigned int)e->width;
        if (e->value_mask & CWHeight) c->h = (unsigned int)e->height;
    }
}

static void
handle_enter_notify(WM *wm, XCrossingEvent *e)
{
    if (e->mode != NotifyNormal)
        return;

    Client *c = mng_find(wm->clients, e->window);
    if (c != NULL && c != wm->focused)
        set_focus(wm, c);
}

static void
action_spawn(WM *wm, const char *arg)
{
    const char *cmd = (arg != NULL && arg[0] != '\0')
                      ? arg
                      : wm->cfg.terminal;
    util_spawn(cmd);
}

static void
action_kill(WM *wm, const char *arg)
{
    (void)arg;
    mng_kill(wm->dpy, wm->focused);
}

static void
action_focus_next(WM *wm, const char *arg)
{
    (void)arg;
    Client *next = mng_next(wm->clients, wm->focused);
    if (next != NULL)
        set_focus(wm, next);
}

static void
action_focus_prev(WM *wm, const char *arg)
{
    (void)arg;
    Client *prev = mng_prev(wm->clients, wm->focused);
    if (prev != NULL)
        set_focus(wm, prev);
}

static void
action_fullscreen(WM *wm, const char *arg)
{
    (void)arg;
    if (wm->focused == NULL)
        return;
    Monitor *m = swm_monitor_for_client(wm, wm->focused);
    mng_toggle_fullscreen(wm->dpy, wm->focused,
                          m->x, m->y, m->w, m->h,
                          wm->cfg.border_width);
    ewmh_set_wm_state(wm, wm->focused, wm->focused->fullscreen);
}

static void
action_move(WM *wm, const char *arg)
{
    if (wm->focused == NULL || wm->tiling)
        return;

    int dx = 0, dy = 0;
    if (arg != NULL)
        sscanf(arg, "%d %d", &dx, &dy);

    mng_move(wm->dpy, wm->focused, dx, dy, wm->sw, wm->sh);
}

static void
action_resize(WM *wm, const char *arg)
{
    if (wm->focused == NULL || wm->tiling)
        return;

    int dw = 0, dh = 0;
    if (arg != NULL)
        sscanf(arg, "%d %d", &dw, &dh);

    mng_resize(wm->dpy, wm->focused, dw, dh);
}

static void
ewmh_init(WM *wm)
{
    EWMH *e = &wm->ewmh;

    e->net_supported          = XInternAtom(wm->dpy, "_NET_SUPPORTED",          False);
    e->net_client_list        = XInternAtom(wm->dpy, "_NET_CLIENT_LIST",        False);
    e->net_active_window      = XInternAtom(wm->dpy, "_NET_ACTIVE_WINDOW",      False);
    e->net_close_window       = XInternAtom(wm->dpy, "_NET_CLOSE_WINDOW",       False);
    e->net_wm_name            = XInternAtom(wm->dpy, "_NET_WM_NAME",            False);
    e->net_wm_state           = XInternAtom(wm->dpy, "_NET_WM_STATE",           False);
    e->net_wm_state_fullscreen= XInternAtom(wm->dpy, "_NET_WM_STATE_FULLSCREEN",False);
    e->net_wm_window_type     = XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE",     False);
    e->net_wm_window_type_dock= XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE_DOCK",False);
    e->net_wm_window_type_panel=XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE_PANEL",False);
    e->net_number_of_desktops = XInternAtom(wm->dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    e->net_current_desktop    = XInternAtom(wm->dpy, "_NET_CURRENT_DESKTOP",    False);
    e->net_wm_desktop         = XInternAtom(wm->dpy, "_NET_WM_DESKTOP",         False);
    e->utf8_string            = XInternAtom(wm->dpy, "UTF8_STRING",             False);

    Atom supported[] = {
        e->net_supported,
        e->net_client_list,
        e->net_active_window,
        e->net_close_window,
        e->net_wm_name,
        e->net_wm_state,
        e->net_wm_state_fullscreen,
        e->net_wm_window_type,
        e->net_wm_window_type_dock,
        e->net_wm_window_type_panel,
        e->net_number_of_desktops,
        e->net_current_desktop,
        e->net_wm_desktop,
    };
    XChangeProperty(wm->dpy, wm->root, e->net_supported,
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)supported,
                    (int)(sizeof(supported) / sizeof(Atom)));

    long one  = 1;
    long zero = 0;
    XChangeProperty(wm->dpy, wm->root, e->net_number_of_desktops,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&one, 1);
    XChangeProperty(wm->dpy, wm->root, e->net_current_desktop,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&zero, 1);

    Atom net_supporting = XInternAtom(wm->dpy, "_NET_SUPPORTING_WM_CHECK", False);
    Window child = XCreateSimpleWindow(wm->dpy, wm->root,
                                       -1, -1, 1, 1, 0, 0, 0);

    XChangeProperty(wm->dpy, wm->root, net_supporting,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&child, 1);
    XChangeProperty(wm->dpy, child, net_supporting,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&child, 1);

    XChangeProperty(wm->dpy, child, e->net_wm_name,
                    e->utf8_string, 8, PropModeReplace,
                    (unsigned char *)"swm", 3);

    XDeleteProperty(wm->dpy, wm->root, e->net_client_list);
    XDeleteProperty(wm->dpy, wm->root, e->net_active_window);
}

static void
ewmh_update_client_list(WM *wm)
{
    Window wins[256];
    int n = 0;
    Client *c = wm->clients;
    while (c != NULL && n < 256) {
        wins[n++] = c->win;
        c = c->next;
    }
    XChangeProperty(wm->dpy, wm->root, wm->ewmh.net_client_list,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)wins, n);
}

static void
ewmh_update_active_window(WM *wm, Client *c)
{
    Window w = c ? c->win : None;
    XChangeProperty(wm->dpy, wm->root, wm->ewmh.net_active_window,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&w, 1);
}

static void
ewmh_set_wm_state(WM *wm, Client *c, int fullscreen)
{
    if (c == NULL)
        return;
    if (fullscreen) {
        XChangeProperty(wm->dpy, c->win, wm->ewmh.net_wm_state,
                        XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)&wm->ewmh.net_wm_state_fullscreen, 1);
    } else {
        XDeleteProperty(wm->dpy, c->win, wm->ewmh.net_wm_state);
    }
}

static void
handle_client_message(WM *wm, XClientMessageEvent *e)
{
    Client *c = mng_find(wm->clients, e->window);

    if (e->message_type == wm->ewmh.net_active_window) {
        if (c != NULL && c != wm->focused)
            set_focus(wm, c);
        return;
    }

    if (e->message_type == wm->ewmh.net_close_window) {
        if (c != NULL)
            mng_kill(wm->dpy, c);
        return;
    }

    if (e->message_type == wm->ewmh.net_wm_state) {
        if (c == NULL)
            return;

        long action = e->data.l[0];
        Atom atom1  = (Atom)e->data.l[1];
        Atom atom2  = (Atom)e->data.l[2];

        int is_fs = (atom1 == wm->ewmh.net_wm_state_fullscreen ||
                     atom2 == wm->ewmh.net_wm_state_fullscreen);
        if (!is_fs)
            return;

        int should_fs = (action == 1) ||
                        (action == 2 && !c->fullscreen);

        if (should_fs != c->fullscreen) {
            Monitor *m = swm_monitor_for_client(wm, c);
            mng_toggle_fullscreen(wm->dpy, c,
                                  m->x, m->y, m->w, m->h,
                                  wm->cfg.border_width);
            ewmh_set_wm_state(wm, c, c->fullscreen);
        }
        return;
    }
}

static Monitor *
swm_monitor_at(WM *wm, int x, int y)
{
    for (int i = 0; i < wm->nmonitors; i++) {
        Monitor *m = &wm->monitors[i];
        if (x >= m->x && x < m->x + m->w &&
            y >= m->y && y < m->y + m->h)
            return m;
    }
    return &wm->monitors[0]; /* fallback to primary */
}

static Monitor *
swm_monitor_for_client(WM *wm, Client *c)
{
    return swm_monitor_at(wm,
                          c->x + (int)c->w / 2,
                          c->y + (int)c->h / 2);
}

static void
swm_tile(WM *wm)
{
    int bw = wm->cfg.border_width * 2;

    Client *c = wm->clients;
    while (c != NULL) {
        if (!c->tile_saved) {
            c->tile_saved_x = c->x;
            c->tile_saved_y = c->y;
            c->tile_saved_w = c->w;
            c->tile_saved_h = c->h;
            c->tile_saved   = 1;
        }
        c = c->next;
    }

    for (int mi = 0; mi < wm->nmonitors; mi++) {
        Monitor *mon = &wm->monitors[mi];
        int top    = mon->bar_h;
        int usable = mon->h - top;

        int n = 0;
        c = wm->clients;
        while (c != NULL) {
            int cx = c->tile_saved_x + (int)c->tile_saved_w / 2;
            int cy = c->tile_saved_y + (int)c->tile_saved_h / 2;
            if (swm_monitor_at(wm, cx, cy) == mon)
                n++;
            c = c->next;
        }

        if (n == 0)
            continue;

        int i = 0;
        c = wm->clients;
        while (c != NULL) {
            int cx = c->tile_saved_x + (int)c->tile_saved_w / 2;
            int cy = c->tile_saved_y + (int)c->tile_saved_h / 2;
            if (swm_monitor_at(wm, cx, cy) != mon) {
                c = c->next;
                continue;
            }

            if (n == 1) {
                mng_move_abs(wm->dpy, c, mon->x, mon->y + top);
                mng_resize_abs(wm->dpy, c,
                               (unsigned int)(mon->w - bw),
                               (unsigned int)(usable - bw));
            } else if (i == 0) {
                /* Master: left half */
                mng_move_abs(wm->dpy, c, mon->x, mon->y + top);
                mng_resize_abs(wm->dpy, c,
                               (unsigned int)(mon->w / 2 - bw),
                               (unsigned int)(usable - bw));
            } else {
                /* Stack: right half, divided evenly */
                int stack_h = usable / (n - 1);
                mng_move_abs(wm->dpy, c,
                             mon->x + mon->w / 2,
                             mon->y + top + (i - 1) * stack_h);
                mng_resize_abs(wm->dpy, c,
                               (unsigned int)(mon->w - mon->w / 2 - bw),
                               (unsigned int)(stack_h - bw));
            }
            i++;
            c = c->next;
        }
    }
}

static void
swm_restore_float(WM *wm)
{
    Client *c = wm->clients;
    while (c != NULL) {
        if (c->tile_saved) {
            mng_move_abs(wm->dpy, c, c->tile_saved_x, c->tile_saved_y);
            mng_resize_abs(wm->dpy, c, c->tile_saved_w, c->tile_saved_h);
            c->tile_saved = 0;
        }
        c = c->next;
    }
}

static void
action_tile_toggle(WM *wm, const char *arg)
{
    (void)arg;
    wm->tiling = !wm->tiling;
    if (wm->tiling)
        swm_tile(wm);
    else
        swm_restore_float(wm);
}

static void
action_quit(WM *wm, const char *arg)
{
    (void)arg;
    swm_cleanup(wm);
    exit(EXIT_SUCCESS);
}

static void
set_focus(WM *wm, Client *c)
{
    mng_focus(wm->dpy, c,
              wm->color_focus,
              wm->color_border,
              wm->focused,
              wm->cfg.border_width);
    wm->focused = c;
    ewmh_update_active_window(wm, c);
}

static void
unmanage(WM *wm, Window win)
{
    Client *c = mng_find(wm->clients, win);
    if (c == NULL)
        return;

    Client *next_focus = NULL;
    if (c == wm->focused)
        next_focus = mng_next(wm->clients, c);

    mng_remove(&wm->clients, win);

    if (next_focus == c)
        next_focus = wm->clients; /* fallback to head */

    if (c == wm->focused) {
        wm->focused = NULL;
        if (next_focus != NULL)
            set_focus(wm, next_focus);
        else {
            XSetInputFocus(wm->dpy, wm->root,
                           RevertToPointerRoot, CurrentTime);
            ewmh_update_active_window(wm, NULL);
        }
    }
    ewmh_update_client_list(wm);
}

static void
grab_keys(WM *wm)
{
    XUngrabKey(wm->dpy, AnyKey, AnyModifier, wm->root);

    for (int i = 0; i < wm->cfg.nkeys; i++) {
        KeyCode kc = XKeysymToKeycode(wm->dpy, wm->cfg.keys[i].keysym);
        if (kc == 0)
            continue;
                XGrabKey(wm->dpy, kc, wm->cfg.keys[i].mod, wm->root,
                 True, GrabModeAsync, GrabModeAsync);
        if (wm->cfg.keys[i].mod != 0) {
            XGrabKey(wm->dpy, kc, wm->cfg.keys[i].mod | Mod2Mask,
                     wm->root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(wm->dpy, kc, wm->cfg.keys[i].mod | LockMask,
                     wm->root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(wm->dpy, kc, wm->cfg.keys[i].mod | Mod2Mask | LockMask,
                     wm->root, True, GrabModeAsync, GrabModeAsync);
        }
    }
}

static void
grab_buttons(WM *wm, Window w)
{
    XGrabButton(wm->dpy, Button1, wm->cfg.mod, w,
                False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(wm->dpy, Button3, wm->cfg.mod, w,
                False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
}

/* -------------------------------------------------------------------------
 * Config parser
 *
 * File format (key = value, one per line, # for comments):
 *
 *   terminal   = xterm
 *   mod        = mod4
 *   border_width  = 2
 *   border_color  = #444444
 *   focus_color   = #8888ff
 *   move_step     = 32
 *   resize_step   = 32
 *   key mod4 Return     spawn  xterm
 *   key mod4 q          kill
 *   key mod4 j          focus_next
 *   key mod4 k          focus_prev
 *   key mod4 f          fullscreen
 *   key mod4 Left       move   -32 0
 *   key mod4 Right      move    32 0
 *   key mod4 Up         move    0 -32
 *   key mod4 Down       move    0  32
 *   key mod4shift equal resize  32 0
 *   key mod4shift minus resize -32 0
 *   key mod4 q          quit
 * ---------------------------------------------------------------------- */

static void
cfg_defaults(Config *c)
{
    memset(c, 0, sizeof(*c));
    util_str_copy(c->terminal,     SWM_DEFAULT_TERMINAL,   sizeof(c->terminal));
    util_str_copy(c->border_color, SWM_DEFAULT_BORDER_CLR, sizeof(c->border_color));
    util_str_copy(c->focus_color,  SWM_DEFAULT_FOCUS_CLR,  sizeof(c->focus_color));
    c->mod          = SWM_DEFAULT_MOD;
    c->border_width = SWM_DEFAULT_BORDER_W;
    c->move_step    = SWM_DEFAULT_MOVE_STEP;
    c->resize_step  = SWM_DEFAULT_RESIZE_STEP;
}

static unsigned int
parse_mod(const char *s)
{
    unsigned int mask = 0;

    const char *p = s;
    while (*p) {
        if      (strncmp(p, "mod4",  4) == 0) { mask |= Mod4Mask;   p += 4; }
        else if (strncmp(p, "mod1",  4) == 0) { mask |= Mod1Mask;   p += 4; }
        else if (strncmp(p, "mod2",  4) == 0) { mask |= Mod2Mask;   p += 4; }
        else if (strncmp(p, "mod3",  4) == 0) { mask |= Mod3Mask;   p += 4; }
        else if (strncmp(p, "mod5",  4) == 0) { mask |= Mod5Mask;   p += 4; }
        else if (strncmp(p, "ctrl",  4) == 0) { mask |= ControlMask; p += 4; }
        else if (strncmp(p, "shift", 5) == 0) { mask |= ShiftMask;  p += 5; }
        else if (strncmp(p, "alt",   3) == 0) { mask |= Mod1Mask;   p += 3; }
        else {
            p++; /* skip unknown character */
        }
    }
    return mask;
}

static int
cfg_load(Config *cfg, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        util_log("cannot open config '%s': %s", path, strerror(errno));
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = util_str_trim(line);
        if (*p == '#' || *p == '\0')
            continue;

        if (strncmp(p, "key ", 4) == 0) {
            if (cfg->nkeys >= SWM_MAX_KEYBINDS) {
                util_log("too many key bindings (max %d)", SWM_MAX_KEYBINDS);
                continue;
            }

            char mod_str[64]  = {0};
            char sym_str[64]  = {0};
            char action[SWM_MAX_CMD_LEN] = {0};
            char arg[SWM_MAX_CMD_LEN]    = {0};

            int fields = sscanf(p + 4, "%63s %63s %255s %255[^\n]",
                                mod_str, sym_str, action, arg);
            if (fields < 3)
                continue;

            Keybind *kb = &cfg->keys[cfg->nkeys];
            kb->mod    = parse_mod(mod_str);
            kb->keysym = XStringToKeysym(sym_str);
            if (kb->keysym == NoSymbol) {
                util_log("unknown keysym '%s'", sym_str);
                continue;
            }
            util_str_copy(kb->action, action, sizeof(kb->action));
            if (fields >= 4)
                util_str_copy(kb->arg, util_str_trim(arg), sizeof(kb->arg));

            cfg->nkeys++;
            continue;
        }

        char key[64]  = {0};
        char val[256] = {0};
        if (sscanf(p, "%63[^ =] = %255[^\n]", key, val) != 2)
            continue;

        char *v = util_str_trim(val);

        if      (strcmp(key, "terminal")     == 0)
            util_str_copy(cfg->terminal, v, sizeof(cfg->terminal));
        else if (strcmp(key, "mod")          == 0)
            cfg->mod = parse_mod(v);
        else if (strcmp(key, "border_width") == 0)
            cfg->border_width = atoi(v);
        else if (strcmp(key, "border_color") == 0)
            util_str_copy(cfg->border_color, v, sizeof(cfg->border_color));
        else if (strcmp(key, "focus_color")  == 0)
            util_str_copy(cfg->focus_color,  v, sizeof(cfg->focus_color));
        else if (strcmp(key, "move_step")    == 0)
            cfg->move_step = atoi(v);
        else if (strcmp(key, "resize_step")  == 0)
            cfg->resize_step = atoi(v);
    }

    fclose(fp);
    return 0;
}

static void
cfg_apply_defaults_if_empty(Config *c)
{
    if (c->terminal[0] == '\0')
        util_str_copy(c->terminal, SWM_DEFAULT_TERMINAL, sizeof(c->terminal));
    if (c->border_color[0] == '\0')
        util_str_copy(c->border_color, SWM_DEFAULT_BORDER_CLR,
                      sizeof(c->border_color));
    if (c->focus_color[0] == '\0')
        util_str_copy(c->focus_color, SWM_DEFAULT_FOCUS_CLR,
                      sizeof(c->focus_color));
    if (c->border_width <= 0)
        c->border_width = SWM_DEFAULT_BORDER_W;
    if (c->move_step <= 0)
        c->move_step = SWM_DEFAULT_MOVE_STEP;
    if (c->resize_step <= 0)
        c->resize_step = SWM_DEFAULT_RESIZE_STEP;
    if (c->mod == 0)
        c->mod = SWM_DEFAULT_MOD;
}

int
main(int argc, char *argv[])
{
    const char *config = SWM_CONFIG_PATH;
    if (argc >= 2)
        config = argv[1];

    WM wm;
    if (swm_init(&wm, config) != 0)
        return EXIT_FAILURE;

    swm_run(&wm);
    swm_cleanup(&wm);
    return EXIT_SUCCESS;
}