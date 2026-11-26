/*
 * ps5overlayd.c — ultra-lightweight standalone Wayland monitoring overlay for PS5 Linux.
 * Protocol: wl_shm + zwlr_layer_shell_v1 (LayerOverlay) + PangoCairo.
 * Reads sensors directly from sysfs and runs independently of the main GUI.
 */

#define _GNU_SOURCE
#include <cairo/cairo.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define OVERLAY_WIDTH 224
#define PAD_X 10
#define PAD_Y 8
#define ROW_H 16
#define HEADER_H 24
#define FOOTER_H 14

typedef struct {
    bool valid;
    double val;
} OptDouble;

typedef struct {
    bool valid;
    int val;
} OptInt;

typedef struct {
    OptDouble soc, local1, local2, local3;
    OptDouble gpu_t, cpu_t, nvme_t;
    OptInt cpu_mhz, gpu_mhz, gpu_busy, fan_pct;
    OptInt vram_used_mb, vram_total_mb;
    char gpu_pm[32];
} SensorData;

typedef struct {
    int x, y;
    int opacity; /* 40..100 */
    bool lang_ru;
    double acc_r, acc_g, acc_b;
    double bg_r, bg_g, bg_b;
    double fg_r, fg_g, fg_b;
    bool m_soc, m_local1, m_local2, m_local3;
    bool m_gpu_t, m_cpu_t, m_cpu_mhz, m_nvme;
    bool m_gpu_busy, m_gpu_mhz, m_vram, m_fan, m_gpu_pm;
} OverlayConfig;

typedef struct {
    struct wl_buffer *buffer;
    void *data;
    size_t size;
    int width, height;
    bool busy;
} ShmBuffer;

typedef struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_output *output;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    ShmBuffer bufs[2];
    int cur_buf;
    int width, height;
    int out_w, out_h;
    bool configured;
    bool running;

    /* Pointer / Drag */
    double ptr_x, ptr_y;
    bool dragging;
    double drag_start_x, drag_start_y;

    /* Config & Sensors */
    OverlayConfig cfg;
    SensorData sensors;
    char conf_path[PATH_MAX];
    char pid_path[PATH_MAX];

    /* Cached sysfs base paths */
    char hw_fan[PATH_MAX];
    char hw_gpu[PATH_MAX];
    char hw_nvme[PATH_MAX];
    char hw_cpu[PATH_MAX];
    char drm_dev[PATH_MAX];
} AppState;

/* ── sysfs read helpers ────────────────────────────────────────────────── */

static bool read_file_str(const char *path, char *out, size_t sz) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = read(fd, out, sz - 1);
    close(fd);
    if (n <= 0) return false;
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '))
        out[--n] = '\0';
    return true;
}

static bool read_file_ll(const char *path, long long *out) {
    char buf[64];
    if (!read_file_str(path, buf, sizeof(buf))) return false;
    char *end = NULL;
    long long v = strtoll(buf, &end, 10);
    if (end == buf) return false;
    *out = v;
    return true;
}

static void find_hwmon_paths(AppState *st) {
    st->hw_fan[0] = st->hw_gpu[0] = st->hw_nvme[0] = st->hw_cpu[0] = st->drm_dev[0] = '\0';
    DIR *d = opendir("/sys/class/hwmon");
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strncmp(ent->d_name, "hwmon", 5) != 0) continue;
            char npath[PATH_MAX], name[64];
            snprintf(npath, sizeof(npath), "/sys/class/hwmon/%s/name", ent->d_name);
            if (!read_file_str(npath, name, sizeof(name))) continue;
            if (strcmp(name, "ps5_fan") == 0)
                snprintf(st->hw_fan, sizeof(st->hw_fan), "/sys/class/hwmon/%s", ent->d_name);
            else if (strcmp(name, "amdgpu") == 0)
                snprintf(st->hw_gpu, sizeof(st->hw_gpu), "/sys/class/hwmon/%s", ent->d_name);
            else if (strcmp(name, "nvme") == 0)
                snprintf(st->hw_nvme, sizeof(st->hw_nvme), "/sys/class/hwmon/%s", ent->d_name);
            else if (strcmp(name, "k10temp") == 0)
                snprintf(st->hw_cpu, sizeof(st->hw_cpu), "/sys/class/hwmon/%s", ent->d_name);
        }
        closedir(d);
    }

    DIR *drm = opendir("/sys/class/drm");
    if (drm) {
        struct dirent *ent;
        while ((ent = readdir(drm)) != NULL) {
            if (strncmp(ent->d_name, "card", 4) != 0 || strchr(ent->d_name, '-') != NULL)
                continue;
            char vpath[PATH_MAX], vend[32];
            snprintf(vpath, sizeof(vpath), "/sys/class/drm/%s/device/vendor", ent->d_name);
            if (read_file_str(vpath, vend, sizeof(vend)) && strcmp(vend, "0x1002") == 0) {
                snprintf(st->drm_dev, sizeof(st->drm_dev), "/sys/class/drm/%s/device", ent->d_name);
                break;
            }
        }
        closedir(drm);
    }
    if (!st->drm_dev[0])
        snprintf(st->drm_dev, sizeof(st->drm_dev), "/sys/class/drm/card0/device");
}

static void read_cpu_mhz(SensorData *s) {
    long long khz = 0;
    if (read_file_ll("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", &khz) && khz > 0) {
        s->cpu_mhz = (OptInt){true, (int)((khz + 500) / 1000)};
        return;
    }
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return;
    char line[256];
    double sum = 0.0;
    int cnt = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu MHz", 7) == 0) {
            char *colon = strchr(line, ':');
            if (colon) {
                sum += atof(colon + 1);
                cnt++;
            }
        }
    }
    fclose(f);
    if (cnt > 0) s->cpu_mhz = (OptInt){true, (int)lround(sum / cnt)};
}

static void poll_sensors(AppState *st) {
    memset(&st->sensors, 0, sizeof(st->sensors));
    snprintf(st->sensors.gpu_pm, sizeof(st->sensors.gpu_pm), "—");
    if (!st->hw_fan[0] && !st->hw_gpu[0]) find_hwmon_paths(st);

    char p[PATH_MAX + 64];
    long long v = 0;

    if (st->hw_fan[0]) {
        snprintf(p, sizeof(p), "%s/temp1_input", st->hw_fan);
        if (read_file_ll(p, &v)) st->sensors.soc = (OptDouble){true, v / 1000.0};
        snprintf(p, sizeof(p), "%s/temp2_input", st->hw_fan);
        if (read_file_ll(p, &v)) st->sensors.local1 = (OptDouble){true, v / 1000.0};
        snprintf(p, sizeof(p), "%s/temp3_input", st->hw_fan);
        if (read_file_ll(p, &v)) st->sensors.local2 = (OptDouble){true, v / 1000.0};
        snprintf(p, sizeof(p), "%s/temp4_input", st->hw_fan);
        if (read_file_ll(p, &v)) st->sensors.local3 = (OptDouble){true, v / 1000.0};
        snprintf(p, sizeof(p), "%s/pwm1", st->hw_fan);
        if (read_file_ll(p, &v)) st->sensors.fan_pct = (OptInt){true, (int)lround((double)v / 255.0 * 100.0)};
    }
    if (st->hw_gpu[0]) {
        snprintf(p, sizeof(p), "%s/temp1_input", st->hw_gpu);
        if (read_file_ll(p, &v)) st->sensors.gpu_t = (OptDouble){true, v / 1000.0};
        snprintf(p, sizeof(p), "%s/freq1_input", st->hw_gpu);
        if (read_file_ll(p, &v)) st->sensors.gpu_mhz = (OptInt){true, (int)(v / 1000000LL)};
    }
    if (st->hw_cpu[0]) {
        snprintf(p, sizeof(p), "%s/temp1_input", st->hw_cpu);
        if (read_file_ll(p, &v)) st->sensors.cpu_t = (OptDouble){true, v / 1000.0};
    }
    if (st->hw_nvme[0]) {
        snprintf(p, sizeof(p), "%s/temp1_input", st->hw_nvme);
        if (read_file_ll(p, &v)) st->sensors.nvme_t = (OptDouble){true, v / 1000.0};
    }
    if (st->drm_dev[0]) {
        snprintf(p, sizeof(p), "%s/gpu_busy_percent", st->drm_dev);
        if (read_file_ll(p, &v)) st->sensors.gpu_busy = (OptInt){true, (int)v};
        snprintf(p, sizeof(p), "%s/power/runtime_status", st->drm_dev);
        read_file_str(p, st->sensors.gpu_pm, sizeof(st->sensors.gpu_pm));

        long long vu = 0, vt = 0;
        snprintf(p, sizeof(p), "%s/mem_info_vram_used", st->drm_dev);
        bool ok_u = read_file_ll(p, &vu);
        snprintf(p, sizeof(p), "%s/mem_info_vram_total", st->drm_dev);
        bool ok_t = read_file_ll(p, &vt);
        if (ok_u && ok_t && vt > 0) {
            st->sensors.vram_used_mb = (OptInt){true, (int)lround(vu / (1024.0 * 1024.0))};
            st->sensors.vram_total_mb = (OptInt){true, (int)lround(vt / (1024.0 * 1024.0))};
        }
    }
    read_cpu_mhz(&st->sensors);
}

/* ── Configuration ─────────────────────────────────────────────────────── */

static bool detect_system_is_ru(void) {
    const char *home = getenv("HOME");
    if (home) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/.config/plasma-localerc", home);
        FILE *f = fopen(path, "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                if (strncasecmp(line, "LANGUAGE=", 9) == 0 || strncasecmp(line, "LANG=", 5) == 0) {
                    const char *v = strchr(line, '=');
                    if (v) {
                        v++;
                        fclose(f);
                        return strncasecmp(v, "ru", 2) == 0;
                    }
                }
            }
            fclose(f);
        }
    }
    const char *env_vars[] = {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"};
    for (size_t i = 0; i < 4; ++i) {
        const char *val = getenv(env_vars[i]);
        if (val && val[0]) {
            return strncasecmp(val, "ru", 2) == 0;
        }
    }
    return false;
}

static void parse_hex_color(const char *hex, double *r, double *g, double *b) {
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return;
    unsigned int rv = 0, gv = 211, bv = 184;
    if (sscanf(hex + 1, "%2x%2x%2x", &rv, &gv, &bv) == 3) {
        *r = rv / 255.0;
        *g = gv / 255.0;
        *b = bv / 255.0;
    }
}

static void init_default_config(AppState *st) {
    OverlayConfig *c = &st->cfg;
    c->x = st->out_w > 0 ? st->out_w - OVERLAY_WIDTH - 20 : 1676;
    c->y = 40;
    c->opacity = 85;
    c->lang_ru = detect_system_is_ru();
    c->acc_r = 0.0; c->acc_g = 0.827; c->acc_b = 0.722; /* #00d3b8 */
    c->bg_r = 16.0 / 255.0; c->bg_g = 26.0 / 255.0; c->bg_b = 22.0 / 255.0; /* #101a16 */
    c->fg_r = 0.94; c->fg_g = 0.93; c->fg_b = 0.88; /* #efeee1 */
    c->m_soc = true;
    c->m_local1 = c->m_local2 = c->m_local3 = false;
    c->m_gpu_t = c->m_cpu_t = c->m_cpu_mhz = c->m_nvme = true;
    c->m_gpu_busy = c->m_gpu_mhz = c->m_vram = c->m_fan = true;
    c->m_gpu_pm = false;
}

static void load_config(AppState *st) {
    init_default_config(st);
    FILE *f = fopen(st->conf_path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = line;
        char *v = eq + 1;
        size_t vl = strlen(v);
        while (vl > 0 && (v[vl - 1] == '\n' || v[vl - 1] == '\r')) v[--vl] = '\0';

        if (strcmp(k, "x") == 0) st->cfg.x = atoi(v);
        else if (strcmp(k, "y") == 0) st->cfg.y = atoi(v);
        else if (strcmp(k, "opacity") == 0) {
            int op = atoi(v);
            st->cfg.opacity = op < 40 ? 40 : (op > 100 ? 100 : op);
        } else if (strcmp(k, "lang") == 0) {
            if (strcmp(v, "ru") == 0) st->cfg.lang_ru = true;
            else if (strcmp(v, "en") == 0) st->cfg.lang_ru = false;
            else st->cfg.lang_ru = detect_system_is_ru();
        } else if (strcmp(k, "accent") == 0) parse_hex_color(v, &st->cfg.acc_r, &st->cfg.acc_g, &st->cfg.acc_b);
        else if (strcmp(k, "bg") == 0) parse_hex_color(v, &st->cfg.bg_r, &st->cfg.bg_g, &st->cfg.bg_b);
        else if (strcmp(k, "fg") == 0) parse_hex_color(v, &st->cfg.fg_r, &st->cfg.fg_g, &st->cfg.fg_b);
        else if (strcmp(k, "metric_soc") == 0) st->cfg.m_soc = atoi(v) != 0;
        else if (strcmp(k, "metric_local1") == 0) st->cfg.m_local1 = atoi(v) != 0;
        else if (strcmp(k, "metric_local2") == 0) st->cfg.m_local2 = atoi(v) != 0;
        else if (strcmp(k, "metric_local3") == 0) st->cfg.m_local3 = atoi(v) != 0;
        else if (strcmp(k, "metric_gpu_t") == 0) st->cfg.m_gpu_t = atoi(v) != 0;
        else if (strcmp(k, "metric_cpu_t") == 0) st->cfg.m_cpu_t = atoi(v) != 0;
        else if (strcmp(k, "metric_cpu_mhz") == 0) st->cfg.m_cpu_mhz = atoi(v) != 0;
        else if (strcmp(k, "metric_nvme") == 0) st->cfg.m_nvme = atoi(v) != 0;
        else if (strcmp(k, "metric_gpu_busy") == 0) st->cfg.m_gpu_busy = atoi(v) != 0;
        else if (strcmp(k, "metric_gpu_mhz") == 0) st->cfg.m_gpu_mhz = atoi(v) != 0;
        else if (strcmp(k, "metric_vram") == 0) st->cfg.m_vram = atoi(v) != 0;
        else if (strcmp(k, "metric_fan") == 0) st->cfg.m_fan = atoi(v) != 0;
        else if (strcmp(k, "metric_gpu_pm") == 0) st->cfg.m_gpu_pm = atoi(v) != 0;
    }
    fclose(f);
}

static void save_config(const AppState *st) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", st->conf_path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }
    FILE *f = fopen(st->conf_path, "w");
    if (!f) return;
    const OverlayConfig *c = &st->cfg;
    fprintf(f, "x=%d\ny=%d\nopacity=%d\nlang=%s\naccent=#%02x%02x%02x\nbg=#%02x%02x%02x\nfg=#%02x%02x%02x\n",
            c->x, c->y, c->opacity, c->lang_ru ? "ru" : "en",
            (int)lround(c->acc_r * 255), (int)lround(c->acc_g * 255), (int)lround(c->acc_b * 255),
            (int)lround(c->bg_r * 255), (int)lround(c->bg_g * 255), (int)lround(c->bg_b * 255),
            (int)lround(c->fg_r * 255), (int)lround(c->fg_g * 255), (int)lround(c->fg_b * 255));
    fprintf(f, "metric_soc=%d\nmetric_local1=%d\nmetric_local2=%d\nmetric_local3=%d\n",
            c->m_soc, c->m_local1, c->m_local2, c->m_local3);
    fprintf(f, "metric_gpu_t=%d\nmetric_cpu_t=%d\nmetric_cpu_mhz=%d\nmetric_nvme=%d\n",
            c->m_gpu_t, c->m_cpu_t, c->m_cpu_mhz, c->m_nvme);
    fprintf(f, "metric_gpu_busy=%d\nmetric_gpu_mhz=%d\nmetric_vram=%d\nmetric_fan=%d\nmetric_gpu_pm=%d\n",
            c->m_gpu_busy, c->m_gpu_mhz, c->m_vram, c->m_fan, c->m_gpu_pm);
    fclose(f);
}

static int count_visible_rows(const OverlayConfig *c) {
    int n = c->m_soc + c->m_local1 + c->m_local2 + c->m_local3 +
            c->m_gpu_t + c->m_cpu_t + c->m_cpu_mhz + c->m_nvme +
            c->m_gpu_busy + c->m_gpu_mhz + c->m_vram + c->m_fan + c->m_gpu_pm;
    return n > 0 ? n : 1;
}

static void clamp_pos(AppState *st) {
    int max_x = st->out_w - st->width;
    int max_y = st->out_h - st->height;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (st->cfg.x < 0) st->cfg.x = 0;
    if (st->cfg.y < 0) st->cfg.y = 0;
    if (st->cfg.x > max_x) st->cfg.x = max_x;
    if (st->cfg.y > max_y) st->cfg.y = max_y;
}

/* ── Wayland SHM and PangoCairo rendering ──────────────────────────────── */

static void buf_release(void *data, struct wl_buffer *wl_buffer) {
    (void)wl_buffer;
    ShmBuffer *b = (ShmBuffer *)data;
    b->busy = false;
}

static const struct wl_buffer_listener buf_listener = {
    .release = buf_release,
};

static void destroy_shm_buf(ShmBuffer *b) {
    if (b->buffer) wl_buffer_destroy(b->buffer);
    if (b->data && b->data != MAP_FAILED) munmap(b->data, b->size);
    memset(b, 0, sizeof(*b));
}

static ShmBuffer *acquire_shm_buf(AppState *st, int w, int h) {
    ShmBuffer *b = &st->bufs[st->cur_buf];
    if (b->busy) b = &st->bufs[1 - st->cur_buf];
    if (b->busy) return NULL;
    st->cur_buf = (b == &st->bufs[0]) ? 1 : 0;

    size_t stride = (size_t)w * 4;
    size_t sz = stride * (size_t)h;
    if (b->buffer && (b->width != w || b->height != h))
        destroy_shm_buf(b);

    if (!b->buffer) {
        int fd = memfd_create("ps5overlayd-shm", MFD_CLOEXEC);
        if (fd < 0) return NULL;
        if (ftruncate(fd, (off_t)sz) < 0) {
            close(fd);
            return NULL;
        }
        void *map = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            close(fd);
            return NULL;
        }
        struct wl_shm_pool *pool = wl_shm_create_pool(st->shm, fd, (int32_t)sz);
        b->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, (int32_t)stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        wl_buffer_add_listener(b->buffer, &buf_listener, b);
        b->data = map;
        b->size = sz;
        b->width = w;
        b->height = h;
    }
    return b;
}

static void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    const double deg = M_PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -90 * deg, 0 * deg);
    cairo_arc(cr, x + w - r, y + h - r, r, 0 * deg, 90 * deg);
    cairo_arc(cr, x + r, y + h - r, r, 90 * deg, 180 * deg);
    cairo_arc(cr, x + r, y + r, r, 180 * deg, 270 * deg);
    cairo_close_path(cr);
}

static void draw_text_line(cairo_t *cr, PangoLayout *pl, const char *font_desc,
                           const char *txt, double x, double y, double w,
                           double r, double g, double b, double a, PangoAlignment align) {
    PangoFontDescription *fd = pango_font_description_from_string(font_desc);
    pango_layout_set_font_description(pl, fd);
    pango_font_description_free(fd);
    pango_layout_set_width(pl, (int)(w * PANGO_SCALE));
    pango_layout_set_alignment(pl, align);
    pango_layout_set_text(pl, txt, -1);
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, pl);
}

static void temp_rgb(double t, double warn, double hot, double *r, double *g, double *b) {
    if (t >= hot) { *r = 0.85; *g = 0.31; *b = 0.24; }
    else if (t >= warn) { *r = 0.85; *g = 0.65; *b = 0.05; }
    else { *r = 0.94; *g = 0.93; *b = 0.88; }
}

static void render_frame(AppState *st) {
    if (!st->configured || !st->surface || !st->shm) return;

    int rows = count_visible_rows(&st->cfg);
    int target_w = OVERLAY_WIDTH;
    int target_h = PAD_Y * 2 + HEADER_H + rows * ROW_H + FOOTER_H;

    if (target_w != st->width || target_h != st->height) {
        st->width = target_w;
        st->height = target_h;
        clamp_pos(st);
        zwlr_layer_surface_v1_set_size(st->layer_surface, (uint32_t)st->width, (uint32_t)st->height);
        zwlr_layer_surface_v1_set_margin(st->layer_surface, st->cfg.y, 0, 0, st->cfg.x);
    }

    ShmBuffer *b = acquire_shm_buf(st, st->width, st->height);
    if (!b) return;

    memset(b->data, 0, b->size);
    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        (unsigned char *)b->data, CAIRO_FORMAT_ARGB32, st->width, st->height, st->width * 4);
    cairo_t *cr = cairo_create(cs);

    double bg_alpha = st->cfg.opacity / 100.0;
    draw_rounded_rect(cr, 0.5, 0.5, st->width - 1.0, st->height - 1.0, 8.0);
    cairo_set_source_rgba(cr, st->cfg.bg_r, st->cfg.bg_g, st->cfg.bg_b, bg_alpha);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, st->cfg.acc_r, st->cfg.acc_g, st->cfg.acc_b, 0.55);
    cairo_stroke(cr);

    PangoLayout *pl = pango_cairo_create_layout(cr);

    /* Header */
    draw_text_line(cr, pl, "Noto Sans Bold 9", "PS5 Monitor",
                   PAD_X, PAD_Y, st->width - PAD_X * 2 - 20,
                   st->cfg.acc_r, st->cfg.acc_g, st->cfg.acc_b, 1.0, PANGO_ALIGN_LEFT);

    /* Close button in the top-right corner */
    double cx = st->width - 18.0, cy = PAD_Y + 3.0;
    cairo_set_line_width(cr, 1.5);
    cairo_set_source_rgba(cr, st->cfg.fg_r, st->cfg.fg_g, st->cfg.fg_b, 0.7);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx + 8, cy + 8);
    cairo_move_to(cr, cx + 8, cy);
    cairo_line_to(cr, cx, cy + 8);
    cairo_stroke(cr);

    /* Divider */
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, st->cfg.acc_r, st->cfg.acc_g, st->cfg.acc_b, 0.30);
    cairo_move_to(cr, PAD_X, PAD_Y + HEADER_H - 5);
    cairo_line_to(cr, st->width - PAD_X, PAD_Y + HEADER_H - 5);
    cairo_stroke(cr);

    /* Metrics */
    double y = PAD_Y + HEADER_H;
    const double col_w = st->width - PAD_X * 2;
    const OverlayConfig *c = &st->cfg;
    const SensorData *s = &st->sensors;
    char vbuf[64];
    const double fr = c->fg_r, fg = c->fg_g, fb = c->fg_b;
    const double kr = fr * 0.88, kg = fg * 0.92, kb = fb * 0.92;
    const char *na_str = c->lang_ru ? "н/д" : "N/A";
    const char *mhz_unit = c->lang_ru ? "МГц" : "MHz";
    const char *mb_unit = c->lang_ru ? "МБ" : "MB";

    #define DRAW_TEMP(flag, label, opt, warn, hot) do { \
        if (flag) { \
            double r = fr, g = fg, b_ = fb; \
            if ((opt).valid) { \
                snprintf(vbuf, sizeof(vbuf), "%.1f °C", (opt).val); \
                temp_rgb((opt).val, (warn), (hot), &r, &g, &b_); \
            } else { \
                snprintf(vbuf, sizeof(vbuf), "%s", na_str); \
            } \
            draw_text_line(cr, pl, "Noto Sans 8.5", (label), PAD_X, y, col_w * 0.5, kr, kg, kb, 0.85, PANGO_ALIGN_LEFT); \
            draw_text_line(cr, pl, "Noto Sans Mono 8.5", vbuf, PAD_X, y, col_w, r, g, b_, 1.0, PANGO_ALIGN_RIGHT); \
            y += ROW_H; \
        } \
    } while (0)

    DRAW_TEMP(c->m_soc,    "MainSoC:",  s->soc,    75.0, 90.0);
    DRAW_TEMP(c->m_local1, "Local1:",   s->local1, 70.0, 85.0);
    DRAW_TEMP(c->m_local2, "Local2:",   s->local2, 50.0, 65.0);
    DRAW_TEMP(c->m_local3, "Local3:",   s->local3, 70.0, 85.0);
    DRAW_TEMP(c->m_gpu_t,  "GPU edge:", s->gpu_t,  75.0, 90.0);
    DRAW_TEMP(c->m_cpu_t,  "CPU Tctl:", s->cpu_t,  75.0, 90.0);

    if (c->m_cpu_mhz) {
        if (s->cpu_mhz.valid) snprintf(vbuf, sizeof(vbuf), "%d %s", s->cpu_mhz.val, mhz_unit);
        else snprintf(vbuf, sizeof(vbuf), "—");
        draw_text_line(cr, pl, "Noto Sans 8.5", "CPU freq:", PAD_X, y, col_w * 0.5, kr, kg, kb, 0.85, PANGO_ALIGN_LEFT);
        draw_text_line(cr, pl, "Noto Sans Mono 8.5", vbuf, PAD_X, y, col_w, fr, fg, fb, 1.0, PANGO_ALIGN_RIGHT);
        y += ROW_H;
    }

    DRAW_TEMP(c->m_nvme, "NVMe:", s->nvme_t, 60.0, 70.0);

    if (c->m_gpu_busy) {
        double r = fr, g = fg, b_ = fb;
        if (s->gpu_busy.valid) {
            snprintf(vbuf, sizeof(vbuf), "%d%%", s->gpu_busy.val);
            if (s->gpu_busy.val > 95) { r = 0.85; g = 0.31; b_ = 0.24; }
            else if (s->gpu_busy.val > 70) { r = 0.85; g = 0.65; b_ = 0.05; }
        } else snprintf(vbuf, sizeof(vbuf), "%s", na_str);
        draw_text_line(cr, pl, "Noto Sans 8.5", "GPU load:", PAD_X, y, col_w * 0.5, kr, kg, kb, 0.85, PANGO_ALIGN_LEFT);
        draw_text_line(cr, pl, "Noto Sans Mono 8.5", vbuf, PAD_X, y, col_w, r, g, b_, 1.0, PANGO_ALIGN_RIGHT);
        y += ROW_H;
    }

    if (c->m_gpu_mhz) {
        if (s->gpu_mhz.valid) snprintf(vbuf, sizeof(vbuf), "%d %s", s->gpu_mhz.val, mhz_unit);
        else snprintf(vbuf, sizeof(vbuf), "—");
        draw_text_line(cr, pl, "Noto Sans 8.5", "GPU freq:", PAD_X, y, col_w * 0.5, kr, kg, kb, 0.85, PANGO_ALIGN_LEFT);
        draw_text_line(cr, pl, "Noto Sans Mono 8.5", vbuf, PAD_X, y, col_w, fr, fg, fb, 1.0, PANGO_ALIGN_RIGHT);
        y += ROW_H;
    }

    if (c->m_vram) {
        double r = fr, g = fg, b_ = fb;
        if (s->vram_used_mb.valid && s->vram_total_mb.valid) {
            snprintf(vbuf, sizeof(vbuf), "%d/%d %s", s->vram_used_mb.val, s->vram_total_mb.val, mb_unit);
            double ratio = (double)s->vram_used_mb.val / s->vram_total_mb.val;
            if (ratio > 0.9) { r = 0.85; g = 0.31; b_ = 0.24; }
            else if (ratio > 0.75) { r = 0.85; g = 0.65; b_ = 0.05; }
        } else snprintf(vbuf, sizeof(vbuf), "—");
        draw_text_line(cr, pl, "Noto Sans 8.5", "VRAM:", PAD_X, y, col_w * 0.5, kr, kg, kb, 0.85, PANGO_ALIGN_LEFT);
        draw_text_line(cr, pl, "Noto Sans Mono 8.5", vbuf, PAD_X, y, col_w, r, g, b_, 1.0, PANGO_ALIGN_RIGHT);
        y += ROW_H;
    }

    if (c->m_fan) {
        if (s->fan_pct.valid) snprintf(vbuf, sizeof(vbuf), "%d%%", s->fan_pct.val);
        else snprintf(vbuf, sizeof(vbuf), "—");
        draw_text_line(cr, pl, "Noto Sans 8.5", "Fan PWM:", PAD_X, y, col_w * 0.5, kr, kg, kb, 0.85, PANGO_ALIGN_LEFT);
        draw_text_line(cr, pl, "Noto Sans Mono 8.5", vbuf, PAD_X, y, col_w, fr, fg, fb, 1.0, PANGO_ALIGN_RIGHT);
        y += ROW_H;
    }

    if (c->m_gpu_pm) {
        bool act = strcmp(s->gpu_pm, "active") == 0;
        draw_text_line(cr, pl, "Noto Sans 8.5", "GPU PM:", PAD_X, y, col_w * 0.5, kr, kg, kb, 0.85, PANGO_ALIGN_LEFT);
        draw_text_line(cr, pl, "Noto Sans Mono 8.5", s->gpu_pm, PAD_X, y, col_w,
                       act ? 0.20 : 0.85, act ? 0.82 : 0.65, act ? 0.48 : 0.05, 1.0, PANGO_ALIGN_RIGHT);
    }

    draw_text_line(cr, pl, "Noto Sans 7", c->lang_ru ? "Потяните для перемещения" : "Drag to move",
                   PAD_X, st->height - FOOTER_H - 2, col_w, fr * 0.8, fg * 0.85, fb * 0.82, 0.55, PANGO_ALIGN_CENTER);

    g_object_unref(pl);
    cairo_destroy(cr);
    cairo_surface_destroy(cs);

    b->busy = true;
    wl_surface_attach(st->surface, b->buffer, 0, 0);
    wl_surface_damage_buffer(st->surface, 0, 0, st->width, st->height);
    wl_surface_commit(st->surface);
    wl_display_flush(st->display);
}

/* ── Wayland callbacks ─────────────────────────────────────────────────── */

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *surf,
                            uint32_t serial, uint32_t w, uint32_t h) {
    (void)w; (void)h;
    AppState *st = (AppState *)data;
    zwlr_layer_surface_v1_ack_configure(surf, serial);
    st->configured = true;
    render_frame(st);
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *surf) {
    (void)surf;
    AppState *st = (AppState *)data;
    st->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_configure,
    .closed = layer_closed,
};

static void ptr_enter(void *data, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *s, wl_fixed_t x, wl_fixed_t y) {
    (void)p; (void)serial; (void)s;
    AppState *st = (AppState *)data;
    st->ptr_x = wl_fixed_to_double(x);
    st->ptr_y = wl_fixed_to_double(y);
}

static void ptr_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) {
    (void)data; (void)p; (void)serial; (void)s;
}

static void ptr_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    (void)p; (void)time;
    AppState *st = (AppState *)data;
    st->ptr_x = wl_fixed_to_double(x);
    st->ptr_y = wl_fixed_to_double(y);
    if (st->dragging) {
        int dx = (int)lround(st->ptr_x - st->drag_start_x);
        int dy = (int)lround(st->ptr_y - st->drag_start_y);
        if (dx != 0 || dy != 0) {
            st->cfg.x += dx;
            st->cfg.y += dy;
            clamp_pos(st);
            zwlr_layer_surface_v1_set_margin(st->layer_surface, st->cfg.y, 0, 0, st->cfg.x);
            wl_surface_commit(st->surface);
            wl_display_flush(st->display);
        }
    }
}

static void ptr_button(void *data, struct wl_pointer *p, uint32_t serial,
                       uint32_t time, uint32_t button, uint32_t state) {
    (void)p; (void)serial; (void)time;
    AppState *st = (AppState *)data;
    if (button != BTN_LEFT) return;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (st->ptr_x >= st->width - 26 && st->ptr_y <= 24) {
            st->running = false;
            return;
        }
        st->dragging = true;
        st->drag_start_x = st->ptr_x;
        st->drag_start_y = st->ptr_y;
    } else if (state == WL_POINTER_BUTTON_STATE_RELEASED && st->dragging) {
        st->dragging = false;
        save_config(st);
    }
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) { (void)d;(void)p;(void)t;(void)a;(void)v; }
static void ptr_frame(void *d, struct wl_pointer *p) { (void)d;(void)p; }
static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t s) { (void)d;(void)p;(void)s; }
static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) { (void)d;(void)p;(void)t;(void)a; }
static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) { (void)d;(void)p;(void)a;(void)v; }
static void ptr_axis_value120(void *d, struct wl_pointer *p, uint32_t a, int32_t v) { (void)d;(void)p;(void)a;(void)v; }
static void ptr_axis_rel_dir(void *d, struct wl_pointer *p, uint32_t a, uint32_t r) { (void)d;(void)p;(void)a;(void)r; }

static const struct wl_pointer_listener ptr_listener = {
    .enter = ptr_enter,
    .leave = ptr_leave,
    .motion = ptr_motion,
    .button = ptr_button,
    .axis = ptr_axis,
    .frame = ptr_frame,
    .axis_source = ptr_axis_source,
    .axis_stop = ptr_axis_stop,
    .axis_discrete = ptr_axis_discrete,
    .axis_value120 = ptr_axis_value120,
    .axis_relative_direction = ptr_axis_rel_dir,
};

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps) {
    AppState *st = (AppState *)data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !st->pointer) {
        st->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(st->pointer, &ptr_listener, st);
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d;(void)s;(void)n; }

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_caps,
    .name = seat_name,
};

static void out_geom(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sp, const char *mk, const char *md, int32_t tr) {
    (void)d;(void)o;(void)x;(void)y;(void)pw;(void)ph;(void)sp;(void)mk;(void)md;(void)tr;
}
static void out_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t ref) {
    (void)o; (void)ref;
    AppState *st = (AppState *)data;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        st->out_w = w;
        st->out_h = h;
    }
}
static void out_done(void *d, struct wl_output *o) { (void)d;(void)o; }
static void out_scale(void *d, struct wl_output *o, int32_t f) { (void)d;(void)o;(void)f; }
static void out_name(void *d, struct wl_output *o, const char *n) { (void)d;(void)o;(void)n; }
static void out_desc(void *d, struct wl_output *o, const char *ds) { (void)d;(void)o;(void)ds; }

static const struct wl_output_listener out_listener = {
    .geometry = out_geom,
    .mode = out_mode,
    .done = out_done,
    .scale = out_scale,
    .name = out_name,
    .description = out_desc,
};

static void reg_global(void *data, struct wl_registry *reg, uint32_t id, const char *iface, uint32_t ver) {
    AppState *st = (AppState *)data;
    if (strcmp(iface, wl_compositor_interface.name) == 0)
        st->compositor = wl_registry_bind(reg, id, &wl_compositor_interface, ver < 4 ? ver : 4);
    else if (strcmp(iface, wl_shm_interface.name) == 0)
        st->shm = wl_registry_bind(reg, id, &wl_shm_interface, 1);
    else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0)
        st->layer_shell = wl_registry_bind(reg, id, &zwlr_layer_shell_v1_interface, ver < 4 ? ver : 4);
    else if (strcmp(iface, wl_seat_interface.name) == 0) {
        st->seat = wl_registry_bind(reg, id, &wl_seat_interface, ver < 5 ? ver : 5);
        wl_seat_add_listener(st->seat, &seat_listener, st);
    } else if (strcmp(iface, wl_output_interface.name) == 0 && !st->output) {
        st->output = wl_registry_bind(reg, id, &wl_output_interface, ver < 2 ? ver : 2);
        wl_output_add_listener(st->output, &out_listener, st);
    }
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t id) { (void)d;(void)r;(void)id; }

static const struct wl_registry_listener reg_listener = {
    .global = reg_global,
    .global_remove = reg_remove,
};

/* ── Entry point and event loop ────────────────────────────────────────── */

int main(void) {
    AppState st;
    memset(&st, 0, sizeof(st));
    st.out_w = 1920;
    st.out_h = 1080;
    st.running = true;

    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(st.conf_path, sizeof(st.conf_path), "%s/.config/ps5linuxtools/overlay.conf", home);

    const char *xrd = getenv("XDG_RUNTIME_DIR");
    if (xrd && xrd[0])
        snprintf(st.pid_path, sizeof(st.pid_path), "%s/ps5overlayd.pid", xrd);
    else
        snprintf(st.pid_path, sizeof(st.pid_path), "/tmp/ps5overlayd-%u.pid", (unsigned)getuid());

    /* Check if the daemon is already running */
    FILE *pf = fopen(st.pid_path, "r");
    if (pf) {
        pid_t old_pid = 0;
        if (fscanf(pf, "%d", &old_pid) == 1 && old_pid > 0 && kill(old_pid, 0) == 0) {
            fclose(pf);
            fprintf(stderr, "ps5overlayd is already running (PID %d)\n", old_pid);
            return 0;
        }
        fclose(pf);
    }

    st.display = wl_display_connect(NULL);
    if (!st.display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return 1;
    }

    st.registry = wl_display_get_registry(st.display);
    wl_registry_add_listener(st.registry, &reg_listener, &st);
    wl_display_roundtrip(st.display);
    wl_display_roundtrip(st.display); /* receive output mode from wl_output */

    if (!st.compositor || !st.shm || !st.layer_shell) {
        fprintf(stderr, "Compositor lacks wl_compositor / wl_shm / zwlr_layer_shell_v1\n");
        return 1;
    }

    /* Write PID file */
    pf = fopen(st.pid_path, "w");
    if (pf) {
        fprintf(pf, "%d\n", (int)getpid());
        fclose(pf);
    }

    find_hwmon_paths(&st);
    load_config(&st);
    poll_sensors(&st);

    int rows = count_visible_rows(&st.cfg);
    st.width = OVERLAY_WIDTH;
    st.height = PAD_Y * 2 + HEADER_H + rows * ROW_H + FOOTER_H;
    clamp_pos(&st);

    st.surface = wl_compositor_create_surface(st.compositor);
    st.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        st.layer_shell, st.surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "ps5tools-overlay");
    zwlr_layer_surface_v1_add_listener(st.layer_surface, &layer_listener, &st);
    zwlr_layer_surface_v1_set_anchor(st.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_exclusive_zone(st.layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(st.layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_set_margin(st.layer_surface, st.cfg.y, 0, 0, st.cfg.x);
    zwlr_layer_surface_v1_set_size(st.layer_surface, (uint32_t)st.width, (uint32_t)st.height);
    wl_surface_commit(st.surface);
    wl_display_flush(st.display);

    /* Signals and 2-second polling timer */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sig_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    struct itimerspec its = {
        .it_interval = {.tv_sec = 2, .tv_nsec = 0},
        .it_value    = {.tv_sec = 2, .tv_nsec = 0},
    };
    timerfd_settime(tfd, 0, &its, NULL);

    int wl_fd = wl_display_get_fd(st.display);
    while (st.running) {
        while (wl_display_prepare_read(st.display) != 0) {
            if (wl_display_dispatch_pending(st.display) < 0) {
                st.running = false;
                break;
            }
        }
        if (!st.running) {
            wl_display_cancel_read(st.display);
            break;
        }
        wl_display_flush(st.display);

        struct pollfd fds[3] = {
            {.fd = wl_fd,  .events = POLLIN},
            {.fd = tfd,    .events = POLLIN},
            {.fd = sig_fd, .events = POLLIN},
        };
        int ret = poll(fds, 3, -1);
        if (ret < 0) {
            wl_display_cancel_read(st.display);
            if (errno == EINTR) continue;
            break;
        }

        if (fds[0].revents & POLLIN) {
            if (wl_display_read_events(st.display) < 0 || wl_display_dispatch_pending(st.display) < 0)
                st.running = false;
        } else {
            wl_display_cancel_read(st.display);
        }

        if (fds[1].revents & POLLIN) {
            uint64_t exp;
            if (read(tfd, &exp, sizeof(exp)) > 0) {
                poll_sensors(&st);
                render_frame(&st);
            }
        }

        if (fds[2].revents & POLLIN) {
            struct signalfd_siginfo si;
            while (read(sig_fd, &si, sizeof(si)) == sizeof(si)) {
                if (si.ssi_signo == SIGUSR1) {
                    load_config(&st);
                    clamp_pos(&st);
                    render_frame(&st);
                    zwlr_layer_surface_v1_set_margin(st.layer_surface, st.cfg.y, 0, 0, st.cfg.x);
                    wl_surface_commit(st.surface);
                    wl_display_flush(st.display);
                } else {
                    st.running = false;
                }
            }
        }
    }

    close(tfd);
    close(sig_fd);
    unlink(st.pid_path);
    destroy_shm_buf(&st.bufs[0]);
    destroy_shm_buf(&st.bufs[1]);
    if (st.layer_surface) zwlr_layer_surface_v1_destroy(st.layer_surface);
    if (st.surface) wl_surface_destroy(st.surface);
    if (st.pointer) wl_pointer_destroy(st.pointer);
    if (st.seat) wl_seat_destroy(st.seat);
    if (st.output) wl_output_destroy(st.output);
    if (st.layer_shell) zwlr_layer_shell_v1_destroy(st.layer_shell);
    if (st.shm) wl_shm_destroy(st.shm);
    if (st.compositor) wl_compositor_destroy(st.compositor);
    if (st.registry) wl_registry_destroy(st.registry);
    wl_display_disconnect(st.display);
    return 0;
}
