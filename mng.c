#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <stdlib.h>
#include <string.h>

#include "mng.h"
#include "util.h"

#define MIN_WIN_SIZE 64

Client *
mng_add(Client **list, Window win,
        int x, int y, unsigned int w, unsigned int h)
{
    Client *c = calloc(1, sizeof(Client));
    if (c == NULL) {
        util_log("mng_add: out of memory");
        return NULL;
    }

    c->win  = win;
    c->x    = x;
    c->y    = y;
    c->w    = w;
    c->h    = h;

    c->next = *list;
    *list   = c;

    return c;
}

Client *
mng_remove(Client **list, Window win)
{
    Client *prev = NULL;
    Client *c    = *list;

    while (c != NULL) {
        if (c->win == win) {
            if (prev != NULL)
                prev->next = c->next;
            else
                *list = c->next;
            free(c);
            return prev;
        }
        prev = c;
        c    = c->next;
    }

    return NULL; /* not found */
}

Client *
mng_find(Client *list, Window win)
{
    Client *c = list;
    while (c != NULL) {
        if (c->win == win)
            return c;
        c = c->next;
    }
    return NULL;
}

void
mng_focus(Display *dpy, Client *c,
          unsigned long focused_color, unsigned long normal_color,
          Client *prev_focused, int border_width)
{
    /* Unfocus previous window */
    if (prev_focused != NULL && prev_focused != c)
        XSetWindowBorder(dpy, prev_focused->win, normal_color);

    if (c == NULL)
        return;

    XSetWindowBorderWidth(dpy, c->win, (unsigned int)border_width);
    XSetWindowBorder(dpy, c->win, focused_color);
    XRaiseWindow(dpy, c->win);
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
}

void
mng_move(Display *dpy, Client *c, int dx, int dy, int sw, int sh)
{
    if (c == NULL)
        return;

    c->x = util_clamp(c->x + dx, 0, sw - (int)c->w);
    c->y = util_clamp(c->y + dy, 0, sh - (int)c->h);

    XMoveWindow(dpy, c->win, c->x, c->y);
}

void
mng_move_abs(Display *dpy, Client *c, int x, int y)
{
    if (c == NULL)
        return;

    c->x = x;
    c->y = y;
    XMoveWindow(dpy, c->win, c->x, c->y);
}

void
mng_resize(Display *dpy, Client *c, int dw, int dh)
{
    if (c == NULL)
        return;

    int nw = (int)c->w + dw;
    int nh = (int)c->h + dh;

    c->w = (unsigned int)(nw < MIN_WIN_SIZE ? MIN_WIN_SIZE : nw);
    c->h = (unsigned int)(nh < MIN_WIN_SIZE ? MIN_WIN_SIZE : nh);

    XResizeWindow(dpy, c->win, c->w, c->h);
}

void
mng_resize_abs(Display *dpy, Client *c, unsigned int w, unsigned int h)
{
    if (c == NULL)
        return;

    c->w = (w < MIN_WIN_SIZE) ? MIN_WIN_SIZE : w;
    c->h = (h < MIN_WIN_SIZE) ? MIN_WIN_SIZE : h;

    XResizeWindow(dpy, c->win, c->w, c->h);
}

void
mng_toggle_fullscreen(Display *dpy, Client *c,
                      int sw, int sh, int border_width)
{
    if (c == NULL)
        return;

    if (!c->fullscreen) {
        /* Save current geometry */
        c->saved_x = c->x;
        c->saved_y = c->y;
        c->saved_w = c->w;
        c->saved_h = c->h;

        XSetWindowBorderWidth(dpy, c->win, 0);
        c->x = 0;
        c->y = 0;
        c->w = (unsigned int)sw;
        c->h = (unsigned int)sh;
        XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
        XRaiseWindow(dpy, c->win);
        c->fullscreen = 1;
    } else {
        /* Restore geometry */
        c->x = c->saved_x;
        c->y = c->saved_y;
        c->w = c->saved_w;
        c->h = c->saved_h;
        XSetWindowBorderWidth(dpy, c->win, (unsigned int)border_width);
        XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
        c->fullscreen = 0;
    }
}

void
mng_kill(Display *dpy, Client *c)
{
    if (c == NULL)
        return;

    Atom wm_protocols   = XInternAtom(dpy, "WM_PROTOCOLS", False);
    Atom wm_delete_win  = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

    Atom   *protocols = NULL;
    int     n         = 0;
    int     supported = 0;

    if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
        for (int i = 0; i < n; i++) {
            if (protocols[i] == wm_delete_win) {
                supported = 1;
                break;
            }
        }
        XFree(protocols);
    }

    if (supported) {
        /* Politely ask the window to close */
        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type                 = ClientMessage;
        ev.xclient.window       = c->win;
        ev.xclient.message_type = wm_protocols;
        ev.xclient.format       = 32;
        ev.xclient.data.l[0]    = (long)wm_delete_win;
        ev.xclient.data.l[1]    = CurrentTime;
        XSendEvent(dpy, c->win, False, NoEventMask, &ev);
    } else {
        /* Force-kill */
        XKillClient(dpy, c->win);
    }
}

Client *
mng_next(Client *list, Client *current)
{
    if (list == NULL)
        return NULL;
    if (current == NULL || current->next == NULL)
        return list;
    return current->next;
}

Client *
mng_prev(Client *list, Client *current)
{
    if (list == NULL)
        return NULL;

    Client *prev = list;
    while (prev->next != NULL && prev->next != current)
        prev = prev->next;

    return prev;
}

int
mng_count(Client *list)
{
    int n = 0;
    Client *c = list;
    while (c != NULL) {
        n++;
        c = c->next;
    }
    return n;
}