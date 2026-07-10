/*
 * gui.c - GUI Compositor / Desktop Environment
 *
 * Manages windows, renders text, and handles the display
 * using the virtio-gpu framebuffer driver.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/virtio_gpu.h>
#include <shlte/virtio_input.h>
#include <shlte/gui.h>

/* ============================================================
 * Global state
 * ============================================================ */

gui_state_t g_gui;

/* ============================================================
 * Color macros
 * ============================================================ */

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    /* BGRX little-endian: bytes are B, G, R, X (MSB) */
    return ((uint32_t)b << 0) | ((uint32_t)g << 8) |
           ((uint32_t)r << 16);
}

/* ============================================================
 * Window management
 * ============================================================ */

static void draw_window_border(window_t *win)
{
    if (!win->visible) return;

    uint32_t border = win->focused ? rgb(0x6A, 0xAA, 0xFF) : COLOR_WIN_BORDER;

    /* Top border + title bar */
    virtio_gpu_fill_rect(win->x, win->y, win->width, 24, COLOR_WIN_TITLE);

    /* Border edges */
    virtio_gpu_fill_rect(win->x, win->y, win->width, 2, border);
    virtio_gpu_fill_rect(win->x, win->y + win->height - 2, win->width, 2, border);
    virtio_gpu_fill_rect(win->x, win->y, 2, win->height, border);
    virtio_gpu_fill_rect(win->x + win->width - 2, win->y, 2, win->height, border);

    /* Title text */
    int tx = (int)win->x + 8;
    int ty = (int)win->y + 4;
    for (int i = 0; win->title[i] && i < 60; i++) {
        virtio_gpu_draw_char((uint32_t)(tx + i * FONT_W), (uint32_t)ty,
                             win->title[i], COLOR_TEXT_BRIGHT, COLOR_WIN_TITLE);
    }

    /* Close button area (top-right corner) */
    virtio_gpu_fill_rect(win->x + win->width - 22, win->y + 4, 16, 16, rgb(0xAA, 0x44, 0x44));
    virtio_gpu_draw_char(win->x + win->width - 20, win->y + 4, 'X',
                         COLOR_TEXT_BRIGHT, rgb(0xAA, 0x44, 0x44));

    /* Interior background */
    virtio_gpu_fill_rect(win->x + 2, win->y + 24,
                         win->width - 4, win->height - 26,
                         COLOR_WIN_BG);
}

/* ============================================================
 * Terminal emulator
 * ============================================================ */

static void term_scroll_up(void)
{
    terminal_t *t = &g_gui.term;
    for (int row = 0; row < TERM_ROWS - 1; row++) {
        memcpy(t->buffer[row], t->buffer[row + 1], TERM_COLS);
    }
    memset(t->buffer[TERM_ROWS - 1], ' ', TERM_COLS);
    t->dirty = 1;
}

static void term_render(void)
{
    terminal_t *t = &g_gui.term;
    if (!t->win.visible) return;

    int base_x = (int)t->win.x + 6;
    int base_y = (int)t->win.y + 28;

    for (int row = 0; row < TERM_ROWS; row++) {
        for (int col = 0; col < TERM_COLS; col++) {
            char c = t->buffer[row][col];
            if (c < ' ') c = ' ';
            virtio_gpu_draw_char((uint32_t)(base_x + col * FONT_W),
                                 (uint32_t)(base_y + row * FONT_H),
                                 c, COLOR_TEXT, COLOR_WIN_BG);
        }
    }

    /* Cursor */
    if (t->cursor_y < TERM_ROWS && t->cursor_x < TERM_COLS) {
        virtio_gpu_fill_rect(
            (uint32_t)(base_x + t->cursor_x * FONT_W),
            (uint32_t)(base_y + t->cursor_y * FONT_H + 14),
            FONT_W, 2, COLOR_CURSOR);
    }

    t->dirty = 0;
}

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * gui_init - Initialize the GUI subsystem
 *
 * Sets up the GPU, creates the initial terminal window,
 * and draws the desktop.
 */
int gui_init(void)
{
    memset(&g_gui, 0, sizeof(g_gui));

    /* Initialize GPU */
    if (virtio_gpu_init() != 0) {
        printk("[GUI] No GPU available, falling back to serial console\n");
        return -1;
    }

    g_gui.screen_width  = (int)g_gpu.width;
    g_gui.screen_height = (int)g_gpu.height;

    /* Draw desktop background */
    virtio_gpu_fill_rect(0, 0, g_gpu.width, g_gpu.height, COLOR_DESKTOP);

    /* Taskbar at the bottom */
    virtio_gpu_fill_rect(0, g_gpu.height - 28, g_gpu.width, 28, COLOR_TASKBAR);

    /* Taskbar label */
    const char *tb = "Shlte OS v0.2";
    for (int i = 0; tb[i]; i++) {
        virtio_gpu_draw_char(8 + (uint32_t)i * FONT_W, g_gpu.height - 22,
                             tb[i], COLOR_TEXT_BRIGHT, COLOR_TASKBAR);
    }

    /* Create terminal window (centered) */
    terminal_t *t = &g_gui.term;
    memset(t, 0, sizeof(*t));

    int win_w = TERM_WIDTH + 8;
    int win_h = TERM_HEIGHT + 32;
    int win_x = (g_gui.screen_width - win_w) / 2;
    int win_y = (g_gui.screen_height - 28 - win_h) / 2;

    if (win_x < 0) win_x = 0;
    if (win_y < 0) win_y = 0;

    t->win.x      = (uint32_t)win_x;
    t->win.y      = (uint32_t)win_y;
    t->win.width  = (uint32_t)win_w;
    t->win.height = (uint32_t)win_h;
    memcpy(t->win.title, "Terminal", 9);
    t->win.visible = 1;
    t->win.focused = 1;

    /* Clear terminal buffer */
    for (int row = 0; row < TERM_ROWS; row++) {
        memset(t->buffer[row], ' ', TERM_COLS);
    }

    g_gui.focused_win = &t->win;
    g_gui.win_count = 1;
    g_gui.windows[0] = t->win;
    g_gui.initialized = 1;

    /* Draw the terminal */
    draw_window_border(&t->win);
    term_render();
    virtio_gpu_flush();

    printk("[GUI] Desktop initialized (%dx%d)\n",
           g_gui.screen_width, g_gui.screen_height);
    return 0;
}

/**
 * gui_draw_desktop - Redraw the desktop background
 */
void gui_draw_desktop(void)
{
    if (!g_gui.initialized) return;
    virtio_gpu_fill_rect(0, 0, g_gpu.width, g_gpu.height, COLOR_DESKTOP);
    virtio_gpu_fill_rect(0, g_gpu.height - 28, g_gpu.width, 28, COLOR_TASKBAR);
}

/**
 * gui_draw_window - Redraw a window (border + contents)
 */
void gui_draw_window(window_t *win)
{
    if (!g_gui.initialized || !win || !win->visible) return;
    draw_window_border(win);
    if (win == &g_gui.term.win) {
        term_render();
    }
}

/**
 * gui_flush - Flush all pending drawing to the display
 */
void gui_flush(void)
{
    if (!g_gui.initialized) return;
    virtio_gpu_flush();
}

/**
 * gui_term_putchar - Write a character to the terminal
 */
void gui_term_putchar(char c)
{
    if (!g_gui.initialized) return;
    terminal_t *t = &g_gui.term;

    if (c == '\n') {
        gui_term_newline();
        return;
    }
    if (c == '\r') {
        t->cursor_x = 0;
        return;
    }
    if (c == '\b') {
        gui_term_backspace();
        return;
    }
    if (c == '\t') {
        for (int i = 0; i < 4; i++) gui_term_putchar(' ');
        return;
    }
    if (c < ' ') return;  /* Skip other control chars */

    t->buffer[t->cursor_y][t->cursor_x] = c;
    t->cursor_x++;

    if (t->cursor_x >= TERM_COLS) {
        t->cursor_x = 0;
        t->cursor_y++;
    }
    if (t->cursor_y >= TERM_ROWS) {
        term_scroll_up();
        t->cursor_y = TERM_ROWS - 1;
    }

    t->dirty = 1;
}

/**
 * gui_term_write - Write a string to the terminal
 */
void gui_term_write(const char *s)
{
    while (*s) {
        gui_term_putchar(*s++);
    }
}

/**
 * gui_term_backspace - Handle backspace in terminal
 */
void gui_term_backspace(void)
{
    terminal_t *t = &g_gui.term;
    if (t->cursor_x > 0) {
        t->cursor_x--;
        t->buffer[t->cursor_y][t->cursor_x] = ' ';
    } else if (t->cursor_y > 0) {
        t->cursor_y--;
        t->cursor_x = TERM_COLS - 1;
        t->buffer[t->cursor_y][t->cursor_x] = ' ';
    }
    t->dirty = 1;
}

/**
 * gui_term_newline - Move cursor to next line
 */
void gui_term_newline(void)
{
    terminal_t *t = &g_gui.term;
    t->cursor_x = 0;
    t->cursor_y++;
    if (t->cursor_y >= TERM_ROWS) {
        term_scroll_up();
        t->cursor_y = TERM_ROWS - 1;
    }
    t->dirty = 1;
}

/**
 * gui_update - Poll input and redraw if needed
 *
 * Should be called periodically from the kernel main loop
 * or from a dedicated GUI service process.
 */
void gui_update(void)
{
    if (!g_gui.initialized) return;

    /* Poll input devices for new events */
    int events = virtio_input_poll();

    /* Process keyboard events */
    virtio_input_event_t ev;
    while (input_kbd_read_event(&ev) == 0) {
        if (ev.type == EV_KEY && ev.value == 1) {  /* Key press only */
            int shift = g_input.keys_pressed[KEY_LEFTSHIFT] ||
                        g_input.keys_pressed[KEY_RIGHTSHIFT];
            char c = input_keycode_to_ascii(ev.code, shift);
            if (c) {
                gui_term_putchar(c);
            }
            /* Special keys */
            if (ev.code == KEY_UP)    { /* cursor up - could scroll history */ }
            if (ev.code == KEY_DOWN)  { /* cursor down */ }
            if (ev.code == KEY_ESC)   { /* escape */ }
        }
        events--;
    }

    /* Drain mouse events */
    virtio_input_event_t mev;
    while (input_mouse_read_event(&mev) == 0) {
        events--;
    }

    /* Redraw if terminal changed */
    if (g_gui.term.dirty) {
        draw_window_border(&g_gui.term.win);
        term_render();
        virtio_gpu_flush();
    }

    /* Redraw mouse cursor */
    int mx, my, mb;
    input_get_mouse(&mx, &my, &mb);
    /* Clamp to screen */
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= g_gui.screen_width)  mx = g_gui.screen_width - 1;
    if (my >= g_gui.screen_height) my = g_gui.screen_height - 1;
}
