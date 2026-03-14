#ifndef SWM_MNG_H
#define SWM_MNG_H

#include <X11/Xlib.h>

typedef struct Client {
    Window       win;
    int          x, y;
    unsigned int w, h;
    int          saved_x, saved_y;
    unsigned int saved_w, saved_h;
    int          fullscreen;
    int          tile_saved_x, tile_saved_y;
    unsigned int tile_saved_w, tile_saved_h;
    int          tile_saved;

    int          base_w, base_h;
    int          min_w,  min_h;
    int          max_w,  max_h;
    int          inc_w,  inc_h;

    struct Client *next;
} Client;

Client *mng_add(Client **list, Window win,
                int x, int y, unsigned int w, unsigned int h);
Client *mng_remove(Client **list, Window win);

Client *mng_find(Client *list, Window win);

void mng_focus(Display *dpy, Client *c,
               unsigned long focused_color, unsigned long normal_color,
               Client *prev_focused, int border_width);

void mng_move(Display *dpy, Client *c,
              int dx, int dy, int sw, int sh);
void mng_move_abs(Display *dpy, Client *c, int x, int y);

void mng_resize(Display *dpy, Client *c, int dw, int dh);
void mng_resize_abs(Display *dpy, Client *c,
                    unsigned int w, unsigned int h);
                    
void mng_apply_hints(Client *c,
                     unsigned int *w, unsigned int *h);

void mng_toggle_fullscreen(Display *dpy, Client *c,
                           int mx, int my, int mw, int mh,
                           int border_width);

void mng_kill(Display *dpy, Client *c);

Client *mng_next(Client *list, Client *current);
Client *mng_prev(Client *list, Client *current);

int mng_count(Client *list);

#endif