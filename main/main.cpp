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
#include <cstring>
#include <stdint.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TILE_SIZE (256)
#define NUM_TILES (9)  //9 6 4
#define LOC_LAT (11.5346)
#define LOC_LON (9.9743)
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
    "custom",
    "world",
};

//static const char *const maps[] = {
//    "world",
//};

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
static VMINT screen_w = 0;
static VMINT screen_h = 0;
static double current_lat = LOC_LAT;
static double current_lon = LOC_LON;
static uint8_t current_zoom = 0;
static uint32_t rng_state = 0;
static uint8_t max_zoom_level = 0;
static double small_step = 0.0;
static double big_step = 0.0;
static bool show_info = true;
static bool info_dirty = true;
static lv_obj_t *info_panel = NULL;

static void update_pan_step(void);
static uint8_t get_max_zoom_level(const char *map);
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
    char tmp[INFO_PATH_LEN];

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    VMWCHAR wpath[INFO_PATH_LEN] = {0};
    vm_ascii_to_ucs2(wpath, sizeof(wpath), tmp);

    fix_path(wpath);

    VMFILE file = vm_file_open(wpath, MODE_READ, 1);
    if (file >= 0)
    {
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

if(n > 0 && n < len) {

    if(file_exists(filename)) {

        return true;
    }

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

    current_zoom = LV_MIN(current_zoom, max_zoom_level);

    deg2num(current_lat, current_lon, current_zoom, &x, &y, &dx, &dy);

    px = (int16_t)((screen_w / 2) - dx);
    py = (int16_t)((screen_h / 2) - dy);

    update_tiles(px, py, current_zoom, x, y, tiles);

if(show_info && info_dirty && info_label != NULL) {

    lv_label_set_text_fmt(
        info_label,
        "Lat: %.4f Y:%lu\n"
        "Lon: %.4f X:%lu\n"
        "Zoom:%u",
        current_lat,
        (unsigned long)y,
        current_lon,
        (unsigned long)x,
        (unsigned int)current_zoom
    );

    info_dirty = false;
}

}

extern "C" void map_key_hook(VMINT event, VMINT keycode)
{
    bool handled = false;

//    if((event != VM_KEY_EVENT_DOWN) &&
//       (event != VM_KEY_EVENT_LONG_PRESS) &&
//       (event != VM_KEY_EVENT_REPEAT)) {
//        return;
//    }

    if((event != VM_KEY_EVENT_UP) &&
       (event != VM_KEY_EVENT_LONG_PRESS)) {
        return;
    }


    switch(keycode) {
    case VM_KEY_UP:

    current_lat = clamp_latitude(
        current_lat + (
            event == VM_KEY_EVENT_LONG_PRESS
                ? big_step
                : small_step

        )
    );
    info_dirty = true;
    handled = true;

        break;

    case VM_KEY_DOWN:

    current_lat = clamp_latitude(
        current_lat - (
            event == VM_KEY_EVENT_LONG_PRESS
                ? big_step
                : small_step

        )
    );
    info_dirty = true;
    handled = true;

        break;

    case VM_KEY_LEFT:

    current_lon = wrap_longitude(
        current_lon - (
            event == VM_KEY_EVENT_LONG_PRESS
                ? big_step
                : small_step

        )
    );
    info_dirty = true;
    handled = true;

        break;

    case VM_KEY_RIGHT:

    current_lon = wrap_longitude(
        current_lon + (
            event == VM_KEY_EVENT_LONG_PRESS
                ? big_step
                : small_step

        )
    );
    info_dirty = true;
    handled = true;

        break;

case VM_KEY_NUM1:
    show_info = !show_info;

    if (show_info) {
        lv_obj_clear_flag(info_panel, LV_OBJ_FLAG_HIDDEN);
        info_dirty = true;
        refresh_map_view();
    } else {
        lv_obj_add_flag(info_panel, LV_OBJ_FLAG_HIDDEN);
    }

    handled = true;

    break;

    case VM_KEY_OK:
        current_lat = LOC_LAT;
        current_lon = LOC_LON;
        current_zoom = 0;     // or 2 if you prefer
//        current_zoom = max_zoom_level;
        update_pan_step();
        info_dirty = true;
        handled = true;
        break;

    case VM_KEY_LEFT_SOFTKEY:

        if(current_zoom < max_zoom_level) {
            ++current_zoom;
            info_dirty = true;
            update_pan_step();
        }

        handled = true;
        break;


    case VM_KEY_RIGHT_SOFTKEY:

        if(current_zoom > 0) {
            --current_zoom;
            info_dirty = true;
            update_pan_step();
        }

        handled = true;
        break;

    default:
        return;
    }

    if(handled) {

        refresh_map_view();
    }
}

static void tim(int tid)
{
    (void)tid;
    lv_timer_handler();
}

void vm_main(void)
{
    max_zoom_level = LV_MAX(get_max_zoom_level("world"), get_max_zoom_level("custom"));
//    current_zoom = max_zoom_level;
    update_pan_step();
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

    info_panel = lv_button_create(scr);

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

    vm_reg_sysevt_callback(handle_sysevt);
    vm_create_timer(1, tim);

}

void handle_sysevt(VMINT message, VMINT param)
{
    (void)param;

    switch(message) {
    case VM_MSG_CREATE:
    case VM_MSG_ACTIVE:
        vm_switch_power_saving_mode(turn_off_mode);
        disp_enable_update();
        break;

    case VM_MSG_PAINT:
        break;

    case VM_MSG_INACTIVE:
        disp_disable_update();
        break;

    case VM_MSG_QUIT:
//        if(map_timer != NULL) {
//            lv_timer_delete(map_timer);
//            map_timer = NULL;
//        }
        break;
    }
}

static uint8_t get_max_zoom_level(const char *map)
{
    char path[128];
    char name[32];

    VMWCHAR wpath[128];
    VMINT hnd;
    vm_fileinfo_ext info;

    uint8_t max_z = 0;

    snprintf(path, sizeof(path), "e:/osm/%s/*.*",  map); //*/

    vm_ascii_to_ucs2(wpath, sizeof(wpath), path);

    /* Convert '/' to '\' */
    for (VMWCHAR *p = wpath; *p; ++p) {
        if (*p == '/') {
            *p = '\\';
        }
    }

    hnd = vm_find_first_ext(wpath, &info);
    if (hnd < 0) {
        return 0;
    }

    do {

        /* Process directories only */
        if (!(info.attributes & VM_FS_ATTR_DIR)) {
            continue;
        }

        vm_ucs2_to_ascii(
            name,
            sizeof(name),
            info.filefullname
        );

        int z = atoi(name);

        if (z >= 0 && z <= 255) {
            if ((uint8_t)z > max_z) {
                max_z = (uint8_t)z;
            }
        }

    } while (vm_find_next_ext(hnd, &info) == 0);

    vm_find_close_ext(hnd);

    return max_z;
}

static void update_pan_step(void)
{
    small_step =
        3.0 *
        (double)(1u << (max_zoom_level - current_zoom)) /
        (double)(1u << max_zoom_level);

    big_step = small_step * 20.0;
}
