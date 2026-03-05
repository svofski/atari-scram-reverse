#include <wchar.h>
#include <ncurses.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "ants.h"
#include "devices.h"

#define WIDTH 160
#define HEIGHT 96

uint8_t bitmap[WIDTH * HEIGHT];

int plot_color = 3;
int plot_x, plot_y;

// 4 colors, 4 pixel combos

#define C0 0
#define C1 1
#define C2 2
#define C3 3

#define NCOLORS 8

int colormap[NCOLORS] = {
    COLOR_BLUE + 8, 
    COLOR_YELLOW, 
    COLOR_GREEN + 8, 
    COLOR_BLACK, 
    COLOR_MAGENTA, //COLOR_CYAN,   -- turbine
    COLOR_YELLOW + 8, 
    COLOR_GREEN, //COLOR_RED,      -- power "temp"
    COLOR_WHITE
};


#define LEGEND_NORMAL 66
#define LEGEND_INV    76

WINDOW * dash;

WINDOW * create_window(int height, int width, int starty, int startx)
{
    WINDOW *win = newwin(height, width, starty, startx);
    //box(win, 0, 0);
    init_pair(66, COLOR_WHITE, COLOR_BLUE);

    init_pair(76, COLOR_BLUE,  COLOR_WHITE);  // inverted
    wbkgd(win, COLOR_PAIR(LEGEND_NORMAL));
    wclear(win);
    wrefresh(win);
    return win;
}

void destroy_window(WINDOW  *win)
{
    delwin(win);
}

int getpair(int c1, int c2)
{
    if (c1 == 0 && c2 == 0) {
        return NCOLORS * NCOLORS;
    }
    return c2 * NCOLORS + c1;
}

void init_colors()
{
    for (int c2 = 0; c2 < NCOLORS; ++c2) {
        for (int c1 = 0; c1 < NCOLORS; ++c1) {
            init_pair(getpair(c1, c2), colormap[c1], colormap[c2]);
        }
    }
}

void update_cell(int x, int y)
{
    int screen_y = y / 2;

    int c1 = bitmap[x + (2 * screen_y + 0) * WIDTH];
    int c2 = bitmap[x + (2 * screen_y + 1) * WIDTH];

    attron(COLOR_PAIR(getpair(c1, c2)));
    mvprintw(screen_y, x, "▀");
}

void color(int c)
{
    plot_color = c;
}

void plot(int x, int y) 
{
    bitmap[x + y * WIDTH] = plot_color;
    update_cell(x, y);
    plot_x = x;
    plot_y = y;
}

void drawto(int x, int y)
{
#if 0
    int dy = y - plot_y;
    int dx = x - plot_x;

    int ystep = dy < 0 ? -1 : dy > 0 ? 1 : 0;
    int xstep = dx < 0 ? -1 : dx > 0 ? 1 : 0;

    //fprintf(stderr, "dx=%d,dy=%d, xstep=%d, ystep=%d", dx, dy, xstep, ystep);

    if (abs(dy) > abs(dx)) {
        int xx = plot_x * 1000;
        int py = plot_y;
        dx = dx * 1000 / abs(dy);

        for (int i = 0; i <= abs(dy); ++i, py += ystep) {
            plot(xx / 1000, py);
            xx += dx;
        }
    }
    else {
        int yy = plot_y * 1000;
        int px = plot_x;
        if (dx != 0) {
            dy = dy * 1000 / abs(dx);
        }
        for (int i = 0; i <= abs(dx); ++i, px += xstep) {
            int py = yy / 1000;
            plot(px, py);
            yy += dy;
        }
    }
#else
    int x0 = plot_x;
    int y0 = plot_y;

    int dx = abs(x - x0);
    int dy = abs(y - y0);
    int sx = (x0 < x) ? 1 : -1;
    int sy = (y0 < y) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        plot(x0, y0);
        if (x0 == x && y0 == y) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }

    // Update cursor to new position like DRAWTO does
    //plot_x = x;
    //plot_y = y;
#endif
}

void setcursorx(uint8_t x)
{
    int y = getcury(dash);
    wmove(dash, y, x);
}

void setcursory(uint8_t y)
{
    int x = getcurx(dash);
    wmove(dash, y, x);
}

void cursxy(uint8_t x, uint8_t y)
{
    wmove(dash, y, x);
}

void print(const char *s)
{
    wattron(dash, COLOR_PAIR(LEGEND_NORMAL));
    wprintw(dash, "%s", s);
}

void print_inv(const char *s)
{
    wattron(dash, COLOR_PAIR(LEGEND_INV));
    wprintw(dash, "%s", s);
}

void print_int(int n)
{
    wprintw(dash, "%d", n);
}

#define STICK_HOLD_TIME    2
#define STICK_HOLDOFF_TIME 2

int stick_dir = 15;
int stick_trig = 1;
int stick_hold = 0;
int stick_holdoff = 0;

uint8_t jkbits = 0;

enum {
    JKBIT_UP    = 1,
    JKBIT_DOWN  = 2,
    JKBIT_LEFT  = 4,
    JKBIT_RIGHT = 8,
};

void poll_input()
{
    int ch = getch();
    if (ch != ERR) {
        switch (ch) {
            case KEY_UP:
                //stick_dir = 14;
                jkbits |= JKBIT_UP;
                stick_holdoff = stick_holdoff ? stick_holdoff : STICK_HOLDOFF_TIME;
                //stick_hold = STICK_HOLD_TIME;
                break;
            case KEY_DOWN:
                jkbits |= JKBIT_DOWN;
                stick_holdoff = stick_holdoff ? stick_holdoff : STICK_HOLDOFF_TIME;
                //stick_hold = STICK_HOLD_TIME;
                break;
            case KEY_LEFT:
                jkbits |= JKBIT_LEFT;
                stick_holdoff = stick_holdoff ? stick_holdoff : STICK_HOLDOFF_TIME;
                //stick_hold = STICK_HOLD_TIME;
                break;
            case KEY_RIGHT:
                jkbits |= JKBIT_RIGHT;
                stick_holdoff = stick_holdoff ? stick_holdoff : STICK_HOLDOFF_TIME;
                //stick_hold = STICK_HOLD_TIME;
                break;

            case KEY_PPAGE:
            case KEY_HOME:
                stick_dir = ASTICK_N;
                stick_trig = 0;
                stick_hold = STICK_HOLD_TIME;
                stick_holdoff = 0;
                break;
            case KEY_NPAGE:
            case KEY_END:
                stick_dir = ASTICK_S;
                stick_trig = 0;
                stick_hold = STICK_HOLD_TIME;
                stick_holdoff = 0;
                break;
        }
    }
    if (stick_holdoff && !--stick_holdoff) {
        switch (jkbits) {
            case JKBIT_UP:                  stick_dir = ASTICK_N; break;
            case JKBIT_DOWN:                stick_dir = ASTICK_S; break;
            case JKBIT_LEFT:                stick_dir = ASTICK_W; break;
            case JKBIT_RIGHT:               stick_dir = ASTICK_E; break;
            case JKBIT_UP|JKBIT_LEFT:       stick_dir = ASTICK_NW; break;
            case JKBIT_UP|JKBIT_RIGHT:      stick_dir = ASTICK_NE; break;
            case JKBIT_DOWN|JKBIT_LEFT:     stick_dir = ASTICK_SW; break;
            case JKBIT_DOWN|JKBIT_RIGHT:    stick_dir = ASTICK_SE; break;
            default:                        stick_dir = 15; break;// jam
        }
        stick_hold = STICK_HOLD_TIME;
        jkbits = 0;
    }

    if (stick_hold > 0) {
        --stick_hold;
        if (stick_hold == 0) {
            stick_dir = 15;
            stick_trig = 1;
        }
    }
    if (stick_hold > 0) 
    fprintf(stderr, "poll_input: jkbits=%1x stick_dir=%d, holdoff=%d hold=%d\n",
            jkbits, stick_dir, stick_holdoff, stick_hold);
}

uint8_t stick(uint8_t n)
{
    return stick_dir;
}

uint8_t strig(uint8_t n)
{
    return stick_trig;
}

void graphics_init()
{
    setlocale(LC_ALL, "");
    // Initialize ncurses
    initscr();              // Start curses mode
    cbreak();               // Disable line buffering
    noecho();               // Don't echo() while we do getch
    nodelay(stdscr, TRUE);              // don't block
    keypad(stdscr, TRUE);   // Enable F1, F2, arrow keys etc.
    curs_set(0);            // Hide the cursor

    //// Optional: initialize colors
    //if (has_colors()) {
    //    start_color();
    //    init_pair(1, COLOR_RED, COLOR_BLACK);
    //    attron(COLOR_PAIR(1));
    //    mvprintw(0, 0, "Hello with color!");
    //    attroff(COLOR_PAIR(1));
    //} else {
    //    mvprintw(0, 0, "Hello without color!");
    //}


    start_color();
    init_colors();


    color(0);
    for (int i = 0; i < 60; ++i) {
        plot(0, i);
        drawto(120, i);
    }

    dash = create_window(4, 40, 40, 108);

#if 0
    //color(1);
    //plot(0, 0);
    //plot(0, 1);

    for (int i = 0; i < 8; ++i) {
        color(i);
        plot(i, 0);
        color(7-i);
        plot(i, 1);
    }

    color(3);
    plot(10, 10);
    //drawto(10, 11);
    drawto(30, 30);

    color(4);

    drawto(10, 40);

    color(5);
    drawto(10, 42);

    refresh();              // Print it on to the real screen
    getch();                // Wait for user input
#endif
    // Clean up
    //endwin();               // End curses mode
}

int n;

void graphics_refresh()
{
    ++n;
    refresh();
    redrawwin(dash);
    wrefresh(dash);
}

void graphics_shutdown()
{
    refresh();
    getch();
    endwin();
}

void input_poll()
{
}

void marching_ants_h(marching_ants_t * state, uint8_t x1, uint8_t x2, uint8_t y, int8_t dir)
{
    state->orientation = MANTS_HORZ;
    state->direction = dir;
    state->speed = 0;
    state->accu = 0;
    state->pos = dir < 0 ? 0 : 3;
    state->x1 = x1;
    state->x2 = x2;
    state->y1 = state->y2 = y;
    state->speed = 1;
    state->div = 0; // all blue
}

void marching_ants_v(marching_ants_t * state, uint8_t x, uint8_t y1, uint8_t y2, int8_t dir)
{
    state->orientation = MANTS_VERT;
    state->direction = dir;
    state->speed = 0;
    state->accu = 0;
    state->pos = dir < 0 ? 0 : 3;

    state->y1 = y1;
    state->y2 = y2;
    state->x1 = state->x2 = x;
    state->speed = 1;
}

void marching_ants_step(marching_ants_t * state)
{
    state->accu += state->speed;
    if (state->accu > MAX_ANTS_SPEED) {
        state->accu -= MAX_ANTS_SPEED;

        if (state->direction > 0) {
            --state->pos;
            if (state->pos == -1) {
                state->pos = 3;
            }
        }
        else {
            ++state->pos;
            if (state->pos == 4) {
                state->pos = 0;
            }
        }

        uint8_t u1 = state->orientation == MANTS_HORZ ? state->x1 : state->y1;
        uint8_t u2 = state->orientation == MANTS_HORZ ? state->x2 : state->y2;
        for (uint8_t u = u1, k = state->pos; u <= u2; ++u) {
            if (state->speed > 0 && k == 0) {
                color(3); // black
            }
            else {
                 // background -- should in fact reflect steam level in some cases
                if ((u - u1) >= state->div) {
                    color(0); // blue
                }
                else {
                    color(5); // light yellow
                }

            }
            k += 1;
            if (k == 4) {
                k = 0;
            }

            if (state->orientation == MANTS_HORZ) {
                plot(u, state->y1);
            }
            else {
                plot(state->x1, u);
            }
        }
    }
}

void marching_ants_set_speed(marching_ants_t * state, uint8_t speed)
{
    state->speed = speed;
    if (speed == 0) {
        state->accu = MAX_ANTS_SPEED + 1; // force refresh
        marching_ants_step(state);
    }
}

void marching_ants_set_div(marching_ants_t * state, uint8_t div)
{
    //if (state->div != div) fprintf(stderr, "marching div=%d\n", div);
    state->div = div;
    if (state->speed == 0) {
        state->accu = MAX_ANTS_SPEED + 1; // force refresh
        marching_ants_step(state);
    }
}

void fill(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2)
{
    for (uint8_t y = y1; y <= y2; ++y) {
        for (uint8_t x = x1; x <= x2; ++x) {
            plot(x, y);
        }
    }
}



