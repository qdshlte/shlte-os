/*
 * shlte/gui.h - GUI Compositor / Window Manager
 *
 * Provides a simple desktop environment with window management,
 * text rendering, and input handling on top of virtio-gpu.
 */

#ifndef SHLTE_GUI_H
#define SHLTE_GUI_H

#include <shlte/types.h>

/* Desktop colors (BGRX format: 0x00RRGGBB) */
#define COLOR_DESKTOP     0x0033485A   /* Dark blue-grey */
#define COLOR_TASKBAR     0x001E2D3D   /* Darker bar */
#define COLOR_WIN_BG      0x001E1E2E   /* Terminal background */
#define COLOR_WIN_BORDER  0x004A6A8A   /* Blue border */
#define COLOR_WIN_TITLE   0x002A4A6A   /* Title bar */
#define COLOR_TEXT        0x00CCCCCC   /* Light grey text */
#define COLOR_TEXT_BRIGHT 0x00FFFFFF   /* White */
#define COLOR_CURSOR      0x0000AAFF   /* Blue cursor */

/* Font metrics */
#define FONT_W      8
#define FONT_H      16

/* Terminal window dimensions */
#define TERM_COLS   80
#define TERM_ROWS   25
#define TERM_WIDTH  (TERM_COLS * FONT_W)   /* 640 */
#define TERM_HEIGHT (TERM_ROWS * FONT_H)   /* 400 */

/* Maximum number of windows */
#define MAX_WINDOWS 16

typedef struct {
    uint32_t x, y;
    uint32_t width, height;
    char title[64];
    int visible;
    int focused;
} window_t;

typedef struct {
    int cursor_x, cursor_y;
    char buffer[TERM_ROWS][TERM_COLS];
    int dirty;
    window_t win;
} terminal_t;

typedef struct {
    int initialized;
    terminal_t term;
    window_t windows[MAX_WINDOWS];
    int win_count;
    window_t *focused_win;
    int screen_width;
    int screen_height;
} gui_state_t;

extern gui_state_t g_gui;

/* API */
int  gui_init(void);
void gui_draw_desktop(void);
void gui_draw_window(window_t *win);
void gui_flush(void);
void gui_term_putchar(char c);
void gui_term_write(const char *s);
void gui_term_backspace(void);
void gui_term_newline(void);
void gui_update(void);

#endif /* SHLTE_GUI_H */
