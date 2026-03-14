#ifndef SWM_H
#define SWM_H

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "mng.h"
#include "util.h"

#define SWM_MAX_CMD_LEN   256
#define SWM_MAX_KEYBINDS  64
#define SWM_MAX_COLOR_LEN  32
#define SWM_MAX_EWMH_ATOMS 16
#define SWM_MAX_MONITORS  8
#define SWM_CONFIG_PATH   "swm.conf"

#define SWM_DEFAULT_TERMINAL   "xterm"
#define SWM_DEFAULT_BORDER_W   2
#define SWM_DEFAULT_BORDER_CLR "#444444"
#define SWM_DEFAULT_FOCUS_CLR  "#8888ff"
#define SWM_DEFAULT_MOD        Mod4Mask   /* Super key */
#define SWM_DEFAULT_MOVE_STEP  32         /* pixels per move step */
#define SWM_DEFAULT_RESIZE_STEP 32        /* pixels per resize step */

typedef struct {
    unsigned int mod;
    KeySym       keysym;
    char         action[SWM_MAX_CMD_LEN]; /* e.g. "spawn", "kill", "fullscreen" */
    char         arg[SWM_MAX_CMD_LEN];    /* e.g. "xterm", "" */
} Keybind;

typedef struct {
    char         terminal[SWM_MAX_CMD_LEN];
    unsigned int mod;                          /* modifier mask */
    int          border_width;
    char         border_color[SWM_MAX_COLOR_LEN];
    char         focus_color[SWM_MAX_COLOR_LEN];
    int          move_step;
    int          resize_step;
    Keybind      keys[SWM_MAX_KEYBINDS];
    int          nkeys;
} Config;

typedef struct {
    int x, y, w, h;
    int bar_h;                     /* reserved height for dock/panel */
} Monitor;

typedef struct {
    Atom net_supported;
    Atom net_client_list;
    Atom net_active_window;
    Atom net_close_window;
    Atom net_wm_name;
    Atom net_wm_state;
    Atom net_wm_state_fullscreen;
    Atom net_wm_window_type;
    Atom net_wm_window_type_dock;
    Atom net_wm_window_type_panel;
    Atom net_number_of_desktops;
    Atom net_current_desktop;
    Atom net_wm_desktop;
    Atom utf8_string;
} EWMH;

typedef struct {
    Display     *dpy;
    Window       root;
    int          screen;
    int          sw, sh;           /* full virtual screen size */

    Config       cfg;

    Monitor      monitors[SWM_MAX_MONITORS];
    int          nmonitors;

    Client      *clients;          /* managed client list (head) */
    Client      *focused;          /* currently focused client */

    int          drag_active;
    int          drag_button;      /* Button1 = move, Button3 = resize */
    Window       drag_win;
    int          drag_start_x;     /* pointer position at drag start */
    int          drag_start_y;
    int          drag_win_x;       /* window position at drag start */
    int          drag_win_y;
    unsigned int drag_win_w;       /* window size at drag start */
    unsigned int drag_win_h;

    unsigned long color_border;    /* resolved pixel values */
    unsigned long color_focus;

    int tiling;                    /* 0 = floating, 1 = tiling */
    EWMH ewmh;                     /* interned EWMH atoms */
} WM;

int  swm_init(WM *wm, const char *config_path);

void swm_run(WM *wm);

void swm_cleanup(WM *wm);

#endif /* SWM_H */