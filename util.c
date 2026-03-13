#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>

#include "util.h"

void
util_log(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "swm: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

void
util_die(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "swm: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

int
util_clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

char *
util_str_copy(char *dst, const char *src, size_t n)
{
    if (dst == NULL || n == 0)
        return dst;
    if (src == NULL) {
        dst[0] = '\0';
        return dst;
    }
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
    return dst;
}

char *
util_str_trim(char *s)
{
    if (s == NULL)
        return s;

    while (*s && isspace((unsigned char)*s))
        s++;

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1)))
        end--;
    *end = '\0';

    return s;
}

void
util_spawn(const char *cmd)
{
    if (cmd == NULL || cmd[0] == '\0')
        return;

    pid_t pid = fork();
    if (pid < 0) {
        util_log("fork failed");
        return;
    }

    if (pid == 0) {
        /* Child: create a new session so the process is fully detached */
        if (setsid() < 0)
            _exit(1);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(1);
    }
}

unsigned long
util_parse_color(Display *dpy, int screen, const char *name)
{
    XColor color, exact;
    Colormap cmap = DefaultColormap(dpy, screen);

    if (!XAllocNamedColor(dpy, cmap, name, &color, &exact)) {
        util_log("cannot allocate color '%s', using black", name);
        return BlackPixel(dpy, screen);
    }
    return color.pixel;
}