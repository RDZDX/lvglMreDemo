#include "vmsys.h"
#include "vmio.h"
#include "vmgraph.h"
#include "vmchset.h"
#include "vmstdlib.h"
#include "vm4res.h"
#include "vmres.h"
#include "vmtimer.h"

#include "lv_conf.h"
#include "lvgl.h"

#include "lv_port_disp_mre.h"
#include "lv_port_indev_mre.h"
#include "lv_port_fs_mre.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TILE_SIZE (256)
#define NUM_TILES (4)
#define LOC_LAT (54.3520)
#define LOC_LON (18.6466)
#define MAX_ZOOM_LEVEL (16)
#define INFO_PATH_LEN (96)
#define OFFSCREEN_POS (-300)
#define INFO_ZOOM_WALK_PERIOD_MS (2000)
#define INFO_RANDOM_WALK_PERIOD_MS (1000)
#define MAX_MERCATOR_LAT (85.05112878)

typedef struct {
    int8_t x;
    int8_t y;
} tile_offset_t;

typedef struct {
    lv_obj_t *img;
    size_t x;
    size_t y;
    uint8_t z;
    bool is_visible;
} tile_t;

typedef enum {
    MAP_PHASE_ZOOM_WALK = 0,
    MAP_PHASE_RANDOM_WALK,
} map_phase_t;

static const char *const maps[] = {
    "gdansk",
    "world",
};

static const tile_offset_t tile_offsets[9] = {
    {0, 0},
    {0, -1},
    {1, -1},
    {1, 0},
    {1, 1},
    {0, 1},
    {-1, 1},
    {-1, 0},
    {-1, -1},
};

static const lv_point_precise_t crosshair_h_points[2] = {{0, 0}, {15, 0}};
static const lv_point_precise_t crosshair_v_points[2] = {{0, 0}, {0, 15}};

static tile_t tiles[NUM_TILES];
static lv_obj_t *info_label = NULL;
static lv_timer_t *map_timer = NULL;
static VMINT screen_w = 0;
static VMINT screen_h = 0;
static double current_lat = LOC_LAT;
static double current_lon = LOC_LON;
static uint8_t current_zoom = 0;
static map_phase_t map_phase = MAP_PHASE_ZOOM_WALK;
static uint32_t rng_state = 0;
static bool manual_mode = false;

void handle_sysevt(VMINT message, VMINT param);
extern "C" void map_key_hook(VMINT event, VMINT keycode);

static void fix_path(VMWCHAR *path)
{
    for(; *path; ++path) {
        if(*path == '/') {
            *path = '\\';
        }
    }
}

static bool file_exists(const char *path)
{
    VMWCHAR wpath[INFO_PATH_LEN] = {0};
    vm_ascii_to_ucs2(wpath, sizeof(wpath), path);
    fix_path(wpath);

    VMFILE file = vm_file_open(wpath, MODE_READ, 1);
    if(file >= 0) {
        vm_file_close(file);
        return true;
    }

    return false;
}

static double clamp_latitude(double lat)
{
    if(lat > MAX_MERCATOR_LAT) {
        return MAX_MERCATOR_LAT;
    }
    if(lat < -MAX_MERCATOR_LAT) {
        return -MAX_MERCATOR_LAT;
    }
    return lat;
}

static double wrap_longitude(double lon)
{
    while(lon > 180.0) {
        lon -= 360.0;
    }
    while(lon < -180.0) {
        lon += 360.0;
    }
    return lon;
}

static uint32_t next_random_u32(void)
{
    rng_state = (rng_state * 1664525u) + 1013904223u;
    return rng_state;
}

static int32_t next_random_delta(void)
{
    return (int32_t)(next_random_u32() % 7u) - 3;
}

static void deg2num(double lat, double lon, uint8_t zoom,
                    size_t *x, size_t *y, uint16_t *dx, uint16_t *dy)
{
    double tmp;
    double lat_rad = lat * (M_PI / 180.0);
    size_t n = (size_t)1u << zoom;
    double xtile = (lon + 180.0) / 360.0 * (double)n;
    double ytile = (1.0 - asinh(tan(lat_rad)) / M_PI) / 2.0 * (double)n;

    *x = (size_t)xtile;
    *y = (size_t)ytile;
    *dx = (uint16_t)(TILE_SIZE * modf(xtile, &tmp));
    *dy = (uint16_t)(TILE_SIZE * modf(ytile, &tmp));
}

static bool get_file_name(char *filename, int len, uint8_t z, size_t x, size_t y)
{
    for(uint8_t i = 0; i < (sizeof(maps) / sizeof(maps[0])); ++i) {
        int n = snprintf(filename, len, "e:/osm/%s/%u/%lu/%lu.bin",
                         maps[i],
                         (unsigned int)z,
                         (unsigned long)x,
                         (unsigned long)y);
        if(n > 0 && n < len && file_exists(filename)) {
            return true;
        }
    }

    int n = snprintf(filename, len, "e:/osm/images/empty.bin");
    return n > 0 && n < len && file_exists(filename);
}

static void update_tiles(int16_t cx, int16_t cy, uint8_t z, size_t x, size_t y, tile_t map_tiles[NUM_TILES])
{
    uint8_t visible_index = 0;
    char filename[INFO_PATH_LEN];
    int32_t tile_limit = (int32_t)((size_t)1u << z);

    for(uint8_t i = 0; i < (sizeof(tile_offsets) / sizeof(tile_offsets[0])); ++i) {
        int16_t r1x = (int16_t)(cx + (tile_offsets[i].x * TILE_SIZE));
        int16_t r2x = (int16_t)(r1x + TILE_SIZE);
        int16_t r1y = (int16_t)(cy + (tile_offsets[i].y * TILE_SIZE));
        int16_t r2y = (int16_t)(r1y + TILE_SIZE);

        if((0 >= r2x) || (screen_w <= r1x) || (screen_h <= r1y) || (0 >= r2y)) {
            continue;
        }

        int32_t tile_x = (int32_t)x + tile_offsets[i].x;
        int32_t tile_y = (int32_t)y + tile_offsets[i].y;
        if(tile_x < 0 || tile_y < 0 || tile_x >= tile_limit || tile_y >= tile_limit) {
            continue;
        }

        if(!get_file_name(filename, sizeof(filename), z, (size_t)tile_x, (size_t)tile_y)) {
            continue;
        }

        tile_t *tile = &map_tiles[visible_index];
        tile->x = (size_t)tile_x;
        tile->y = (size_t)tile_y;
        tile->z = z;
        tile->is_visible = true;
        lv_obj_align(tile->img, LV_ALIGN_TOP_LEFT, r1x, r1y);
        lv_image_set_src(tile->img, filename);
        ++visible_index;

        if(visible_index >= NUM_TILES) {
            break;
        }
    }

    for(uint8_t i = visible_index; i < NUM_TILES; ++i) {
        tile_t *tile = &map_tiles[i];
        tile->is_visible = false;
        lv_obj_align(tile->img, LV_ALIGN_TOP_LEFT, OFFSCREEN_POS, OFFSCREEN_POS);
    }
}

static void refresh_map_view(void)
{
    size_t x;
    size_t y;
    uint16_t dx;
    uint16_t dy;
    int16_t px;
    int16_t py;

    deg2num(current_lat, current_lon, current_zoom, &x, &y, &dx, &dy);

    px = (int16_t)((screen_w / 2) - dx);
    py = (int16_t)((screen_h / 2) - dy);

    update_tiles(px, py, current_zoom, x, y, tiles);

    if(info_label != NULL) {
        lv_label_set_text_fmt(info_label,
                              "Lat: %.4f Y: %lu\nLon: %.4f X: %lu\nZoom: %u",
                              current_lat,
                              (unsigned long)y,
                              current_lon,
                              (unsigned long)x,
                              (unsigned int)current_zoom);
    }
}

static void enter_manual_mode(void)
{
    if(!manual_mode) {
        manual_mode = true;
        if(map_timer != NULL) {
            lv_timer_pause(map_timer);
        }
    }
}

static double get_pan_step(void)
{
    return 0.001 * (double)(1u << (MAX_ZOOM_LEVEL - current_zoom));
}

static void resume_auto_mode(void)
{
    manual_mode = false;
    current_zoom = 0;
    map_phase = MAP_PHASE_ZOOM_WALK;

    if(map_timer != NULL) {
        lv_timer_set_period(map_timer, INFO_ZOOM_WALK_PERIOD_MS);
        lv_timer_resume(map_timer);
    }

    refresh_map_view();
}

extern "C" void map_key_hook(VMINT event, VMINT keycode)
{
    bool handled = false;

    if((event != VM_KEY_EVENT_DOWN) &&
       (event != VM_KEY_EVENT_LONG_PRESS) &&
       (event != VM_KEY_EVENT_REPEAT)) {
        return;
    }

    switch(keycode) {
    case VM_KEY_UP:
    case VM_KEY_NUM2:
        enter_manual_mode();
        if(current_zoom < MAX_ZOOM_LEVEL) {
            ++current_zoom;
        }
        handled = true;
        break;

    case VM_KEY_DOWN:
    case VM_KEY_NUM8:
        enter_manual_mode();
        if(current_zoom > 0) {
            --current_zoom;
        }
        handled = true;
        break;

    case VM_KEY_LEFT:
    case VM_KEY_NUM4:
        enter_manual_mode();
        current_lon = wrap_longitude(current_lon - get_pan_step());
        handled = true;
        break;

    case VM_KEY_RIGHT:
    case VM_KEY_NUM6:
        enter_manual_mode();
        current_lon = wrap_longitude(current_lon + get_pan_step());
        handled = true;
        break;

    case VM_KEY_NUM1:
        enter_manual_mode();
        current_lat = clamp_latitude(current_lat + get_pan_step());
        handled = true;
        break;

    case VM_KEY_NUM7:
        enter_manual_mode();
        current_lat = clamp_latitude(current_lat - get_pan_step());
        handled = true;
        break;

    case VM_KEY_OK:
    case VM_KEY_LEFT_SOFTKEY:
        resume_auto_mode();
        return;

    default:
        return;
    }

    if(handled) {
        refresh_map_view();
    }
}

static void map_timer_cb(lv_timer_t *timer)
{
    if(manual_mode) {
        return;
    }

    if(map_phase == MAP_PHASE_ZOOM_WALK) {
        if(current_zoom < MAX_ZOOM_LEVEL) {
            ++current_zoom;
            refresh_map_view();
            if(current_zoom == MAX_ZOOM_LEVEL) {
                map_phase = MAP_PHASE_RANDOM_WALK;
                lv_timer_set_period(timer, INFO_RANDOM_WALK_PERIOD_MS);
            }
        }
        return;
    }

    current_lat = clamp_latitude(current_lat + (0.0001 * next_random_delta()));
    current_lon = wrap_longitude(current_lon + (0.0001 * next_random_delta()));
    refresh_map_view();
}

static void tim(int tid)
{
    (void)tid;
    lv_timer_handler();
}

void vm_main(void)
{
    lv_init();
    lv_tick_set_cb((lv_tick_get_cb_t)vm_get_tick_count);
    lv_port_disp_init();
    lv_port_indev_init();
    lv_port_indev_set_key_hook(map_key_hook);
    lv_port_fs_init();

    screen_w = vm_graphic_get_screen_width();
    screen_h = vm_graphic_get_screen_height();
    rng_state = (uint32_t)vm_get_tick_count() ^ 0xA5A5A5A5u;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    for(uint8_t i = 0; i < NUM_TILES; ++i) {
        tiles[i].img = lv_image_create(scr);
        tiles[i].is_visible = false;
        lv_obj_align(tiles[i].img, LV_ALIGN_TOP_LEFT, OFFSCREEN_POS, OFFSCREEN_POS);
    }

    lv_obj_t *info_panel = lv_button_create(scr);
    lv_obj_set_size(info_panel, screen_w - 20, screen_h / 4);
    lv_obj_align(info_panel, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(info_panel, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(info_panel, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_border_color(info_panel, lv_color_black(), 0);
    lv_obj_set_style_border_width(info_panel, 2, 0);
    lv_obj_set_style_radius(info_panel, 8, 0);
    lv_obj_set_style_pad_all(info_panel, 8, 0);

    info_label = lv_label_create(info_panel);
    lv_obj_set_style_text_color(info_label, lv_color_white(), 0);
    lv_obj_align(info_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *line_h = lv_line_create(scr);
    lv_line_set_points(line_h, crosshair_h_points, 2);
    lv_obj_set_style_line_width(line_h, 4, 0);
    lv_obj_set_style_line_color(line_h, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_line_rounded(line_h, true, 0);
    lv_obj_align(line_h, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *line_v = lv_line_create(scr);
    lv_line_set_points(line_v, crosshair_v_points, 2);
    lv_obj_set_style_line_width(line_v, 4, 0);
    lv_obj_set_style_line_color(line_v, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_line_rounded(line_v, true, 0);
    lv_obj_align(line_v, LV_ALIGN_CENTER, 0, 0);

    refresh_map_view();
    map_timer = lv_timer_create(map_timer_cb, INFO_ZOOM_WALK_PERIOD_MS, NULL);

    vm_reg_sysevt_callback(handle_sysevt);
    vm_create_timer(1, tim);
}

void handle_sysevt(VMINT message, VMINT param)
{
    (void)param;

    switch(message) {
    case VM_MSG_CREATE:
    case VM_MSG_ACTIVE:
        disp_enable_update();
        break;

    case VM_MSG_PAINT:
        break;

    case VM_MSG_INACTIVE:
        disp_disable_update();
        break;

    case VM_MSG_QUIT:
        if(map_timer != NULL) {
            lv_timer_delete(map_timer);
            map_timer = NULL;
        }
        break;
    }
}
