#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include <stdio.h>

#include "devices.h"
#include "util.h"
#include "ants.h"

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

typedef enum {
  DEV_LO = 1,
  DEV_REACTOR = 1,
  DEV_HPI_VALVE = 2,
  DEV_PRESSURIZER_VALVE = 3,
  DEV_AUX_FEEDWATER_VALVE = 4,
  // these devices have a fat number on them

  DEV_RCS_PUMPS = 5,
  DEV_HPI_PUMPS = 6,
  DEV_MAIN_FEEDWATER_PUMPS = 7,
  DEV_AUX_FEEDWATER_PUMPS = 8,
  DEV_CS_PUMPS = 9,
  DEV_RISK = 10,
  DEV_HI = 10
} device_t;

typedef enum {
  TEMPS_LO = 1,
  TEMPS_REACTOR = 1,
  TEMPS_RCS_COLDLEG = 2,
  TEMPS_RCS_HOTLEG = 3,
  TEMPS_FW_COLD = 4,
  TEMPS_FW_HOT = 5,
  TEMPS_CS_HOT = 6,
  TEMPS_CS_COLD = 7,
  TEMPS_GEN_POWER = 8,    // not exactly a temp
  TEMPS_HI = 8
} temps_t;


const uint8_t devset_init[11] = {
    0,    // padding

    9,    // control rods fully withdrawn
    0,    // HPI valve closed
    0,    // pressurizer valve closed
    0,    // aux feedwater valve closed
    4,    // RCS pumps 4
    0,    // HPI pumps 0
    2,    // main feedwater pumps 2
    0,    // aux feedwater pumps
    2,    // circulating system pumps 2 (cooling tower)
    0     // risk 0
};

    
const int8_t devset_default[11] = {0, 9,0,0,0,4,0,2,0,2,0};

const int8_t ulim_default[11] = {
    0,    // padding
    9,1,1,1,4,4,2,3,2,9
};

const uint8_t devx[11] = {0, 0, 31, 58, 77, 46, 33, 96, 95, 109, 137};
const uint8_t devy[11] = {0, 0, 60, 27, 67, 60, 78, 60, 78, 60,  80};

const uint8_t curofs_x[11] = {0, 18, -4, -4, -4, 14, -6,   2,  12,   8, -1};
const uint8_t curofs_y[11] = {0, 8,   2,  2,  2, -8, -4, -15, -10, -13, -6};

int8_t rdng[11];      // actual device state
int8_t devset[11];    // 
int8_t ulim[11];
int8_t max[11];

float temp[9];
float oldtemp[9];

uint8_t flag = 0; // FLAG

int8_t selected_device = DEV_REACTOR;

uint8_t tempxy[9*2] = {
        0, 0,
//       core     rcs cold  rcs hot   fw cold
	27,40,  27,54,  53,10,  95,54,
//       fw hot    cs hot   cs cold   generator power output
        95,10,  125,32, 149,68, 129,17};

//uint8_t tempx[9] = {};
//uint8_t tempy[9] = {};

static const char * const trn_blank =         "              ";
static const char * const trn_steam_voiding = "STEAM VOIDING!";
static const char * const trn_cold_shutdown = "COLD SHUTDOWN";
static const char * const trn_meltdown =      "  MELTDOWN!  ";

const char * transparant = trn_blank;

#define TRANSPARANT_X 15
#define TRANSPARANT_Y 0

typedef struct {
    float REACTORPOWER;
    float TR;   // temp reactor
    float TP;
    float TS;
    float TC;   // temp in circulating system / hot leg
    float Q2;
    float Q3;
    float Q5;
    float Q6;
    float PP;   // RCS pressure
    float VP;
    float LS;   // water storage tank level (aux feedwater sys)
    float LI;
    float S1;
    float CTTC;
    float S2;
    float S3;
    float S4;
    float S5;
    float S6;
    float BS;
    float IP;
    float IS;
    float TURBPOW;    // turbine power
    float PQ;
    float STBUB;      // steam bubble (yellow, top of pressurizer)
    float TCRIT;      // boiling temp (bottom display) @ 4, 3
    uint8_t PRZRLVL;  // floor(STBUB/PP) 0..23  pressurizer level
} reactor_t;

typedef struct {
    uint8_t BADLUCK;
    uint8_t WRKRS; 
    float NETNRG; // net energy
    float ALVL;   // steam level in the heat exchanger secondary, see usr(4737) -- reasonable values [2..8)

    uint8_t STOPPED;
    int16_t STEP;
    uint8_t CORE_BRIGHTNESS;
} sim_t;

const sim_t sim_defaults = {
    0,
    80, 
    0,
    2,  // ALVL
    0,  // STOPPED

    0,  // STEP
    7,  // CORE_BRIGHTNESS
};

reactor_t reactor;
sim_t sim;

marching_ants_t rcs_ants[5];
marching_ants_t fw_ants[6];
marching_ants_t pressurizer_ants[2];
marching_ants_t hpi_ants[2];
marching_ants_t aux_fw_ants[3];
marching_ants_t cs_ants[5];


int16_t usr(uint16_t addr, int16_t arg)
{
    return 0;
}

#define TINY (0.03)

static const reactor_t reactor_defaults = {
	/* REACTORPOWER = */ 2700,
	/* TR =           */ 0,
	/* TP =           */ 587,
	/* TS =           */ 526,
	/* TC =           */ 95,
	/* Q2 =           */ 2684,
	/* Q3 =           */ 2670,
	/* Q5 =           */ 16,
	/* Q6 =           */ 14.3,
	/* PP =           */ 2260,
	/* VP =           */ 5,
	/* LS =           */ 67,
	/* LI =           */ 67,
	/* S1 =           */ 40,
	/* CTTC =         */ 40,
	/* S2 =           */ 43.5,
	/* S3 =           */ 6.2,
	/* S4 =           */ 60,
	/* S5 =           */ TINY,
	/* S6 =           */ TINY,
	/* BS =           */ 0,
	/* IP =           */ 0,
	/* IS =           */ 0,
	/* TURBPOW =      */ 953,
	/* PQ =           */ 0.5,
	/* STBUB =        */ 22000,
        /* TCRIT =        */ 0,
};

// draw sprite of valve dev at its designated location
void draw_valve(uint8_t dev);


void __attribute__((weak)) sound(int a, int b, int c, int d)
{
}

void __attribute__((weak)) graphics_init()
{
    
}

void __attribute__((weak)) graphics_refresh()
{
    
}


void __attribute__((weak)) graphics_shutdown()
{
    
}

void __attribute__((weak)) color(int c)
{
}

void __attribute__((weak)) plot(int x, int y) 
{
}

void __attribute__((weak)) drawto(int x, int y)
{
}

void __attribute__((weak)) setcursorx(uint8_t x)
{
}

void __attribute__((weak)) setcursory(uint8_t y)
{
}

void __attribute__((weak)) cursxy(uint8_t x, uint8_t y)
{
}

void __attribute__((weak)) print(const char *s)
{
    printf("%s", s);
}

void __attribute__((weak)) print_inv(const char *s)
{
    printf("%s", s);
}

void __attribute__((weak)) print_int(int n)
{
    printf("%d", n);
}

void __attribute__((weak)) poke(uint16_t addr, uint8_t value)
{
}

uint8_t __attribute__((weak)) peek(uint16_t addr)
{
    return 0;
}

uint8_t __attribute__((weak)) stick(uint8_t n)
{
    return 15;
}

uint8_t __attribute__((weak)) strig(uint8_t n)
{
    return 1;
}

float rnd(int n)
{
    return 1/33; // defo random
}

void __attribute__((weak)) poll_input()
{
}

// 0..5 (normally 0..4)
void enable_animation_hpi_valve(uint8_t speed)
{
    //usr(4923, a);
    for (uint8_t i = 0; i < sizeof(hpi_ants)/sizeof(hpi_ants[0]); ++i) {
        marching_ants_set_speed(&hpi_ants[i], speed);
    }
}

// 0..1
void enable_animation_pressurizer_valve(uint8_t speed)
{
    // usr(896)
    for (uint8_t i = 0; i < sizeof(pressurizer_ants)/sizeof(pressurizer_ants[0]); ++i) {
        marching_ants_set_speed(&pressurizer_ants[i], speed);
    }
}

// L8670  0..5 (normally 0..4)
void enable_animation_aux_feedwater_valve(uint8_t speed)
{
    // usr(4882, a);
    for (uint8_t i = 0; i < sizeof(aux_fw_ants)/sizeof(aux_fw_ants[0]); ++i) {
        marching_ants_set_speed(&aux_fw_ants[i], speed);
    }
}

void enable_animation_rcs_pumps(uint8_t speed)
{
    for (uint8_t i = 0; i < sizeof(rcs_ants)/sizeof(rcs_ants[0]); ++i) {
        marching_ants_set_speed(&rcs_ants[i], speed);
    }
}

void enable_animation_feedwater_pumps(uint8_t speed)
{
    for (uint8_t i = 0; i < sizeof(fw_ants)/sizeof(fw_ants[0]); ++i) {
        marching_ants_set_speed(&fw_ants[i], speed);
    }
}

void enable_animation_cs_pumps(uint8_t speed)
{
    for (uint8_t i = 0; i < sizeof(cs_ants)/sizeof(cs_ants[0]); ++i) {
        marching_ants_set_speed(&cs_ants[i], speed);
    }
}

// L8600 (DEV_HPI_VALVE = 2)
void disp_hpi_valve()
{
    uint8_t speed = rdng[DEV_HPI_VALVE] * rdng[DEV_HPI_PUMPS];
    if (reactor.LI > 77) {
        speed = 0;
    }
    enable_animation_hpi_valve(speed);
    if (speed == 0) {
        draw_valve(DEV_HPI_VALVE);
    }
    if (rdng[DEV_HPI_VALVE]) {
        enable_animation_hpi_valve(speed);  // draw open valve even if speed 0
    }
}

// L8630
void disp_pressurizer_valve()
{
    uint8_t speed = rdng[DEV_PRESSURIZER_VALVE];
    //a = usr(896, a);
    enable_animation_pressurizer_valve(speed);
    if (speed == 0) {
        draw_valve(DEV_PRESSURIZER_VALVE);
    }
}

// L8660
void disp_aux_feedwater_valve()
{
    uint8_t speed = rdng[DEV_AUX_FEEDWATER_VALVE] * rdng[DEV_AUX_FEEDWATER_PUMPS];
    if (reactor.LS > 77) {
        speed = 0;
    }
    enable_animation_aux_feedwater_valve(speed);
    if (speed == 0) {
        draw_valve(DEV_AUX_FEEDWATER_VALVE);
    }
    if (rdng[DEV_AUX_FEEDWATER_VALVE]) {
        enable_animation_aux_feedwater_valve(speed); // draw open valve even if speed 0
    }

}

// device 5, L8340
void disp_rcs_pumps(uint8_t func)
{
    //uint8_t a = usr(572, func);
    enable_animation_rcs_pumps(func);
}

// L8350
void disp_hpi_pumps(uint8_t func)
{
    disp_hpi_valve();
}

// L8360
void disp_main_feedwater_pumps(uint8_t func)
{
    //uint8_t a = usr(1024, func);
    enable_animation_feedwater_pumps(func);
}

// L8370
void disp_aux_feedwater_pumps(uint8_t func)
{
    disp_aux_feedwater_valve();
}

// L8380
void disp_cs_pumps(uint8_t func)
{
    //uint8_t a = usr(1075, func);
    enable_animation_cs_pumps(func);
}

// L8390
void disp_risk(uint8_t func)
{
    sim.BADLUCK = func;
    sim.NETNRG = 0;
}

void draw_fat_digit(uint8_t num, uint8_t x, uint8_t y, uint8_t ink, uint8_t paper)
{
    // L4
    color(ink);
    for(int i = 0; i < 3; ++i) {
        plot(x + i, y);
        drawto(x + i, y - 4);
    }
    color(paper);
    switch (num) {
        case 0:
            plot(x + 1, y - 1);
            drawto(x + 1, y - 3);
            break;
        case 1:
            plot(x, y - 1);
            drawto(x, y - 3);
            plot(x + 2, y - 1);
            drawto(x + 2, y - 4);
            break;
        case 2:
            plot(x, y - 3);
            drawto(x + 1, y - 3);
            plot(x + 1, y - 1);
            drawto(x + 2, y - 1);
            break;
        case 3:
            plot(x, y - 1);
            drawto(x, y - 3);
            plot(x + 1, y - 1);
            plot(x + 1, y - 3);
            break;
        case 4:
            plot(x, y);
            plot(x + 1, y);
            drawto(x + 1, y - 4);
            plot(x, y - 1);
            plot(x + 2, y - 4);
            color(ink);
            plot(x + 1, y - 2);
            break;
        case 5:
            plot(x, y - 1);
            plot(x + 1, y - 1);
            plot(x + 1, y - 3);
            plot(x + 2, y - 3);
            break;
        case 6:
            plot(x + 1, y - 1);
            plot(x + 1, y - 3);
            plot(x + 2, y - 3);
            break;
        case 7:
            plot(x, y);
            drawto(x, y - 3);
            plot(x + 1, y);
            drawto(x + 1, y - 3);
            break;
        case 8:
            plot(x + 1, y - 1);
            plot(x + 1, y - 3);
            break;
        case 9:
            plot(x, y - 1);
            plot(x + 1, y - 1);
            plot(x + 1, y - 3);
            break;
        case 10:
        default:
            plot(x, y);
            drawto(x, y - 4);
            plot(x + 1, y);
            drawto(x + 1, y - 4);
            plot(x + 2, y);
            drawto(x + 2, y - 4);
            break;
    }
}

void set_core_brightness(uint8_t n)
{
}

// draw reactor core and flickering
void draw_core()
{
    static const uint8_t flickermap[] = {5,1,5,2,5,1,5,4};
    static uint8_t frm = 0;
    ++frm;
    if ((frm & 1) == 1) return;
    color(flickermap[(frm >> 1) & 7]);

    //color(1);
    static const uint8_t xx[6] = {10,12,16,20,24, 26};
    for (uint8_t x = 0; x < sizeof(xx)/sizeof(xx[0]); ++x) {
        plot(xx[x], 24);
        drawto(xx[x], 32);

        plot(xx[x] + 1, 24);
        drawto(xx[x] + 1, 32);
    }
}

// L8110, also fallthrough from stick handlers
// func = rod position 0..9
// a = 0 or 2
void draw_control_rods(uint8_t func, uint8_t a)
{
    // 3 control rods
    for (uint8_t j = 14; j <= 22; j += 4) {
        color(0);
        plot(j,     23 - func + a * 5);     // 14, 33   
        plot(j + 1, 23 - func + a * 5);     // 15, 33
        set_core_brightness(floor(1.5 * func));
        color(1);
        plot(j,     32 - func - a * 4);     // 14, 24
        plot(j + 1, 32 - func - a * 4);     // 15, 24
    }
    sound(0, 0, 0, 0);
}

// L8200: set device = func
void set_device(uint8_t device, uint8_t func, uint8_t a)
{
    assert(device >= 1 && device <= 9);

    if (device == DEV_REACTOR) {
        reactor.REACTORPOWER = 270 * (func + 1);
        sound(0, 100 - 10 * func, 8, 8);
        draw_control_rods(func, a);
        return;
    }
    else if (device > DEV_AUX_FEEDWATER_VALVE) { // pumps & risk
        // L8300
        sound(0, 14 - func, 2, 8);
        uint8_t num = func;
        uint8_t ink = 3;
        uint8_t paper = 1;
        if (device == DEV_RISK) {
            ink = 1;
            paper = 3;
        }
        // L8310
        uint8_t x = devx[device] + 4;
        uint8_t y = devy[device] - 3;
        draw_fat_digit(num, x, y, ink, paper);
        sound(0, 0, 0, 0);
        // L8320 goto 8290 + device * 10, device = [5..10]
        switch (device) {
            case DEV_RCS_PUMPS:
                disp_rcs_pumps(func);
                break;
            case DEV_HPI_PUMPS:
                disp_hpi_pumps(func);
                break;
            case DEV_MAIN_FEEDWATER_PUMPS:
                disp_main_feedwater_pumps(func);
                break;
            case DEV_AUX_FEEDWATER_PUMPS:
                disp_aux_feedwater_pumps(func);
                break;
            case DEV_CS_PUMPS:
                disp_cs_pumps(func);
                break;
            case DEV_RISK:
                disp_risk(func);
                break;
        }
        return;
    }

    // else valves:
    // DEV_HPI_VALVE = 2,
    // DEV_PRESSURIZER_VALVE = 3,
    // DEV_AUX_FEEDWATER_VALVE = 4,

    if (ulim[device] == 1) {
        devset[device] = func;
    }

    if (func == 1) {
        sound(0, 50, 10, 8);
        color(0);
    }
    else if (func == 0) {
        sound(0, 100, 10, 8);
        color(1);
    }

    // device = [2..4], valves
    switch (device) {
        case DEV_HPI_VALVE:
            disp_hpi_valve();
            break;
        case DEV_PRESSURIZER_VALVE:
            disp_pressurizer_valve();
            break;
        case DEV_AUX_FEEDWATER_VALVE:
            disp_aux_feedwater_valve();
            break;
    }
    sound(0, 0, 0, 0);
    plot(devx[device] + 3, devy[device]);
    drawto(devx[device] + 3, devy[device] + 5);
}

// L8910
void limit_device(uint8_t device)
{
    devset[device] = ulim[device];
    if (devset[device] > rdng[device]) {
        devset[device] = rdng[device];
    }
}

// L8100
void adjust_device(uint8_t device, uint8_t func, uint8_t a)
{
    rdng[device] = func;
    if (device >= DEV_RCS_PUMPS) {
        limit_device(device);
    }
    set_device(device, func, a);
}


// L8028, joy button pressed
// up: open valve, add +1 pump
// down: close valve, sub -1 pump
// left/right: repair device using workers
void button_pressed(uint8_t device, uint8_t func)
{
    if (stick(0) == ASTICK_S && func > 0) {   // joy down
        adjust_device(device, func - 1, 0); // -> L8100
    }
    else if (stick(0) == ASTICK_N && func < max[device]) { // up
        adjust_device(device, func + 1, 2); // -> L8100
    }
    else if (stick(0) == ASTICK_W || stick(0) == ASTICK_E) { // right(7) or left(11)
        // L8700
        ///... 
        if (flag == 1 || device == DEV_REACTOR || device == DEV_RISK) {
            // do nothing if some flag
        }
        else {
            flag = 1;
            cursxy(21, 3);
            uint8_t a = 0, j = 0;
            const char * msg = "";

            if (sim.WRKRS < 5) {
                msg = "NO WORKERS";
                j = 6;
                a = 100;
            }
            else {
                sim.WRKRS -= 5;
                if (ulim[device] == max[device]) {
                    msg = "  WRONG!  ";
                    j = 2;
                    a = 35;
                }
                else {
                    // L8725
                    ulim[device] = ulim[device] + 1;
                    limit_device(device);
                    msg = "  RIGHT!  ";
                    j = 10;
                    a = 50;
                }
            }
            // L8730
            print_int(sim.WRKRS);
            print(" ");
            for (uint8_t z = 1; z <= 3; ++z) {
                sound(0, a, j, 8);
                cursxy(16, 1);
                print_inv(msg);
                usleep(10000);
                sound(0, 0, 0, 0);
                print("          ");
                usleep(10000);
            }
            
        }
    }
}

uint8_t next_device(uint8_t jdir)
{
    //  5    6    7    8   9    10   11   12   13   14
    // SE   NE    E   xx   SW   NW    W   xx    S    N  
    static const uint8_t dev_jumptab[] = {
        5   ,1   ,3   ,1   ,1   ,1   ,1   ,1   ,2   ,1
       ,2   ,3   ,5   ,2   ,2   ,2   ,2   ,2   ,6   ,1
       ,7   ,3   ,3   ,3   ,2   ,1   ,1   ,3   ,5   ,3
       ,4   ,7   ,8   ,4   ,4   ,5   ,6   ,4   ,4   ,3
       ,4   ,5   ,7   ,5   ,6   ,1   ,2   ,5   ,5   ,3
       ,6   ,5   ,4   ,6   ,6   ,6   ,6   ,6   ,6   ,2
       ,10  ,7   ,9   ,7   ,4   ,3   ,5   ,7   ,8   ,7
       ,8   ,9   ,10  ,8   ,8   ,3   ,4   ,8   ,8   ,7
       ,10  ,9   ,9   ,9   ,8   ,9   ,7   ,9   ,8   ,9
       ,10  ,10  ,10  ,10  ,10  ,7   ,8   ,10  ,10  ,9};

    uint8_t best = selected_device;

    if (jdir >= ASTICK_SE && jdir <= ASTICK_N) {
        int index = (selected_device - 1) * 10 + (jdir - 5);
        if (index < 0 || index > sizeof(dev_jumptab)/sizeof(dev_jumptab[0])) {
            fprintf(stderr, "next_device: index error dev=%d index=%d\n",
                    selected_device, index);
        }
        best = dev_jumptab[index];
        fprintf(stderr, "next_device: dev=%d stick=%d next=%d\n",
                selected_device, jdir, best);
    }

    return best;
}

void animate_device_sel(uint8_t clear)
{
    static uint8_t frm = 0;
    if (clear) {
        color(3);
    }
    else {
        color(frm & 7);
    }

    uint8_t x = devx[selected_device] + curofs_x[selected_device];
    uint8_t y = devy[selected_device] + curofs_y[selected_device];
    fill(x, y, x+1, y+1);
    ++frm;
}

// poke 1726 + N5, n
void select_device(uint8_t n)
{
    if (n >= DEV_LO && n <= DEV_HI) {
        animate_device_sel(1);
        selected_device = n;
    }
}


void check_input()
{
    //poll_input();
    // L24
    if (strig(0) == 0) {
        button_pressed(selected_device, rdng[selected_device]);
        return;
    }
    flag = 0;
    uint8_t jdir = stick(0);
    if (jdir != 15) {
        fprintf(stderr, "check_input(): stick=%d\n", jdir);
    }
    if (jdir == 15) {
        return;
    }
    sound(0, 50, 8, 8);
    poke(77, 0);
    // L26 is something crazy
    select_device(next_device(jdir));
}

void sim_init()
{
    sim = sim_defaults;

    uint8_t a = usr(512, 4320);

    reactor = reactor_defaults;
    reactor.TR = 656; // originally CURSY, which was previously set to 656

    memcpy(devset, devset_default, sizeof(devset) / sizeof(devset[0]));
    memcpy(rdng, devset, sizeof(rdng) / sizeof(rdng[0]));
    memcpy(ulim, ulim_default, sizeof(ulim) / sizeof(ulim[0]));
    memcpy(max, ulim, sizeof(max) / sizeof(max[0]));

    // L3050
    for (int dev = DEV_HPI_VALVE; dev <= DEV_CS_PUMPS; ++dev) {
        set_device(dev, rdng[dev], 0);
    }

    // L3060
    for (int func = 0; func <= 9; ++func) {
        draw_control_rods(func, 2);
        graphics_refresh();
        usleep(100000);
    }

    // L3070 -- draw the voids above water in HPI and AUX tanks
    color(3);
    for (int y = 64; y <= 67; ++y) {
        plot(126, y);
        drawto(113, y);
        plot(64, y);
        drawto(51, y);
    }
    // L3080 -- draw the water in HPI and AUX tanks
    color(0);
    for (int y = 68; y <= 77; ++y) {
        plot(126, y);
        drawto(113, y);
        plot(64, y);
        drawto(51, y);
    }

    // L3090 -- void in the quench tank
    color(3);
    for (int i = 36; i <= 45; ++i) {
        plot(51, i);
        drawto(63, i);
    }

    // L3130 -- mystery?
    for (int j = 680; j <= 688; ++j) {
        poke(j, 213);
    }
    for (int j = 689; j <= 719; ++j) {
        poke(j, 0);
    }

    // L7200
    cursxy(0, 0);
    print("  RCS PRESSURE                   RISK");
    draw_fat_digit(0, devx[DEV_RISK] + 4, devy[DEV_RISK] - 3, 1, 3);
    cursxy(0, 2);
    print("  BOILING TEMP    WORKERS   NET ENERGY");
    cursxy(21, 3);
    print("80");
    setcursorx(35);
    print("MWH");
    // goto 100
}

void sim_process_1()
{
    if (rdng[DEV_REACTOR] == 0) {
        reactor.REACTORPOWER = reactor.REACTORPOWER * 0.99;
    }

    float H, A;

    // L110
    H = reactor.S3 * reactor.S4 / (reactor.S3 + reactor.S4);
    A = reactor.S2 * (reactor.S6 + H) / (reactor.S2 + reactor.S6 + H) / reactor.S5;
    reactor.Q2 = A * reactor.REACTORPOWER / (A + 1);
    reactor.Q5 = reactor.REACTORPOWER - reactor.Q2;
    H = H / reactor.S6;
    reactor.Q3 = H * reactor.Q2 / (1 + H);

    // L120
    A = reactor.Q3 * reactor.S1 * reactor.S2 * reactor.S3 * reactor.S4 / 1750000;
    reactor.TURBPOW = reactor.TURBPOW + (A - reactor.TURBPOW) / 4;
    if (reactor.TURBPOW > 999) {
        reactor.TURBPOW = 999;
    }

    // L130
    reactor.Q6 = reactor.Q2 - reactor.Q3;
    float AC = 50 + reactor.Q3 / reactor.S4;

    //gosub N24
    //X check_input();

    float AS = 50 + reactor.Q6 / reactor.S6;
    float AP = 50 + reactor.Q5 / reactor.S5;
    float AR = AP + reactor.REACTORPOWER / reactor.S1;
    reactor.TC = reactor.TC + (AC - reactor.TC) / 24;
    
    // L150
    reactor.TS = reactor.TS + (AS - reactor.TS) / 24;
    reactor.TP = reactor.TP + (AP - reactor.TP ) / 24;
    reactor.TR = reactor.TR + (AR - reactor.TR ) / 24;
    A = (reactor.TP / 3 + 256 ) * reactor.VP;
    reactor.PP = reactor.PP + (A - reactor.PP) / (reactor.PRZRLVL + 1);

    // L160
    if (reactor.PP > 3000) {
        A = (reactor.PP - 2500) / 2000000;
        if (A > reactor.BS) {
            reactor.BS = A;
        }
    }

    // L170
    reactor.VP = reactor.VP - reactor.PP * ( 0.0001 * (devset[DEV_PRESSURIZER_VALVE] + devset[DEV_HPI_VALVE] / 5 ) + reactor.BS ) + 2 * reactor.IP;
    if (reactor.VP < TINY) {
        reactor.VP = TINY;
    }

    // L175
    reactor.PQ = reactor.PQ + reactor.PP * devset[DEV_PRESSURIZER_VALVE] * 5e-005;
    if (reactor.PQ > 3) {
        reactor.PQ = 3;
    }

    // L180
    for (int i = 1; i <= 8; ++i) {
        oldtemp[i] = temp[i];
    }
    reactor.TCRIT = 212 + 33.8 * pow(reactor.PP, 0.33333);
    temp[TEMPS_RCS_HOTLEG] = reactor.TP + (reactor.TR - reactor.TP) / (3 + devset[DEV_RCS_PUMPS] / 4);    // hot leg
    // L190
    temp[TEMPS_RCS_COLDLEG] = reactor.TP - (reactor.TP - reactor.TS) / (1.5 + devset[DEV_RCS_PUMPS] / 4); // cold leg
    temp[TEMPS_FW_COLD] = reactor.TS - (reactor.TS - reactor.TC) / (6 + devset[DEV_MAIN_FEEDWATER_PUMPS] / 2); // feedwater cold
    // L195
    temp[TEMPS_FW_HOT] = temp[TEMPS_RCS_HOTLEG] - devset[DEV_MAIN_FEEDWATER_PUMPS] * ( temp[TEMPS_RCS_HOTLEG] - reactor.TS ) / 16; // feedwater hot

    //X check_input();

    temp[TEMPS_CS_HOT] = reactor.TC; // circulating system hot
    temp[TEMPS_CS_COLD] = reactor.TC - (reactor.TC - 50 ) / ( 1.3 + devset[DEV_CS_PUMPS] / 9 ); // circulating system cold

    // L196
    temp[TEMPS_REACTOR] = reactor.TR;
    temp[TEMPS_GEN_POWER] = reactor.TURBPOW;
}

void draw_temps()
{
    // L198
    setcursorx(4);
    setcursory(3);
    print_int(floor(reactor.TCRIT));
    print(" F  ");
    setcursory(1);
    setcursorx(4);
    print_int(floor(reactor.PP));
    print(" PSI   ");

    // L200
    for (int i = TEMPS_LO; i <= TEMPS_HI; ++i) {
        uint8_t tx = tempxy[i * 2];
        uint8_t y = tempxy[i * 2 + 1];

        int inttemp = floor(temp[i]);
        int2digits(inttemp);

        int numlen = 0;
        uint8_t paper = 0;
        uint8_t ink = 2;
        if (i > TEMPS_RCS_COLDLEG) {
            paper = 3;  // reactor core (light bleu)
            if (i == TEMPS_GEN_POWER) {
                paper = 6;
                ink = 3;
            }
        }

        // L210
        uint8_t x = tx - 20;
        for (int j = sizeof(numbuf) - 4; j < sizeof(numbuf); ++j) {
            x += 4;
            draw_fat_digit(numbuf[j], x, y, ink, paper);
            if (numbuf[j] != 10) {
                ++numlen;
            }
        }

        // L222 -- wipe up/down indicators
        color(paper);
        plot(x + 2, y + 2);
        drawto(tx - 16, y + 2);
        plot(x + 2, y - 6);
        drawto(tx - 16, y - 6);
        color(ink);
        
        // L224 - draw temp decrease
        if (floor(temp[i]) < floor(oldtemp[i])) {
            plot(x + 2, y + 2);
            drawto(tx - 4 * numlen, y + 2);
        }
        else if (floor(temp[i]) > floor(oldtemp[i])) {
            plot(x + 2, y - 6);
            drawto(tx - 4 * numlen, y - 6);
        }

        if (reactor.TR < reactor.TCRIT + 2 || i == 8) {
            // we're good
        }
        else {
            // oopsie alarm
            transparant = trn_steam_voiding;
            //setcursorx(15);
            //setcursory(0);
            if (i & 1 == 0) {
                sound(1, 200, 10, 6);
                //print("STEAM VOIDING!");
            }
            else {
                sound(1, 100, 10, 6);
                //print_inv("STEAM VOIDING!");
            }
        }

        //X check_input();
    }
}

// pressurizer container level
// the pipes are always yellow
void update_pressurizer_level()
{
    color(5);
    fill(35, 24, 45, 24 + MIN(22, reactor.PRZRLVL));
    color(0);
    if (reactor.PRZRLVL < 22) {
        fill(35, 24 + reactor.PRZRLVL, 45, 24 + 22);
    }
    color(5);
    if (reactor.PRZRLVL >= 22) {
        plot(40, 24+23);
    }
}

// the tank next to the pressurizer, separated by PRV
void update_quench_tank()
{
    uint8_t a = floor(48 - reactor.PQ * 4);
    color(0); // blue
    for (uint8_t y = a; y <= 46; ++y) {
        plot  (50 + 1, y);
        drawto(50 + 13, y);
    }
}

void cold_shutdown()
{
    // L600
    transparant = trn_cold_shutdown;
    sim.STOPPED = 1;
}

void meltdown()
{
    // L520..540
    transparant = trn_meltdown;

    // lines 520..540 skipped, meltdown animation
    sim.STOPPED = 1;
}

void update_transparant()
{
    cursxy(15, 0);
    if ((sim.STEP & 1) == 0 || transparant == trn_blank) {
        print(transparant);  // display blank / STEAM VOIDING! / COLD SHUTDOWN / MELTDOWN!
    }
    else {
        print_inv(transparant);
    }
    transparant = trn_blank;
}

void sim_process_2()
{
    // power proportional hum?
    int a = floor(reactor.TURBPOW / 77);
    sound(2, peek(371 + a), 4, 2 * (a < 0 ? -1 : a > 0 ? 1 : 0));
    sound(1, 0, 0, 0);

    // L240 
    update_transparant();

    reactor.STBUB = reactor.STBUB - reactor.PP * devset[DEV_PRESSURIZER_VALVE] * 3;
    if (reactor.STBUB < 0) {
        reactor.STBUB = 0;
    }

    // L250
    if (reactor.PRZRLVL < 10) {
        reactor.STBUB = reactor.STBUB + 1000;
    }

    // L260
    reactor.PRZRLVL = floor(reactor.STBUB / reactor.PP);    // STBUB / RCS_Pressure
    if (reactor.PRZRLVL > 23) {
        reactor.PRZRLVL = 23;
    }

    // L280
    //a = usr(523, reactor.PRZRLVL);
    update_pressurizer_level();
    // 
    update_quench_tank();
}

void gosub_8900(uint8_t dev)
{
    // L8900
    if (dev < 5) return;
    devset[dev] = ulim[dev];
    if (devset[dev] > rdng[dev]) {
        devset[dev] = rdng[dev];
    }
}

// update steam level in heat exchanger
// sim.ALVL = 2..40
void update_alvl()
{
    //marching_ants_set_div(&fw_ants[0], 100);         // all steam (yellow)
    uint8_t div = (int)sim.ALVL;
    marching_ants_set_div(&fw_ants[1], div);
    marching_ants_set_div(&fw_ants[2], div);
}

void sim_process_3()
{
    // L310
    sim.ALVL = sim.ALVL + (floor(40 - 3000 * reactor.S3 / reactor.TP ) - sim.ALVL) / 4;
    if (sim.ALVL < 2) {
        sim.ALVL = 2;
    }

    // L330
    int a = usr(4737, sim.ALVL);
    update_alvl();
    sim.NETNRG = sim.NETNRG + reactor.TURBPOW / 144;

    int numlen = int2digits(sim.NETNRG);
    setcursorx(24 + 6 - numlen);
    setcursory(3);
    // L340
    print("    "); print_int(floor(sim.NETNRG));
    if (20 * rnd(1) >= sim.BADLUCK) {
        // goto N400
    }
    // L350
    int z = floor(8 * rnd(3) + 2);
    if (ulim[z] == 0) {
        // goto N400
    }
    // L355
    if (z > 4 && ulim[z] <= max[z] - rdng[z]) {
        // goto N400
    }
    // L370
    ulim[z] = ulim[z] - 1;
    gosub_8900(z);

    // some crazy shit? ----------- - - - 
    a = 934;
    int j = peek(a);
    for (int i = 1; i <= 77; ++i) {
        poke(a, j + floor(3 * rnd(3)) - 1);
        // L380
        sound(3, 140 + floor(40 * rnd(9)), 8, 8);
    }
    poke(a, j);
    // ----------------------- -- -   -  

    // L390
    for (int i = 1; i <= 15; ++i) {
        sound(3, 64, 10, 15 - i);
        sound(0, 32, 10, 15 - i);
    }
    // L400 -- magic point, see gotos above
    reactor.IP = 0;
    if (reactor.LI > 77) {
        // goto 410
    }
    else {
        // L402
        if (reactor.PP < 3000) {
            reactor.IP = ((3000 - reactor.PP) / 3000) * devset[DEV_HPI_PUMPS] * devset[DEV_HPI_VALVE] / 10;
        }
        // L404
        a = reactor.LI;
        reactor.LI = reactor.LI + reactor.IP;
        int y = floor(reactor.LI);
        if (y > a) {
            color(3);
            plot(51, y);        // -- void in the borated water tank (hpi)
            drawto(64, y);
            if (y = 77) {
                enable_animation_hpi_valve(0);
            }
        }	
    }
    // L410
    reactor.S5 = 3 * reactor.IP;
    if (reactor.S5 < TINY) {
        reactor.S5 = TINY;
    }
    // L420
    reactor.S2 = reactor.VP * (17 * devset[DEV_RCS_PUMPS] + 1.6 ) / 8;
    if (reactor.S2 < TINY) {
        reactor.S2 = TINY;
    } 
    // L430
    reactor.IS = 0;
    if (reactor.LS > 77) {
        // goto 440
    }
    else {
        // L432
	reactor.IS = devset[DEV_AUX_FEEDWATER_PUMPS] * devset[DEV_AUX_FEEDWATER_VALVE];
        // L434
	a = reactor.LS;
	reactor.LS = reactor.LS + reactor.IS / 16;
	uint8_t y = floor(reactor.LS);
	if (y > a) {
            // update water storage tank level
            color(3);
            plot(113, y);
            drawto(126, y);
            if (y == 77) {
                enable_animation_aux_feedwater_valve(0);
            }
        }
    }
    // L440
    reactor.S6 = 2 * reactor.IS;
    if (reactor.S6 < TINY) {
        reactor.S6 = TINY;
    }
    // L450
    reactor.S3 = 3.08 * devset[DEV_MAIN_FEEDWATER_PUMPS] + reactor.S6 - devset[DEV_AUX_FEEDWATER_VALVE] / 2;
    if (reactor.S3 < TINY) {
        reactor.S3 = TINY;
    }
    // L460
    reactor.S4 = 30 * devset[DEV_CS_PUMPS];
    if (reactor.S4 < 2 * TINY) {
        reactor.S4 = 2 * TINY;
    }
    // L500
    if (reactor.TR > 2300) {
        reactor.CTTC = reactor.CTTC - (reactor.TR - 2300 ) / 100;
        if (reactor.CTTC < TINY) {
            reactor.CTTC = TINY;
        }
    }
    // L502
    reactor.S1 = reactor.CTTC * (reactor.TCRIT - reactor.TP) / (reactor.TR - reactor.TP);
    if (reactor.S1 > 40) {
        reactor.S1 = 40;
    }
    // L504
    if (reactor.S1 < 0.4) {
        reactor.S1 = 0.4;
    }
    // L510
    if (reactor.TR < 5000) {
        // L590
        if (reactor.TR > 200) {
            return;   // we're okay
        }
        fprintf(stderr, "reactor.TR=%f\n", reactor.TR);
        cold_shutdown();
    }
    else {
        // L520 -- meltdown
        meltdown();
    }
}

void clear_screen()
{
    color(3);
    for(int y = 0; y < 96; ++y) {
        for(int x = 0; x < 160; ++x) {
            plot(x, y);
        }
    }
}

void draw_pipes_1()
{
    // L1046
    static const uint8_t data[] = {30,15,94,30,52,48,41,77,50,46,24,62,54,59,98,79,77,98,103,77,112,102,37,129,116,59,141};
    // L1044
    uint8_t index = 0;
    for(uint8_t i = 1; i <= 9; ++i) {
        uint8_t x = data[index++];
        uint8_t y = data[index++];
        uint8_t z = data[index++];
        color(0);
        plot(x, y);
        drawto(z, y);
        color(2);
        plot(x, y - 1);
        drawto(z, y - 1);
        plot(x, y + 1);
        drawto(z, y + 1);
    }
}

void draw_pipes_2()
{
    // L1050
    static const uint8_t data[] = {
	34,53,71,40,48,51,61,25,35,68,16,58,70,16,58,73,16,58,75,16,58,80,60,77,103,38,46,105,38,53,
	108,38,46,110,38,53,140,53,59,105,21,36};
    uint8_t index = 0;
    // L1048
    for(int i = 1; i <= 14; ++i) {
        uint8_t x = data[index++];
        uint8_t y = data[index++];
        uint8_t z = data[index++];
        color(0);
        plot(x, y);
        drawto(x, z);
        color(2);
        plot(x + 1, y);
        drawto(x + 1, z);
        plot(x - 1, y);
        drawto(x - 1, z);
    }
}

void draw_valve(uint8_t dev)
{
    color(1);
    uint8_t x = devx[dev];
    uint8_t y = devy[dev];
    for (uint8_t j = 0; j <= 2; ++j) {
        plot(x + j, y + j);
        drawto(x + 6 - j, y + j);
        plot(x + j, y + 5 - j);
        drawto(x + 6 - j, y + 5 - j);
    }
}

void draw_valves()
{
    color(1);
    for(uint8_t dev = 2; dev <=4; ++dev) {
        draw_valve(dev);
    }
}

void draw_pumps()
{
    for(uint8_t i = 5; i <= 9; ++i) {
        uint8_t x = devx[i];
        uint8_t y = devy[i];
        color(1);
        plot(x, y - 3);
        drawto(x, y - 6);
        plot(x + 1, y - 2);
        drawto(x + 1, y - 7);
        plot(x + 2, y - 1);
        drawto(x + 2, y - 8);
        for (uint8_t j = 3; j <= 7; ++j) {
            plot(x + j, y);
            drawto(x + j, y - 9);
        }
        plot(x + 8, y - 1);
        drawto(x + 8, y - 8);
        plot(x + 9, y - 2);
        drawto(x + 9, y - 7);
        plot(x + 10, y - 3);
        drawto(x + 10, y - 6);
    }
}

void draw_reactor_body()
{
    // L1230
    static const uint8_t data[] = {
	4,50,76,50,63,65,63,65,78,50,78,64,68,51,77,
	4,64,35,64,48,50,48,50,35,63,35,63,46,51,47,
	4,112,76,112,63,127,63,127,78,112,78,126,69,113,77,
	4,34,47,46,47,46,23,34,23,34,47,45,24,35,46,
	10,30,11,30,55,26,59,29,55,25,59,12,59,8,55,11,59,7,55,7,11,30,11,29,12,8,54};

    uint8_t index = 0;
    // L1200
    for (uint8_t i = 1; i <= 5; ++i) {
        uint8_t a = data[index++];
        uint8_t x = data[index++];
        uint8_t y = data[index++];
        color(2);
        plot(x, y);
        for (uint8_t j = 1; j <= a; ++j) {
            x = data[index++];
            y = data[index++];
            drawto(x, y);
        }
        x = data[index++];
        y = data[index++];
        uint8_t n = data[index++];
        uint8_t z = data[index++];
        color(0);
        for (uint8_t j = y; j <= z; ++j) {
            plot(x, j);
            drawto(n, j);
        }
    }
}

void draw_reactor_bottom()
{
    // L1286
    static const uint8_t data[] = {
	9,55,28,10,56,27,11,57,26,12,58,25
    };
    // L1284
    uint8_t index = 0;

    color(0);
    for (uint8_t i = 1; i <= 4; ++i) {
        uint8_t x = data[index++];
        uint8_t y = data[index++];
        uint8_t a = data[index++];
        plot(x, y);
        drawto(a, y);
    }
    color(2);
}

void draw_cooling_tower()
{
    // L1294
    static const uint8_t data[] = {
	140,11,4,11,5,12,8,13,9,14,9,15,8,18,7,21,6,26,7,31,8,36,9,40,10,43,14,46,10,47,4,48,0,48
    };
    uint8_t index = 0;
    // L1290
    for (int8_t j = -1; j <= 1; j += 2) {
        uint8_t x = data[index++];
        uint8_t y = data[index++];
        plot(x, y);
        for (uint8_t i = 1; i <= 16; ++i) {
            uint8_t a = data[index++];
            uint8_t b = data[index++];
            drawto(x + j * a, b);
        }
        index = 0;
    }
}

void draw_cooling_tower_base()
{
    // L1310
    static const uint8_t data[] = {
	124,50,127,46,130,52,134,48,136,53,140,48,144,53,146,48,150,52,153,46,156,50
    };
    uint8_t index = 0;

    plot(132, 16);
    drawto(135, 16 + 1);
    drawto(136, 18);
    drawto(144, 18);
    drawto(145, 16 + 1);
    drawto(148, 16);

    uint8_t x = data[index++];
    uint8_t y = data[index++];
    color(2);
    plot(x, y);
    for (uint8_t i = 1; i <= 10; ++i) {
        x = data[index++];
        y = data[index++];
        drawto(x, y);
    }

}

void draw_power_display()
{
    // power output display background (originally olive)
    // L1320
    color(6);
    for (uint8_t i = 9; i <= 16+5; ++i) {
        plot(112, i);
        drawto(129, i);
    }
}

void draw_heat_exchanger()
{
    // L1370
    static const uint8_t data[] = {
	69,11,74,68,12,75,67,13,76,67,61,76,68,62,75,69,63,74
    };
    uint8_t index = 0;

    // L1360
    color(2);
    for (uint8_t i = 1; i <= 6; ++i) {
        uint8_t x = data[index++];
        uint8_t y = data[index++];
        uint8_t a = data[index++];
        plot(x, y);
        drawto(a, y);
    }
}

void draw_turbine()
{
    // L1380
    color(4);
    for (uint8_t i = 1; i <= 5; ++i) {
        plot(i + i + 93, 16 + 1 + i);
        drawto(i + i + 93, 13 - i);
        plot(i + i + 94, 17 + i);
        drawto(i + i + 94, 13 - i);
    }
    // L1385
    for (uint8_t i = 1; i <= 4; ++i) {
        plot(100 + 4 + i, 16 + 6);
        drawto(100 + 4 + i, 8);
    }
}

void draw_reactor_lid()
{
    // L1390
    static const uint8_t data[] = {
	3,6,8,9,10,12,12
    };
    uint8_t index = 0;
    color(2);
    for (uint8_t y = 4; y <= 10; ++y) {
        uint8_t a = data[index++];
        plot(18 - a, y);
        drawto(18 + 1 + a, y);
    }
}

void draw_pipe_ends1()
{
    // L1435
    static const uint8_t data[] = {
	71,15,71,59,72,15,72,59,62,24,102,37,103,46,106,37,107,46,107,37,108,46
    };
    uint8_t index = 0;
    // L1430
    color(2);
    for (uint8_t i = 1; i <= 11; ++i) {
        uint8_t x = data[index++];
        uint8_t y = data[index++];
        plot(x, y);
    }
}

void draw_pipe_ends2()
{
    static const uint8_t data[] = {
	139,59,61,35,81,77,104,45,108,45,30,15,30,52,109,45,46,24,40,48,40,47
    };
    uint8_t index = 0;
    // L1440
    color(0);
    for (uint8_t i = 1; i <= 11; ++i) {
        uint8_t x = data[index++];
        uint8_t y = data[index++];
        plot(x, y);
    }
}

void draw_turbine_link()
{
    color(6);
    plot(100 + 9, 15);
    drawto(112, 15);
    plot(111, 18);
    drawto(111, 9 + 3);
}
    
void waiting_for_nrc_license()
{
    // 1650
    // 	print "       \D7\C1\C9\D4\C9\CE\C7\A0\C6\CF\D2\A0\CE\D2\C3\A0\CC\C9\C3\C5\CE\D3\C5     " # "       WAITING FOR NRC LICENSE     " (inv)
    // 1660
    // 	G$( N24, N24 + N1 ) = "\A9\00"
    // 	A = usr( adr( G$ ), N0, PMBASE + 384, 546 )
    // 	G$( N24, N24 + N1 ) = "\B1\CC"  # "1L" (inv)
    // 1670
}

void draw_degree_glyphs()
{
    // L1670
    color(2);
    for (uint8_t i = TEMPS_LO; i <= TEMPS_HI; ++i) {
        uint8_t x = tempxy[i * 2];
        uint8_t y = tempxy[i * 2 + 1];
        
        if (i != TEMPS_HI) {
            plot(x, y - 5);
        }
    }
}

void draw_power_plant()
{
    clear_screen();

    draw_pipes_1();
    draw_pipes_2();

    // L1111
    //poke(1726 + 5, 1);
    select_device(DEV_REACTOR);
    
    draw_valves();
    draw_pumps();

    draw_reactor_body();
    draw_reactor_bottom();
    draw_cooling_tower();
    draw_cooling_tower_base();
    draw_power_display();
    draw_heat_exchanger();
    draw_turbine();

    draw_reactor_lid();
    draw_pipe_ends1();
    draw_pipe_ends2();
    draw_turbine_link();
    //
    waiting_for_nrc_license();
    draw_degree_glyphs();

    draw_core();
}

void mystery_routines()
{
    // RCS ants
    marching_ants_h(&rcs_ants[0], 29, 70, 15, 1); // hot
    marching_ants_v(&rcs_ants[1], 68, 16, 58, 1); // heat exch 1
    marching_ants_v(&rcs_ants[2], 70, 16, 58, 1); // heat exch 2
    marching_ants_h(&rcs_ants[3], 56, 70, 59, -1); // cold before pump
    marching_ants_h(&rcs_ants[4], 29, 47, 52, -1); // cold after pump

    // pressurizer
    marching_ants_h(&pressurizer_ants[0], 46, 61, 24, 1);
    marching_ants_set_div(&pressurizer_ants[0], 100);         // all steam (yellow)
    marching_ants_v(&pressurizer_ants[1], 61, 25, 35, 1);
    marching_ants_set_div(&pressurizer_ants[1], 100);         // all steam (yellow)
    
    // high-pressure injector
    marching_ants_v(&hpi_ants[0], 34, 52, 70, -1);
    marching_ants_h(&hpi_ants[1], 42, 51, 77, -1);

    // Secondary loop (feedwater) ants
    marching_ants_h(&fw_ants[0], 73, 94, 15, 1); // hot
    marching_ants_set_div(&fw_ants[0], 100);         // all steam (yellow)
    marching_ants_v(&fw_ants[1], 73, 16, 58, -1); // heat exch 1
    marching_ants_set_div(&fw_ants[1], 20);
    marching_ants_v(&fw_ants[2], 75, 16, 58, -1); // heat exch 2
    marching_ants_set_div(&fw_ants[2], 20);
    marching_ants_v(&fw_ants[3], 105, 23, 52, 1); // down from the turbine
    marching_ants_set_div(&fw_ants[3], 20);        // 
    marching_ants_v(&fw_ants[4], 103, 37, 45, 1); // condenser
    marching_ants_set_div(&fw_ants[4], 6);       // turbine cold leg is fixed
    marching_ants_h(&fw_ants[5], 73, 97, 59, -1); // cold

    // aux feedwater 
    marching_ants_v(&aux_fw_ants[0], 80, 60, 76, -1);
    marching_ants_h(&aux_fw_ants[1], 80, 96, 77, -1);
    marching_ants_h(&aux_fw_ants[2], 104, 111, 77, -1);
    
    // tertiary, cooling tower
    marching_ants_v(&cs_ants[0], 140, 53, 58, 1);
    marching_ants_h(&cs_ants[1], 118, 140, 59, -1);
    marching_ants_v(&cs_ants[2], 110, 38, 52, -1); //
    marching_ants_v(&cs_ants[3], 108, 37, 45, -1); // condenser
    marching_ants_h(&cs_ants[4], 110, 128, 37, 1);
}

void play_animations()
{
    for (uint8_t i = 0; i < 5; ++i) {
        marching_ants_step(&rcs_ants[i]);
    }

    for (uint8_t i = 0; i < 6; ++i) {
        marching_ants_step(&fw_ants[i]);
    }

    for (uint8_t i = 0; i < 2; ++i) {
        marching_ants_step(&pressurizer_ants[i]);
    }

    for (uint8_t i = 0; i < 2; ++i) {
        marching_ants_step(&hpi_ants[i]);
    }

    for (uint8_t i = 0; i < 3; ++i) {
        marching_ants_step(&aux_fw_ants[i]);
    }

    for (uint8_t i = 0; i < 5; ++i) {
        marching_ants_step(&cs_ants[i]);
    }

    animate_device_sel(0);
    draw_core();
}


void sim_step()
{
    sim_process_1();
    draw_temps();
    sim_process_2();
    sim_process_3();

    ++sim.STEP;
}

int main()
{
    int iteration = 0;
    graphics_init();
    draw_power_plant();

    mystery_routines();

    sim_init();

    //graphics_refresh();
    //getch();


    while (!sim.STOPPED) {
        sim_step();
        graphics_refresh();
        //printf("iteration: %d stop=%d", iteration, sim.STOPPED);
        fprintf(stderr, "iteration %d: ALVL=%f PRZRLVL=%d STBUB=%f PP=%f\n", iteration, sim.ALVL, reactor.PRZRLVL, reactor.STBUB, reactor.PP);
        ++iteration;

        for (uint8_t i = 0; i < 40; ++i) {
            poll_input();
            check_input();
            play_animations();
            usleep(50000);
            graphics_refresh();
        }
    }
    graphics_shutdown();
    return 0;
}
