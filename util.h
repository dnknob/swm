#ifndef SWM_UTIL_H
#define SWM_UTIL_H

#include <X11/Xlib.h>

#include <stddef.h>

void util_log(const char *fmt, ...);

void util_die(const char *fmt, ...);

int util_clamp(int v, int lo, int hi);

char *util_str_copy(char *dst, const char *src, size_t n);
char *util_str_trim(char *s);

void util_spawn(const char *cmd);

unsigned long util_parse_color(Display *dpy, int screen, const char *name);

#endif