/*
 * 0xFX — GUI application entry point
 *
 * SDL2 + OpenGL 3.3 + Dear ImGui
 * Borderless window with custom title bar (no Windows chrome)
 *
 * Layout: BIAS FX-inspired signal chain flow
 *   Top:           Toolbar (logo, tuner, LIVE, _ [] X)
 *   Middle-top:    Signal chain — horizontal node flow
 *   Middle-bottom: Detail view — knobs for selected node
 *   Bottom:        Status bar with level meters
 */
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_syswm.h>

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>  /* GET_X_LPARAM, GET_Y_LPARAM */
#include <shellapi.h>  /* ShellExecuteA for Open Folder */
#else
#include <unistd.h>    /* fork, execlp, dup2, _exit, close */
#include <fcntl.h>     /* open, O_RDWR */
#endif

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

extern "C" {
#include "../engine/fx_engine.h"
#include "../audio/audio_device.h"
#include "../audio/recorder.h"
#include "../audio/midi_input.h"
#include "../core/log.h"
#include "../core/crash.h"
#include "knobs.h"
#include "texture.h"
}

#include <stdio.h>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#define fx_strcasecmp _stricmp
#else
#include <dirent.h>
#include <strings.h>
#define fx_strcasecmp strcasecmp
#endif

extern "C" {
#include "cJSON.h"
#include "nfd.h"
}

/* ── Session config helpers (TASK-307) ───────────────────────── */

#ifdef _WIN32
#define PATH_SEP "\\"
static const char *get_config_dir(void) {
    static char buf[512];
    const char *appdata = getenv("APPDATA");
    if (!appdata) appdata = ".";
    snprintf(buf, sizeof(buf), "%s\\0xFX", appdata);
    return buf;
}
#else
#define PATH_SEP "/"
static const char *get_config_dir(void) {
    static char buf[512];
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(buf, sizeof(buf), "%s/.0xfx", home);
    return buf;
}
#endif

static void ensure_dir(const char *path) {
#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0755);
#endif
}

static const char *get_config_path(void) {
    static char buf[600];
    snprintf(buf, sizeof(buf), "%s" PATH_SEP "config.json", get_config_dir());
    return buf;
}

struct SessionConfig {
    int   input_device_idx;
    int   output_device_idx;
    int   window_w;
    int   window_h;
    int   buf_size_idx;
    int   sr_idx;
    float input_gain_db;   /* -24..+12 dB, default 0.0 */
    bool  input_pad;       /* -20 dB pad toggle, default false */
    int   theme_id;        /* fx_theme_id_t */
};

static void session_config_defaults(SessionConfig *cfg) {
    cfg->input_device_idx  = -1;
    cfg->output_device_idx = -1;
    cfg->window_w          = 1400;
    cfg->window_h          = 800;
    cfg->buf_size_idx      = 2;
    cfg->sr_idx            = 0;
    cfg->input_gain_db     = 0.0f;
    cfg->input_pad         = false;
    cfg->theme_id          = 0;
}

static bool session_config_load(SessionConfig *cfg) {
    session_config_defaults(cfg);
    FILE *f = fopen(get_config_path(), "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 65536) { fclose(f); return false; }
    char *cbuf = (char *)malloc(sz + 1);
    if (!cbuf) { fclose(f); return false; }
    fread(cbuf, 1, sz, f);
    cbuf[sz] = '\0';
    fclose(f);
    cJSON *root = cJSON_Parse(cbuf);
    free(cbuf);
    if (!root) return false;
    cJSON *v;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "input_device"))  && cJSON_IsNumber(v)) cfg->input_device_idx  = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "output_device")) && cJSON_IsNumber(v)) cfg->output_device_idx = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "window_w"))      && cJSON_IsNumber(v)) cfg->window_w          = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "window_h"))      && cJSON_IsNumber(v)) cfg->window_h          = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "buf_size_idx"))  && cJSON_IsNumber(v)) cfg->buf_size_idx      = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "sr_idx"))        && cJSON_IsNumber(v)) cfg->sr_idx            = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "input_gain_db")) && cJSON_IsNumber(v)) cfg->input_gain_db     = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "input_pad"))     && cJSON_IsBool(v))   cfg->input_pad         = (bool)cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "theme"))         && cJSON_IsNumber(v)) cfg->theme_id          = (int)v->valuedouble;
    cJSON_Delete(root);
    return true;
}

static void session_config_save(const SessionConfig *cfg) {
    ensure_dir(get_config_dir());
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "input_device",  cfg->input_device_idx);
    cJSON_AddNumberToObject(root, "output_device", cfg->output_device_idx);
    cJSON_AddNumberToObject(root, "window_w",      cfg->window_w);
    cJSON_AddNumberToObject(root, "window_h",      cfg->window_h);
    cJSON_AddNumberToObject(root, "buf_size_idx",  cfg->buf_size_idx);
    cJSON_AddNumberToObject(root, "sr_idx",        cfg->sr_idx);
    cJSON_AddNumberToObject(root, "input_gain_db", cfg->input_gain_db);
    cJSON_AddBoolToObject  (root, "input_pad",     cfg->input_pad);
    cJSON_AddNumberToObject(root, "theme",         cfg->theme_id);
    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return;
    FILE *f = fopen(get_config_path(), "w");
    if (f) { fputs(json, f); fclose(f); }
    free(json);
}

/* ── Preset Browser ─────────────────────────────────────────── */

struct PresetEntry {
    char name[128];
    char description[256];
    char category[32];
    char path[512];       /* relative path to .0xfx file */
    bool is_factory;
};

#define MAX_BROWSER_PRESETS 128
static PresetEntry s_browser_presets[MAX_BROWSER_PRESETS];
static int s_browser_preset_count = 0;
static bool s_browser_needs_scan = true;

static const char *s_preset_categories[] = {
    "Classic", "80s", "90s", "Modern", "Heavy", "Experimental", "User"
};
static const int s_preset_category_count = 7;

/* Recording directory state */
static char s_rec_dir[512] = "";
static bool s_rec_dir_modal = false;
static char s_rec_dir_edit[512] = "";
static bool s_rec_dir_inited = false;

/* Last finished recording — shown in the "Recording saved" popup. */
static char s_last_rec_path[768] = "";
static bool s_rec_saved_popup_open = false;

/* Launch the OS file browser for a directory. Uses ShellExecute on
 * Windows and fork/execlp on POSIX so the path is passed as an argv
 * entry, not interpolated into a shell command — no quoting traps,
 * no injection risk if the path contains ", `, $, or ;. */
static void fx_open_folder(const char *path) {
    if (!path || !*path) return;
#ifdef _WIN32
    ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWDEFAULT);
#else
    pid_t pid = fork();
    if (pid < 0) {
        FX_WARN("fx_open_folder: fork failed");
        return;
    }
    if (pid == 0) {
        /* Child — detach stdio so the opener doesn't inherit our TTY. */
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, 0);
            dup2(devnull, 1);
            dup2(devnull, 2);
            if (devnull > 2) close(devnull);
        }
#  ifdef __APPLE__
        execlp("open", "open", path, (char *)NULL);
#  else
        execlp("xdg-open", "xdg-open", path, (char *)NULL);
#  endif
        _exit(127); /* exec failed */
    }
    /* Parent — don't wait. The opener forks its own long-lived GUI
     * process almost immediately, so the zombie reaps quickly. */
#endif
}

/* Scan a single .0xfx file and extract metadata via cJSON */
static bool preset_scan_file(const char *path, PresetEntry *entry, bool is_factory) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > 65536) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return false;

    cJSON *name_j = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *desc_j = cJSON_GetObjectItemCaseSensitive(root, "description");
    cJSON *cat_j  = cJSON_GetObjectItemCaseSensitive(root, "category");

    if (name_j && cJSON_IsString(name_j))
        snprintf(entry->name, sizeof(entry->name), "%s", name_j->valuestring);
    else
        snprintf(entry->name, sizeof(entry->name), "Untitled");

    if (desc_j && cJSON_IsString(desc_j))
        snprintf(entry->description, sizeof(entry->description), "%s", desc_j->valuestring);
    else
        entry->description[0] = '\0';

    if (cat_j && cJSON_IsString(cat_j))
        snprintf(entry->category, sizeof(entry->category), "%s", cat_j->valuestring);
    else
        snprintf(entry->category, sizeof(entry->category), "User");

    snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->is_factory = is_factory;

    cJSON_Delete(root);
    return true;
}

/* Scan a directory for .0xfx files (non-recursive) */
static void preset_scan_dir(const char *dirpath, bool is_factory, const char *category_override) {
#ifdef _WIN32
    char pattern[600];
    snprintf(pattern, sizeof(pattern), "%s\\*.0xfx", dirpath);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (s_browser_preset_count >= MAX_BROWSER_PRESETS) break;
        char fullpath[600];
        snprintf(fullpath, sizeof(fullpath), "%s\\%s", dirpath, fd.cFileName);
        PresetEntry *e = &s_browser_presets[s_browser_preset_count];
        if (preset_scan_file(fullpath, e, is_factory)) {
            if (category_override && category_override[0])
                snprintf(e->category, sizeof(e->category), "%s", category_override);
            s_browser_preset_count++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dirpath);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (s_browser_preset_count >= MAX_BROWSER_PRESETS) break;
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len < 5 || strcmp(name + len - 5, ".0xfx") != 0) continue;
        char fullpath[600];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, name);
        PresetEntry *e = &s_browser_presets[s_browser_preset_count];
        if (preset_scan_file(fullpath, e, is_factory)) {
            if (category_override && category_override[0])
                snprintf(e->category, sizeof(e->category), "%s", category_override);
            s_browser_preset_count++;
        }
    }
    closedir(d);
#endif
}

/* Full preset library scan — factory subdirs + user presets */
static void preset_browser_scan(void) {
    s_browser_preset_count = 0;

    /* Factory presets in subdirectories */
    const char *factory_cats[] = { "classic", "80s", "90s", "modern", "heavy", "experimental" };
    const char *cat_labels[]   = { "Classic", "80s", "90s", "Modern", "Heavy", "Experimental" };
    for (int i = 0; i < 6; i++) {
        char dirpath[600];
        snprintf(dirpath, sizeof(dirpath), "presets/factory/%s", factory_cats[i]);
        preset_scan_dir(dirpath, true, cat_labels[i]);
        /* Try ../presets/ too (for running from build dir) */
        snprintf(dirpath, sizeof(dirpath), "../presets/factory/%s", factory_cats[i]);
        preset_scan_dir(dirpath, true, cat_labels[i]);
    }

    /* User presets in root presets/ (excluding factory/ and last_session) */
    const char *user_dirs[] = { "presets", "../presets" };
    for (int d = 0; d < 2; d++) {
#ifdef _WIN32
        char pattern[600];
        snprintf(pattern, sizeof(pattern), "%s\\*.0xfx", user_dirs[d]);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (s_browser_preset_count >= MAX_BROWSER_PRESETS) break;
            if (strstr(fd.cFileName, "last_session") != NULL) continue;
            char fullpath[600];
            snprintf(fullpath, sizeof(fullpath), "%s\\%s", user_dirs[d], fd.cFileName);
            /* Check if already scanned (avoid duplicates from presets/ and ../presets/) */
            bool dup = false;
            for (int k = 0; k < s_browser_preset_count; k++) {
                /* Compare filenames only */
                const char *existing = strrchr(s_browser_presets[k].path, '\\');
                if (!existing) existing = strrchr(s_browser_presets[k].path, '/');
                if (!existing) existing = s_browser_presets[k].path;
                else existing++;
                if (strcmp(existing, fd.cFileName) == 0) { dup = true; break; }
            }
            if (dup) continue;
            PresetEntry *e = &s_browser_presets[s_browser_preset_count];
            if (preset_scan_file(fullpath, e, false)) {
                if (e->category[0] == '\0' || strcmp(e->category, "User") == 0)
                    snprintf(e->category, sizeof(e->category), "User");
                s_browser_preset_count++;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
#else
        DIR *dd = opendir(user_dirs[d]);
        if (!dd) continue;
        struct dirent *ent;
        while ((ent = readdir(dd)) != NULL) {
            if (s_browser_preset_count >= MAX_BROWSER_PRESETS) break;
            const char *name = ent->d_name;
            size_t len = strlen(name);
            if (len < 5 || strcmp(name + len - 5, ".0xfx") != 0) continue;
            if (strstr(name, "last_session") != NULL) continue;
            char fullpath[600];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", user_dirs[d], name);
            bool dup = false;
            for (int k = 0; k < s_browser_preset_count; k++) {
                const char *existing = strrchr(s_browser_presets[k].path, '/');
                if (!existing) existing = s_browser_presets[k].path;
                else existing++;
                if (strcmp(existing, name) == 0) { dup = true; break; }
            }
            if (dup) continue;
            PresetEntry *e = &s_browser_presets[s_browser_preset_count];
            if (preset_scan_file(fullpath, e, false)) {
                if (e->category[0] == '\0' || strcmp(e->category, "User") == 0)
                    snprintf(e->category, sizeof(e->category), "User");
                s_browser_preset_count++;
            }
        }
        closedir(dd);
#endif
    }

    s_browser_needs_scan = false;
    FX_INFO("Preset browser: scanned %d presets", s_browser_preset_count);
}

/* ── Custom cab library ──────────────────────────────────────── */

#define FX_MAX_CUSTOM_CABS 512

struct custom_cab_entry_t {
    char ir_path[1024];
    char name[64];
    char image_path[1024];
};

static custom_cab_entry_t s_custom_cabs[FX_MAX_CUSTOM_CABS];
static int s_custom_cab_count = 0;

static const char *custom_cabs_path(void) {
    static char buf[1024];
    snprintf(buf, sizeof(buf), "%s%scustom_cabs.json", get_config_dir(), PATH_SEP);
    return buf;
}

static void basename_no_ext(const char *path, char *out, size_t out_sz) {
    if (!path || !out || out_sz == 0) return;
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    const char *base = slash ? slash + 1 : path;
    strncpy(out, base, out_sz - 1);
    out[out_sz - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

static int custom_cab_find(const char *ir_path) {
    if (!ir_path) return -1;
    for (int i = 0; i < s_custom_cab_count; i++) {
        if (strcmp(s_custom_cabs[i].ir_path, ir_path) == 0) return i;
    }
    return -1;
}

static int custom_cab_add(const char *ir_path) {
    int idx = custom_cab_find(ir_path);
    if (idx >= 0) return idx;
    if (s_custom_cab_count >= FX_MAX_CUSTOM_CABS) {
        FX_WARN("Custom cab library full (%d) — cannot add %s",
                FX_MAX_CUSTOM_CABS, ir_path);
        return -1;
    }
    custom_cab_entry_t &e = s_custom_cabs[s_custom_cab_count];
    memset(&e, 0, sizeof(e));
    strncpy(e.ir_path, ir_path, sizeof(e.ir_path) - 1);
    basename_no_ext(ir_path, e.name, sizeof(e.name));
    return s_custom_cab_count++;
}

static void custom_cab_remove(int idx) {
    if (idx < 0 || idx >= s_custom_cab_count) return;
    for (int i = idx; i < s_custom_cab_count - 1; i++)
        s_custom_cabs[i] = s_custom_cabs[i + 1];
    s_custom_cab_count--;
    memset(&s_custom_cabs[s_custom_cab_count], 0, sizeof(custom_cab_entry_t));
}

static void custom_cabs_load(void) {
    s_custom_cab_count = 0;
    FILE *fp = fopen(custom_cabs_path(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 1 << 20) { fclose(fp); return; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, (size_t)sz, fp);
    buf[sz] = '\0';
    fclose(fp);
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root || !cJSON_IsArray(root)) { if (root) cJSON_Delete(root); return; }
    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n && s_custom_cab_count < FX_MAX_CUSTOM_CABS; i++) {
        cJSON *it = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsObject(it)) continue;
        cJSON *p   = cJSON_GetObjectItem(it, "ir_path");
        cJSON *nm  = cJSON_GetObjectItem(it, "name");
        cJSON *img = cJSON_GetObjectItem(it, "image_path");
        if (!cJSON_IsString(p) || !p->valuestring || !*p->valuestring) continue;
        custom_cab_entry_t &e = s_custom_cabs[s_custom_cab_count++];
        memset(&e, 0, sizeof(e));
        strncpy(e.ir_path, p->valuestring, sizeof(e.ir_path) - 1);
        if (cJSON_IsString(nm) && nm->valuestring)
            strncpy(e.name, nm->valuestring, sizeof(e.name) - 1);
        else
            basename_no_ext(e.ir_path, e.name, sizeof(e.name));
        if (cJSON_IsString(img) && img->valuestring)
            strncpy(e.image_path, img->valuestring, sizeof(e.image_path) - 1);
    }
    cJSON_Delete(root);
    FX_INFO("Custom cabs: loaded %d from %s", s_custom_cab_count, custom_cabs_path());
}

static void custom_cabs_save(void) {
    ensure_dir(get_config_dir());
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s_custom_cab_count; i++) {
        cJSON *it = cJSON_CreateObject();
        cJSON_AddStringToObject(it, "ir_path",    s_custom_cabs[i].ir_path);
        cJSON_AddStringToObject(it, "name",       s_custom_cabs[i].name);
        cJSON_AddStringToObject(it, "image_path", s_custom_cabs[i].image_path);
        cJSON_AddItemToArray(arr, it);
    }
    char *out = cJSON_Print(arr);
    cJSON_Delete(arr);
    if (!out) return;
    FILE *fp = fopen(custom_cabs_path(), "wb");
    if (fp) { fwrite(out, 1, strlen(out), fp); fclose(fp); }
    free(out);
}

/* Filters: pass NULL filters_n to skip filter spec */
static bool open_file_picker(const nfdu8filteritem_t *filters, int filters_n,
                             char *out_path, size_t out_sz) {
    nfdu8char_t *picked = NULL;
    nfdopendialogu8args_t args = {};
    args.filterList = filters;
    args.filterCount = (nfdfiltersize_t)filters_n;
    nfdresult_t r = NFD_OpenDialogU8_With(&picked, &args);
    if (r != NFD_OKAY || !picked) return false;
    strncpy(out_path, picked, out_sz - 1);
    out_path[out_sz - 1] = '\0';
    NFD_FreePathU8(picked);
    return true;
}

static bool open_folder_picker(char *out_path, size_t out_sz) {
    nfdu8char_t *picked = NULL;
    nfdresult_t r = NFD_PickFolderU8(&picked, NULL);
    if (r != NFD_OKAY || !picked) return false;
    strncpy(out_path, picked, out_sz - 1);
    out_path[out_sz - 1] = '\0';
    NFD_FreePathU8(picked);
    return true;
}

/* Recursive WAV scanner. Appends matching files to `out`, stopping at
 * `max_out` entries. Returns how many were written. */
static int scan_wavs_recursive(const char *dir, char (*out)[1024],
                               int *out_count, int max_out, int depth) {
    if (depth > 16) return 0;  /* guard against symlink loops */
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        const char *name = fd.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s\\%s", dir, name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_wavs_recursive(full, out, out_count, max_out, depth + 1);
        } else {
            size_t len = strlen(name);
            if (len > 4 &&
                (fx_strcasecmp(name + len - 4, ".wav") == 0)) {
                if (*out_count < max_out) {
                    strncpy(out[*out_count], full, 1023);
                    out[*out_count][1023] = '\0';
                    (*out_count)++;
                }
            }
        }
    } while (FindNextFileA(h, &fd) && *out_count < max_out);
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && *out_count < max_out) {
        const char *name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_wavs_recursive(full, out, out_count, max_out, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            size_t len = strlen(name);
            if (len > 4 && fx_strcasecmp(name + len - 4, ".wav") == 0) {
                if (*out_count < max_out) {
                    strncpy(out[*out_count], full, 1023);
                    out[*out_count][1023] = '\0';
                    (*out_count)++;
                }
            }
        }
    }
    closedir(d);
#endif
    return *out_count;
}

/* Bulk import: scan `folder` recursively for .wav files, add each to the
 * library (dedupe by path). Returns {added, skipped_existing, capacity_hit}. */
struct bulk_import_result_t {
    int added;
    int skipped_existing;
    int scanned;
    bool capacity_hit;
};

static bulk_import_result_t custom_cab_bulk_import(const char *folder) {
    bulk_import_result_t r = {};
    static char paths[FX_MAX_CUSTOM_CABS][1024];
    int found = 0;
    scan_wavs_recursive(folder, paths, &found, FX_MAX_CUSTOM_CABS, 0);
    r.scanned = found;
    for (int i = 0; i < found; i++) {
        if (custom_cab_find(paths[i]) >= 0) { r.skipped_existing++; continue; }
        if (s_custom_cab_count >= FX_MAX_CUSTOM_CABS) {
            r.capacity_hit = true;
            break;
        }
        if (custom_cab_add(paths[i]) >= 0) r.added++;
    }
    return r;
}

/* ── Theme selection ────────────────────────────────────────── */

#include "fx_theme.h"
static fx_theme_id_t s_theme = FX_THEME_GRIME_DARK;

static ImU32 theme_col32(const ImVec4 &v) {
    return ImGui::ColorConvertFloat4ToU32(v);
}
static ImVec4 scale_rgb(const ImVec4 &v, float s) {
    return ImVec4(v.x * s, v.y * s, v.z * s, v.w);
}

/* ── Looper panel state ─────────────────────────────────────── */

static bool s_looper_panel_open = false;

static ImVec4 looper_state_color(fx_loop_state_t s, bool muted) {
    /* Only the EMPTY state is neutral and worth theming — the others
     * (armed/recording/playing/overdubbing) carry semantic meaning so we
     * keep their universal colors for readability. */
    const fx_theme_t *th = fx_theme_get(s_theme);
    if (muted)                return ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
    switch (s) {
    case FX_LOOP_EMPTY:       return th->frame;
    case FX_LOOP_ARMED:       return ImVec4(0.80f, 0.60f, 0.15f, 1.0f);
    case FX_LOOP_RECORDING:   return ImVec4(0.85f, 0.15f, 0.15f, 1.0f);
    case FX_LOOP_PLAYING:     return ImVec4(0.20f, 0.70f, 0.30f, 1.0f);
    case FX_LOOP_OVERDUBBING: return ImVec4(0.85f, 0.55f, 0.20f, 1.0f);
    }
    return th->frame;
}

static const char *looper_state_label(fx_loop_state_t s) {
    switch (s) {
    case FX_LOOP_EMPTY:       return "empty";
    case FX_LOOP_ARMED:       return "armed";
    case FX_LOOP_RECORDING:   return "REC";
    case FX_LOOP_PLAYING:     return "play";
    case FX_LOOP_OVERDUBBING: return "DUB";
    }
    return "?";
}

/* Docked horizontal strip below the toolbar. Stays on top while open;
 * no close button (toolbar Looper toggle is the only way to hide it). */
static void looper_render_panel(fx_engine_t *engine,
                                float x, float y, float w, float h) {
    if (!s_looper_panel_open) return;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##looper_panel", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings);


    /* Background gradient + bottom separator — theme-driven, matches toolbar. */
    {
        const fx_theme_t *th = fx_theme_get(s_theme);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetWindowPos();
        ImVec2 p1 = ImVec2(p0.x + w, p0.y + h);
        ImU32 c_top = ImGui::ColorConvertFloat4ToU32(
            ImVec4(th->panel.x * 1.15f, th->panel.y * 1.15f, th->panel.z * 1.15f, 1.0f));
        ImU32 c_bot = ImGui::ColorConvertFloat4ToU32(th->bg);
        dl->AddRectFilledMultiColor(p0, p1, c_top, c_top, c_bot, c_bot);
        ImVec4 sep = th->border; sep.w = 0.8f;
        dl->AddLine(ImVec2(p0.x, p1.y - 1.0f),
                    ImVec2(p1.x, p1.y - 1.0f),
                    ImGui::ColorConvertFloat4ToU32(sep), 1.0f);
    }

    /* Panel title — theme accent for the heading. */
    {
        const fx_theme_t *th = fx_theme_get(s_theme);
        ImGui::PushStyleColor(ImGuiCol_Text, th->accent_glow);
        ImGui::SetCursorPos(ImVec2(10, 8));
        ImGui::Text("LOOPER");
        ImGui::PopStyleColor();
    }

    /* Keybinds help — ? button on the right edge of the strip. */
    ImGui::SetCursorPos(ImVec2(w - 56, 6));
    if (ImGui::SmallButton("?##looper_help"))
        ImGui::OpenPopup("looper_keybinds");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show looper keybinds");

    /* Close (X) — hides the looper panel. */
    ImGui::SetCursorPos(ImVec2(w - 28, 6));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
    if (ImGui::SmallButton("X##looper_close"))
        s_looper_panel_open = false;
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Close looper panel");
    if (ImGui::BeginPopup("looper_keybinds")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.78f, 0.6f, 1.0f));
        ImGui::Text("Looper keybinds");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::BulletText("1 – 9       tap slot (rec → play → overdub)");
        ImGui::BulletText("Shift + 1-9 mute / unmute slot");
        ImGui::BulletText("Alt + 1-9   clear slot");
        ImGui::BulletText("Space       tap the FOCUSED slot");
        ImGui::BulletText("R           arm next empty slot");
        ImGui::BulletText("Tab         cycle focused slot");
        ImGui::BulletText("Ctrl + Z    undo last overdub on focused");
        ImGui::Separator();
        ImGui::TextDisabled("Clicking a pad does the same as pressing 1-9.");
        ImGui::TextDisabled("Click again while RECORDING to stop and play.");
        ImGui::EndPopup();
    }

    const int focused = fx_looper_focused(engine);
    bool playing = fx_looper_master_is_playing(engine);

    /* Row 1: Play/Pause, master vol, sync, pre-chain tap */
    ImGui::SetCursorPos(ImVec2(10, 28));
    {
        const fx_theme_t *th = fx_theme_get(s_theme);
        ImVec4 btn = playing ? ImVec4(0.20f, 0.55f, 0.25f, 1.0f)
                             : scale_rgb(th->accent, 0.9f);
        ImGui::PushStyleColor(ImGuiCol_Button, btn);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              playing ? ImVec4(0.28f, 0.70f, 0.33f, 1.0f)
                                      : th->accent_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              playing ? ImVec4(0.16f, 0.48f, 0.22f, 1.0f)
                                      : th->accent_active);
    }
    if (ImGui::Button(playing ? "Pause" : "Play", ImVec2(72, 26))) {
        fx_looper_master_toggle(engine);
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    float level = fx_looper_get_master_level(engine);
    ImGui::SetNextItemWidth(130);
    if (ImGui::SliderFloat("##master", &level, 0.0f, 1.0f, "Vol %.2f")) {
        fx_looper_set_master_level(engine, level);
    }

    /* Row 2: options */
    ImGui::SetCursorPos(ImVec2(10, 62));
    bool sync = fx_looper_get_sync(engine);
    if (ImGui::Checkbox("Sync", &sync)) fx_looper_set_sync(engine, sync);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Quantize new slots to the first loop's length");
    ImGui::SameLine();
    bool pre = fx_looper_get_tap_pre_chain(engine);
    if (ImGui::Checkbox("Pre-chain", &pre))
        fx_looper_set_tap_pre_chain(engine, pre);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ON: record raw input. OFF: record post-chain (processed tone).");

    /* Row 3: focus + action buttons */
    ImGui::SetCursorPos(ImVec2(10, 92));
    ImGui::Text("Focus:%d", focused + 1);
    ImGui::SameLine();
    if (ImGui::SmallButton("R arm"))    fx_looper_arm_next(engine);
    ImGui::SameLine();
    if (ImGui::SmallButton("Tab"))      fx_looper_focus_next(engine);
    ImGui::SameLine();
    if (ImGui::SmallButton("Undo"))     fx_looper_slot_undo(engine, focused);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))    fx_looper_slot_clear(engine, focused);

    /* Row 4: export */
    ImGui::SetCursorPos(ImVec2(10, 116));
    if (ImGui::SmallButton("Export slot")) {
        char path[512];
        snprintf(path, sizeof(path), "loop_slot_%d.wav", focused + 1);
        if (fx_looper_export_slot_wav(engine, focused, path))
            FX_INFO("Looper: exported slot %d to %s", focused + 1, path);
        else FX_WARN("Looper: export failed (slot empty?)");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Export mix")) {
        if (fx_looper_export_mix_wav(engine, "loop_mix.wav"))
            FX_INFO("Looper: exported mix to loop_mix.wav");
        else FX_WARN("Looper: mix export failed");
    }

    /* 9 pads in a horizontal row on the right side of the strip. */
    const float pads_left = 260.0f;
    const float pad_gap   = 6.0f;
    const float pads_avail = w - pads_left - 12.0f;
    float pad_w = (pads_avail - pad_gap * 8) / 9.0f;
    if (pad_w < 56.0f) pad_w = 56.0f;
    if (pad_w > 110.0f) pad_w = 110.0f;
    const float pad_h = h - 20.0f;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();

    for (int slot = 0; slot < 9; slot++) {
        float px = pads_left + slot * (pad_w + pad_gap);
        ImGui::SetCursorPos(ImVec2(px, 10));

        fx_loop_state_t st = fx_looper_get_slot_state(engine, slot);
        bool muted = fx_looper_get_slot_muted(engine, slot);
        int len    = fx_looper_get_slot_length_frames(engine, slot);
        int pos    = fx_looper_get_slot_play_pos(engine, slot);
        int layers = fx_looper_get_slot_layers(engine, slot);

        ImVec4 c = looper_state_color(st, muted);
        ImGui::PushStyleColor(ImGuiCol_Button, c);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(c.x * 1.25f + 0.05f, c.y * 1.25f + 0.05f, c.z * 1.25f + 0.05f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            ImVec4(c.x * 0.8f, c.y * 0.8f, c.z * 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));

        /* `###pad_N` pins the widget ID so the button doesn't lose its active
         * item when the displayed length (%.1fs) ticks over during RECORDING.
         * Without it, ImGui's Button() drops the click if the label changes
         * between mouse-down and mouse-up frames. */
        char label[64];
        snprintf(label, sizeof(label), "%d\n%s%s\n%.1fs L%d###looper_pad_%d",
                 slot + 1, looper_state_label(st),
                 muted ? " m" : "",
                 len > 0 ? (float)len / 48000.0f : 0.0f, layers,
                 slot);

        ImGui::PushID(slot);
        if (ImGui::Button(label, ImVec2(pad_w, pad_h))) {
            FX_INFO("looper GUI: pad %d clicked (state=%d len=%d)",
                    slot + 1, (int)st, len);
            /* Click-to-tap also selects the slot so Space targets it next. */
            fx_looper_set_focus(engine, slot);
            fx_looper_slot_tap(engine, slot);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            FX_INFO("looper GUI: pad %d right-clicked (clear)", slot + 1);
            fx_looper_set_focus(engine, slot);
            fx_looper_slot_clear(engine, slot);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Left-click: tap (rec/play/dub)\nRight-click: clear slot");
        ImGui::PopID();
        ImGui::PopStyleColor(4);

        /* Position indicator + focus highlight drawn on top of the pad. */
        ImVec2 p_min = ImGui::GetItemRectMin();
        ImVec2 p_max = ImGui::GetItemRectMax();
        if (len > 0 && (st == FX_LOOP_PLAYING || st == FX_LOOP_OVERDUBBING)) {
            float t = (float)pos / (float)len;
            float bar_x = p_min.x + 3 + t * (p_max.x - p_min.x - 6);
            dl->AddLine(ImVec2(bar_x, p_max.y - 6),
                        ImVec2(bar_x, p_max.y - 2),
                        IM_COL32(255, 255, 255, 220), 2.0f);
        }
        if (slot == focused) {
            const fx_theme_t *th = fx_theme_get(s_theme);
            dl->AddRect(p_min, p_max,
                        ImGui::ColorConvertFloat4ToU32(th->accent_glow),
                        3.0f, 0, 2.0f);
        }
    }

    (void)win_pos;
    ImGui::End();
}

/* Handle looper keyboard shortcuts. Called whenever the panel is open
 * and no text input is focused. */
static void looper_handle_keys(fx_engine_t *engine) {
    ImGuiIO &io = ImGui::GetIO();
    bool ctrl  = io.KeyCtrl;
    bool shift = io.KeyShift;
    bool alt   = io.KeyAlt;

    /* 1..9 — mapped to slot 0..8. Shift=mute, Alt=clear. */
    static const ImGuiKey num_keys[9] = {
        ImGuiKey_1, ImGuiKey_2, ImGuiKey_3,
        ImGuiKey_4, ImGuiKey_5, ImGuiKey_6,
        ImGuiKey_7, ImGuiKey_8, ImGuiKey_9,
    };
    for (int i = 0; i < 9; i++) {
        if (ImGui::IsKeyPressed(num_keys[i], false)) {
            FX_INFO("looper GUI: key %d pressed (ctrl=%d shift=%d alt=%d)",
                    i + 1, ctrl, shift, alt);
            if (alt)        fx_looper_slot_clear(engine, i);
            else if (shift) fx_looper_slot_mute(engine, i);
            else {
                fx_looper_set_focus(engine, i);
                fx_looper_slot_tap(engine, i);
            }
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !ctrl && !shift && !alt) {
        FX_INFO("looper GUI: R pressed (arm next)");
        fx_looper_arm_next(engine);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !ctrl && !shift && !alt) {
        FX_INFO("looper GUI: Tab pressed (focused=%d)", fx_looper_focused(engine));
        fx_looper_focus_next(engine);
    }

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        FX_INFO("looper GUI: Ctrl+Z pressed (undo focused=%d)", fx_looper_focused(engine));
        fx_looper_slot_undo(engine, fx_looper_focused(engine));
    }
}

/* ── Surprise Me — random preset generator ──────────────────── */

static float randf(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

static void load_cab_for_type(fx_engine_t *engine, fx_chain_id chain,
                              fx_cab_type_t cab_type);
static void draw_procedural_cab(ImDrawList *dl, ImVec2 p0, ImVec2 p1, unsigned seed);

static void surprise_me_generate(fx_engine_t *engine, char *preset_name, int name_sz) {
    srand((unsigned)time(NULL));

    /* Clear existing chain */
    int pre_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_PRE);
    for (int i = pre_count - 1; i >= 0; i--) {
        fx_pedal_id pid = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_PRE, i);
        fx_chain_remove_pedal(engine, pid);
    }
    int post_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_POST);
    for (int i = post_count - 1; i >= 0; i--) {
        fx_pedal_id pid = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_POST, i);
        fx_chain_remove_pedal(engine, pid);
    }

    /* Random amp */
    fx_amp_type_t amp = (fx_amp_type_t)(rand() % FX_AMP_COUNT);
    fx_amp_set_model(engine, FX_CHAIN_DEFAULT, amp);
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN,     randf(0.2f, 0.7f));
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME,   randf(0.5f, 0.7f));
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_BASS,     randf(0.2f, 0.8f));
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MID,      randf(0.2f, 0.8f));
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_TREBLE,   randf(0.2f, 0.8f));
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_PRESENCE, randf(0.3f, 0.7f));

    /* Random cab */
    fx_cab_type_t cab = (fx_cab_type_t)(rand() % FX_CAB_TYPE_COUNT);
    load_cab_for_type(engine, FX_CHAIN_DEFAULT, cab);

    /* Noise gate defaults */
    fx_gate_set_threshold(engine, randf(-55.0f, -42.0f));
    fx_gate_set_attack(engine, 1.0f);
    fx_gate_set_release(engine, 40.0f);
    fx_gate_set_hold(engine, 12.0f);

    /* Pre-pedal types that make sense before amp */
    fx_pedal_type_t pre_types[] = {
        FX_PEDAL_JADE_DRIVE, FX_PEDAL_GOLD_DRIVE, FX_PEDAL_BLUES_GRIT,
        FX_PEDAL_RODENT, FX_PEDAL_ORANGE_DIST, FX_PEDAL_METAL_ZONE,
        FX_PEDAL_AMP_BOX, FX_PEDAL_MAMMOTH_FUZZ, FX_PEDAL_ROUND_FUZZ,
        FX_PEDAL_WRAITH_FUZZ, FX_PEDAL_CHAOS_FUZZ,
        FX_PEDAL_SQUEEZE_BOX, FX_PEDAL_GLASS_COMP, FX_PEDAL_PUNCH_COMP,
        FX_PEDAL_NOISE_GATE, FX_PEDAL_HOWL_WAH, FX_PEDAL_QUACK_FILTER,
        FX_PEDAL_WARM_TAPE, FX_PEDAL_GRIT_CRUSH, FX_PEDAL_OCTAVE_ENGINE
    };
    int n_pre_types = (int)(sizeof(pre_types) / sizeof(pre_types[0]));

    int n_pre = rand() % 4; /* 0-3 pre pedals (was 0-5, too many = gain stack) */
    int gain_pedals_added = 0;
    for (int i = 0; i < n_pre; i++) {
        fx_pedal_type_t pt = pre_types[rand() % n_pre_types];
        /* Limit gain-stacking pedals to max 1 drive/fuzz/distortion */
        bool is_gain = (pt == FX_PEDAL_JADE_DRIVE || pt == FX_PEDAL_GOLD_DRIVE ||
                        pt == FX_PEDAL_BLUES_GRIT || pt == FX_PEDAL_RODENT ||
                        pt == FX_PEDAL_ORANGE_DIST || pt == FX_PEDAL_METAL_ZONE ||
                        pt == FX_PEDAL_AMP_BOX || pt == FX_PEDAL_MAMMOTH_FUZZ ||
                        pt == FX_PEDAL_ROUND_FUZZ || pt == FX_PEDAL_WRAITH_FUZZ ||
                        pt == FX_PEDAL_CHAOS_FUZZ);
        if (is_gain && gain_pedals_added >= 1) {
            /* Skip this one — pick a non-gain pedal instead */
            pt = FX_PEDAL_NOISE_GATE;
        }
        if (is_gain) gain_pedals_added++;
        fx_pedal_id pid = fx_chain_add_pedal(engine, pt, FX_CHAIN_POS_PRE);
        int pc = fx_pedal_get_param_count(pt);
        for (int p = 0; p < pc; p++) {
            /* Musically useful random ranges per param type */
            const char *pname = fx_pedal_get_param_name(pt, p);
            float val;
            if (strstr(pname, "mix") || strstr(pname, "blend"))
                val = randf(0.15f, 0.5f);
            else if (strstr(pname, "drive") || strstr(pname, "gain") || strstr(pname, "fuzz") || strstr(pname, "distortion"))
                val = randf(0.3f, 0.8f);
            else if (strstr(pname, "level") || strstr(pname, "output") || strstr(pname, "volume"))
                val = randf(0.6f, 0.85f);
            else if (strstr(pname, "tone") || strstr(pname, "treble") || strstr(pname, "filter"))
                val = randf(0.3f, 0.7f);
            else
                val = randf(0.2f, 0.7f);
            fx_pedal_set_param(engine, pid, p, val);
        }
    }

    /* Post-pedal types */
    fx_pedal_type_t post_types[] = {
        FX_PEDAL_ECHO_DELAY, FX_PEDAL_CARBON_DELAY, FX_PEDAL_TAPE_MACHINE,
        FX_PEDAL_MEMORY_ECHO, FX_PEDAL_DRIP_VERB, FX_PEDAL_PLATE_VERB,
        FX_PEDAL_HALL_VERB, FX_PEDAL_SHIMMER_VERB, FX_PEDAL_CLOUD_VERB,
        FX_PEDAL_LIQUID_CHORUS, FX_PEDAL_PHASE_SWEEP, FX_PEDAL_JET_FLANGER,
        FX_PEDAL_PULSE_TREM, FX_PEDAL_DRIFT_VIBRATO, FX_PEDAL_WARM_TAPE,
        FX_PEDAL_GRAIN_CLOUD, FX_PEDAL_INFINITE_HOLD
    };
    int n_post_types = (int)(sizeof(post_types) / sizeof(post_types[0]));

    int n_post = rand() % 3; /* 0-2 post pedals (was 0-4 — too many) */
    fx_pedal_type_t used_post[8] = {(fx_pedal_type_t)-1};
    int n_used_post = 0;
    for (int i = 0; i < n_post; i++) {
        fx_pedal_type_t pt;
        int attempts = 0;
        do {
            pt = post_types[rand() % n_post_types];
            /* Don't add duplicate types */
            bool dup = false;
            for (int u = 0; u < n_used_post; u++) { if (used_post[u] == pt) { dup = true; break; } }
            if (!dup) break;
        } while (++attempts < 10);
        used_post[n_used_post++] = pt;
        fx_pedal_id pid = fx_chain_add_pedal(engine, pt, FX_CHAIN_POS_POST);
        int pc = fx_pedal_get_param_count(pt);
        for (int p = 0; p < pc; p++) {
            const char *pname = fx_pedal_get_param_name(pt, p);
            float val;
            if (strstr(pname, "mix"))
                val = randf(0.1f, 0.4f);
            else if (strstr(pname, "time") || strstr(pname, "decay"))
                val = randf(0.2f, 0.6f);
            else if (strstr(pname, "feedback"))
                val = randf(0.15f, 0.45f);
            else if (strstr(pname, "rate") || strstr(pname, "speed"))
                val = randf(0.15f, 0.5f);
            else if (strstr(pname, "depth"))
                val = randf(0.2f, 0.5f);
            else
                val = randf(0.2f, 0.6f);
            fx_pedal_set_param(engine, pid, p, val);
        }
    }

    /* Random rack effect (0-2) */
    fx_studio_type_t rack_types[] = {
        FX_STUDIO_IRON_SQUEEZE, FX_STUDIO_GLASS_EQ, FX_STUDIO_REEL_WARMTH,
        FX_STUDIO_VELVET_PRESS, FX_STUDIO_GLUE_BUS, FX_STUDIO_VALVE_COLOR
    };
    int n_rack_types = (int)(sizeof(rack_types) / sizeof(rack_types[0]));
    int n_rack = rand() % 3;
    for (int i = 0; i < n_rack; i++) {
        fx_studio_type_t rt = rack_types[rand() % n_rack_types];
        fx_studio_id sid = fx_studio_add(engine, rt);
        int pc = fx_studio_get_param_count(rt);
        for (int p = 0; p < pc; p++)
            fx_studio_set_param(engine, sid, p, randf(0.2f, 0.6f));
    }

    /* Safety limiter — always at end of rack chain to prevent hearing damage */
    {
        fx_studio_id lid = fx_studio_add(engine, FX_STUDIO_BRICK_WALL);
        fx_studio_set_param(engine, lid, 0, 0.4f);  /* threshold ~-7dB (conservative) */
        fx_studio_set_param(engine, lid, 1, 0.85f);  /* ceiling ~-0.15dB */
        fx_studio_set_param(engine, lid, 2, 0.5f);  /* medium release */
    }

    /* Fun name */
    const char *adjectives[] = {
        "Cosmic", "Vintage", "Electric", "Fuzzy", "Crystal", "Molten",
        "Midnight", "Neon", "Thunder", "Velvet", "Atomic", "Rusty",
        "Golden", "Phantom", "Wild", "Savage", "Dreamy", "Gritty"
    };
    const char *nouns[] = {
        "Machine", "Vibe", "Storm", "Tone", "Wave", "Howl",
        "Blaze", "Echo", "Surge", "Whisper", "Roar", "Drift",
        "Pulse", "Spark", "Crush", "Bloom", "Growl", "Shimmer"
    };
    int n_adj = (int)(sizeof(adjectives) / sizeof(adjectives[0]));
    int n_noun = (int)(sizeof(nouns) / sizeof(nouns[0]));
    snprintf(preset_name, name_sz, "%s %s", adjectives[rand() % n_adj], nouns[rand() % n_noun]);

    FX_INFO("Surprise Me: generated '%s'", preset_name);
}

/* HSV to RGB for rainbow button effect */
static ImVec4 hsv_to_rgb(float h, float s, float v) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return ImVec4(r, g, b, 1.0f);
}

/* ── Amp param tooltips (TASK-201) ───────────────────────────── */

static const char *s_amp_param_tooltips[] = {
    "Gain \xe2\x80\x94 Preamp drive level. Higher = more distortion",
    "Volume \xe2\x80\x94 Overall output volume",
    "Bass \xe2\x80\x94 Low frequency EQ",
    "Mid \xe2\x80\x94 Midrange frequency EQ",
    "Treble \xe2\x80\x94 High frequency EQ",
    "Presence \xe2\x80\x94 Upper-mid sparkle/bite",
    "Sag \xe2\x80\x94 Power supply droop. Higher = spongier feel",
    "Master \xe2\x80\x94 Power amp volume",
    "Bright \xe2\x80\x94 Treble boost switch",
    "Cut \xe2\x80\x94 High frequency cut"
};
static const int s_amp_param_tooltip_count =
    (int)(sizeof(s_amp_param_tooltips) / sizeof(s_amp_param_tooltips[0]));

/* ── Pedal one-line descriptions for gallery tooltips ─────── */

struct PedalTooltip { fx_pedal_type_t type; const char *desc; };
static const PedalTooltip s_pedal_tooltips[] = {
    { FX_PEDAL_JADE_DRIVE,    "Smooth overdrive with mid-hump character (TS-style)" },
    { FX_PEDAL_GOLD_DRIVE,    "Transparent overdrive - clean blend with tanh clipping" },
    { FX_PEDAL_BLUES_GRIT,    "Warm gritty blues overdrive with presence boost" },
    { FX_PEDAL_RODENT,        "Aggressive distortion with backwards tone filter (RAT-style)" },
    { FX_PEDAL_ORANGE_DIST,   "High-gain distortion with scooped mids" },
    { FX_PEDAL_METAL_ZONE,    "Modern metal distortion with graphic EQ contour" },
    { FX_PEDAL_AMP_BOX,       "Amp-in-a-box - simulates a pushed tube amp preamp" },
    { FX_PEDAL_MAMMOTH_FUZZ,  "Big Muff-style fuzz with 4-stage clipping and scooped mids" },
    { FX_PEDAL_ROUND_FUZZ,    "Germanium fuzz - smooth and woolly, cleans up with guitar volume" },
    { FX_PEDAL_WRAITH_FUZZ,   "Velcro-style octave fuzz with harmonic overtones" },
    { FX_PEDAL_CHAOS_FUZZ,    "Sputtery gated fuzz with oscillation control" },
    { FX_PEDAL_ECHO_DELAY,    "Digital echo delay - Time, Feedback, Mix" },
    { FX_PEDAL_CARBON_DELAY,  "BBD analog delay - repeats darken progressively" },
    { FX_PEDAL_TAPE_MACHINE,  "Tape echo with wow/flutter and tape degradation" },
    { FX_PEDAL_MEMORY_ECHO,   "Warm analog-voiced delay with modulation" },
    { FX_PEDAL_DRIP_VERB,     "Spring reverb with characteristic drip transient" },
    { FX_PEDAL_HALL_VERB,     "Lush hall reverb (Freeverb algorithm)" },
    { FX_PEDAL_PLATE_VERB,    "Dense plate reverb with diffusion network" },
    { FX_PEDAL_SHIMMER_VERB,  "Reverb with octave-up pitch in feedback for pads" },
    { FX_PEDAL_CLOUD_VERB,    "Ambient reverb with infinite decay / freeze mode" },
    { FX_PEDAL_LIQUID_CHORUS, "Lush BBD-style chorus with stereo spread" },
    { FX_PEDAL_PHASE_SWEEP,   "Analog-voiced phaser with sweeping notch filters" },
    { FX_PEDAL_JET_FLANGER,   "Through-zero flanger with classic jet-plane sweep" },
    { FX_PEDAL_PULSE_TREM,    "Tremolo - amplitude modulation at LFO rate" },
    { FX_PEDAL_DRIFT_VIBRATO, "True pitch vibrato (not amplitude) via delay modulation" },
    { FX_PEDAL_SQUEEZE_BOX,   "Compressor with peak-detecting envelope follower (4:1 ratio)" },
    { FX_PEDAL_GLASS_COMP,    "Optical-style transparent compressor" },
    { FX_PEDAL_PUNCH_COMP,    "FET-style punchy compressor for attack-heavy sounds" },
    { FX_PEDAL_NOISE_GATE,    "Noise gate - silences signal below threshold" },
    { FX_PEDAL_HOWL_WAH,      "Expression wah - bandpass sweep controlled by expression (0-1)" },
    { FX_PEDAL_QUACK_FILTER,  "Auto-wah - envelope follower drives bandpass cutoff" },
    { FX_PEDAL_TONE_SCULPTOR, "7-band graphic EQ for precise frequency shaping" },
    { FX_PEDAL_PRECISION_EQ,  "Parametric EQ with sweepable frequency bands" },
    { FX_PEDAL_OCTAVE_ENGINE, "Polyphonic octave - sub-octave and octave-up tracking" },
    { FX_PEDAL_PITCH_WARP,    "Pitch shifter with intelligent note tracking" },
    { FX_PEDAL_GRIT_CRUSH,    "Bitcrusher - reduces bit depth and sample rate for lo-fi tones" },
    { FX_PEDAL_RING_TONE,     "Ring modulator - carrier frequency creates metallic tones" },
    { FX_PEDAL_WARM_TAPE,     "Tape saturation - adds harmonic warmth and soft limiting" },
    { FX_PEDAL_INFINITE_HOLD, "Freeze pedal - captures audio frame and loops as infinite drone" },
    { FX_PEDAL_GRAIN_CLOUD,   "Granular delay - chops audio into grains for textural sounds" },
    { FX_PEDAL_LOOP_STATION,  "Looper - record, overdub, play and undo loops (up to 5 min)" },
};
static const int s_pedal_tooltip_count =
    (int)(sizeof(s_pedal_tooltips) / sizeof(s_pedal_tooltips[0]));

static const char *get_pedal_tooltip(fx_pedal_type_t type) {
    for (int i = 0; i < s_pedal_tooltip_count; i++) {
        if (s_pedal_tooltips[i].type == type) return s_pedal_tooltips[i].desc;
    }
    return nullptr;
}

/* ── Theme application ─────────────────────────────────────────── */

static void apply_theme(fx_theme_id_t id) {
    ImGuiStyle &style = ImGui::GetStyle();
    fx_theme_apply(style, id);

    /* Shape / spacing settings are theme-agnostic. */
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;
    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(6, 4);
    style.ItemSpacing       = ImVec2(8, 6);

    s_theme = id;
}

static void setup_theme(void) {
    apply_theme(s_theme);
}

/* Toolbar / status-bar buttons use neutral frame colors (not the bright accent)
 * so they read like "chrome" rather than primary CTAs. */
static void push_toolbar_button_colors(void) {
    const fx_theme_t *th = fx_theme_get(s_theme);
    ImGui::PushStyleColor(ImGuiCol_Button,        th->frame);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, th->frame_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  th->frame_active);
    ImGui::PushStyleColor(ImGuiCol_Text,          th->text);
}
static void pop_toolbar_button_colors(void) {
    ImGui::PopStyleColor(4);
}

/* ── Borderless window support (Win32) ─────────────────────────── */

#define RESIZE_BORDER 8
#define FX_TOOLBAR_HIT_H 64   /* must match TOOLBAR_H in main() */

/* Published each frame by the toolbar pass: true if the mouse currently sits
 * over any interactive toolbar widget. The SDL hit-test reads this to decide
 * whether a toolbar click should become a window drag (empty space) or pass
 * through to ImGui (button/dropdown/etc). Same-thread access — SDL event
 * dispatch and GUI submit both run on main. One frame of staleness is fine:
 * the click position equals the current hover position. */
static bool g_toolbar_pointer_on_widget = false;

/* SDL hit-test: lets the OS/WM handle window drag and edge resize for our
 * borderless window. If we return RESIZE_* or DRAGGABLE, SDL handles the
 * click as a system operation and ImGui never sees it. */
static SDL_HitTestResult SDLCALL fx_window_hit_test(SDL_Window *win,
                                                    const SDL_Point *pt,
                                                    void *data) {
    (void)data;
    int w = 0, h = 0;
    SDL_GetWindowSize(win, &w, &h);
    bool maximized = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;

    if (!maximized) {
        const int edge = RESIZE_BORDER;
        bool left   = pt->x <  edge;
        bool right_ = pt->x >= w - edge;
        bool top    = pt->y <  edge;
        bool bot    = pt->y >= h - edge;
        if (top && left)    return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right_)  return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bot && left)    return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bot && right_)  return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (top)            return SDL_HITTEST_RESIZE_TOP;
        if (bot)            return SDL_HITTEST_RESIZE_BOTTOM;
        if (left)           return SDL_HITTEST_RESIZE_LEFT;
        if (right_)         return SDL_HITTEST_RESIZE_RIGHT;
    }
    /* Whole toolbar is draggable unless the pointer is over a widget. */
    if (pt->y < FX_TOOLBAR_HIT_H && !g_toolbar_pointer_on_widget)
        return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

#ifdef _WIN32
static WNDPROC g_orig_wndproc = NULL;

static LRESULT CALLBACK borderless_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCALCSIZE && wp == TRUE) {
        if (IsZoomed(hwnd)) {
            NCCALCSIZE_PARAMS *params = (NCCALCSIZE_PARAMS *)lp;
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi; mi.cbSize = sizeof(mi);
            if (GetMonitorInfo(mon, &mi)) params->rgrc[0] = mi.rcWork;
        }
        return 0;
    }
    if (msg == WM_NCHITTEST) {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            RECT rc; GetClientRect(hwnd, &rc);
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);
            int w = rc.right, h = rc.bottom;
            bool top = pt.y < RESIZE_BORDER, bottom = pt.y >= h - RESIZE_BORDER;
            bool left = pt.x < RESIZE_BORDER, right_ = pt.x >= w - RESIZE_BORDER;
            if (top && left)      return HTTOPLEFT;
            if (top && right_)    return HTTOPRIGHT;
            if (bottom && left)   return HTBOTTOMLEFT;
            if (bottom && right_) return HTBOTTOMRIGHT;
            if (top)              return HTTOP;
            if (bottom)           return HTBOTTOM;
            if (left)             return HTLEFT;
            if (right_)           return HTRIGHT;
        } else return hit;
    }
    if (msg == WM_GETMINMAXINFO) {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi; mi.cbSize = sizeof(mi);
        if (GetMonitorInfo(mon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mmi->ptMaxPosition.y = mi.rcWork.top  - mi.rcMonitor.top;
            mmi->ptMaxSize.x     = mi.rcWork.right  - mi.rcWork.left;
            mmi->ptMaxSize.y     = mi.rcWork.bottom - mi.rcWork.top;
        }
        return 0;
    }
    return CallWindowProcW(g_orig_wndproc, hwnd, msg, wp, lp);
}

static void install_borderless_wndproc(SDL_Window *window) {
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo)) {
        HWND hwnd = wmInfo.info.win.window;
        g_orig_wndproc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                                      (LONG_PTR)borderless_wndproc);
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style |= WS_THICKFRAME | WS_CAPTION | WS_SYSMENU |
                 WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }
}
#endif /* _WIN32 */

/* ── Signal chain node types ─────────────────────────────────────── */

enum NodeKind {
    NODE_INPUT = 0,
    NODE_PEDAL_PRE,
    NODE_SPLIT,       /* Y-split diamond node (signal splits here) */
    NODE_AMP,
    NODE_CAB,
    NODE_MERGE,       /* merge/mix diamond node (paths join here) */
    NODE_PEDAL_POST,
    NODE_STUDIO,      /* rack effect processor (post-amp) */
    NODE_OUTPUT
};

/* Signal chain node descriptor */
struct ChainNode {
    NodeKind    kind;
    int         slot;       /* index into pre/post pedal arrays, or -1 */
    fx_pedal_id pedal_id;   /* valid only for PEDAL nodes */
    int         chain_id;   /* which parallel chain (0=top, 1=bottom) for AMP/CAB in split mode */
};

/* Node colors by kind */
static ImU32 node_color(NodeKind kind, bool bypassed) {
    if (bypassed) return IM_COL32(60, 55, 50, 255);
    switch (kind) {
        case NODE_INPUT:      return IM_COL32(50, 120, 80, 255);
        case NODE_PEDAL_PRE:  return IM_COL32(60, 100, 160, 255);
        case NODE_SPLIT:      return IM_COL32(200, 160, 30, 255);
        case NODE_AMP:        return IM_COL32(180, 90, 30, 255);
        case NODE_CAB:        return IM_COL32(140, 80, 50, 255);
        case NODE_MERGE:      return IM_COL32(200, 160, 30, 255);
        case NODE_PEDAL_POST: return IM_COL32(100, 60, 160, 255);
        case NODE_STUDIO:     return IM_COL32(60, 100, 140, 255);
        case NODE_OUTPUT:     return IM_COL32(50, 120, 80, 255);
        default:              return IM_COL32(80, 80, 80, 255);
    }
}

static const char *node_label(NodeKind kind, fx_engine_t *engine, fx_pedal_id pid, int chain_id) {
    switch (kind) {
        case NODE_INPUT:      return "INPUT";
        case NODE_SPLIT:      return "SPLIT";
        case NODE_AMP:        return fx_amp_get_type_name(fx_amp_get_model(engine, (fx_chain_id)chain_id));
        case NODE_CAB:        return (chain_id == 0) ? "CAB A" : "CAB B";
        case NODE_MERGE:      return "MIX";
        case NODE_OUTPUT:     return "OUTPUT";
        case NODE_STUDIO: {
            fx_studio_type_t st = fx_studio_get_type(engine, pid);
            if (st < FX_STUDIO_COUNT) return fx_studio_get_type_name(st);
            return "???";
        }
        case NODE_PEDAL_PRE:
        case NODE_PEDAL_POST: {
            fx_pedal_type_t pt = fx_pedal_get_type(engine, pid);
            if (pt < FX_PEDAL_TYPE_COUNT) return fx_pedal_get_type_name(pt);
            return "???";
        }
        default: return "???";
    }
}

/* Cab type names (engine doesn't expose these) */
static const char *s_cab_type_names[] = {
    "1x12 Open", "2x12 Closed", "4x12 Straight", "4x12 Slant"
};

/* Filename bases for bundled public-domain IRs in resources/ir/bundled/
 * (loaded with a .wav suffix). Loaded in preference to synthetic; synthetic
 * acts as a fallback if the WAV file is missing from the install. */
static const char *s_cab_ir_filenames[] = {
    "1x12_open",      /* FX_CAB_1X12_OPEN */
    "2x12_closed",    /* FX_CAB_2X12_CLOSED */
    "4x12_straight",  /* FX_CAB_4X12_STRAIGHT */
    "4x12_slant",     /* FX_CAB_4X12_SLANT */
};

/* Load the cab IR for a given type: prefer the bundled WAV, fall back to
 * the parametric synthesizer if the WAV isn't present. */
static void load_cab_for_type(fx_engine_t *engine, fx_chain_id chain,
                              fx_cab_type_t cab_type) {
    bool loaded = false;
    if (cab_type >= 0 && cab_type < FX_CAB_TYPE_COUNT) {
        char ir_path[256];
        snprintf(ir_path, sizeof(ir_path),
                 "resources/ir/bundled/%s.wav",
                 s_cab_ir_filenames[cab_type]);
        loaded = fx_cab_load_ir(engine, chain, ir_path);
    }
    if (!loaded) {
        fx_cab_params_t params = {};
        params.cab_type   = cab_type;
        params.mic_pos    = FX_MIC_ON_AXIS;
        params.speaker_fs = 80.0f;
        params.brightness = 0.5f;
        params.resonance  = 0.5f;
        fx_cab_generate_ir(engine, chain, &params);
    }
    /* Stock cabs aren't "custom" — drop any metadata the WAV load stamped in */
    fx_cab_clear_custom_ir_path(engine, chain);
}

/* ── Procedural cab rendering ─────────────────────────────────
 * Draws one of 10 variant cabinet designs into rect [p0,p1] using
 * ImGui's draw list. `seed` (usually an FNV-1a hash of the IR filename)
 * picks the variant deterministically, so the same custom IR always
 * gets the same art.  No assets required. */

static unsigned fnv1a(const char *s) {
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)(*s++); h *= 16777619u; }
    return h;
}

struct cab_design_t {
    ImU32 body;       /* tolex fill */
    ImU32 body_edge;  /* tolex border / scuff */
    ImU32 grill;      /* grill cloth */
    ImU32 grill_line; /* grill weave */
    ImU32 speaker;    /* speaker cone */
    ImU32 dust_cap;   /* speaker dust cap */
    ImU32 corner;     /* metal corner protector */
    int   speakers;   /* 1, 2, or 4 */
    bool  slant;      /* slanted top edge (4x12 slant) */
    bool  chrome;     /* chrome corner caps */
    const char *badge;
};

static const cab_design_t s_cab_designs[10] = {
    /* 0: 4x12 straight, black tolex, tan grill */
    { IM_COL32(28,24,22,255),  IM_COL32(55,48,42,255), IM_COL32(180,150,110,255), IM_COL32(140,115,80,255),  IM_COL32(30,28,26,255), IM_COL32(15,14,13,255), IM_COL32(120,115,105,255), 4, false, true,  "0xFX" },
    /* 1: 4x12 slant, black tolex, silver grill */
    { IM_COL32(26,22,20,255),  IM_COL32(50,45,40,255), IM_COL32(185,185,190,255), IM_COL32(130,130,135,255), IM_COL32(40,35,32,255), IM_COL32(20,18,16,255), IM_COL32(110,110,110,255), 4, true,  true,  "FX" },
    /* 2: 2x12 combo-ish, tweed */
    { IM_COL32(168,135,85,255),IM_COL32(120,95,60,255), IM_COL32(95,75,55,255),    IM_COL32(60,45,32,255),    IM_COL32(45,35,28,255), IM_COL32(25,18,14,255), IM_COL32(90,75,55,255),    2, false, false, "0xFX" },
    /* 3: 1x12 combo, cream bronco */
    { IM_COL32(215,205,185,255),IM_COL32(170,160,140,255),IM_COL32(105,85,55,255),  IM_COL32(70,55,35,255),    IM_COL32(50,40,30,255), IM_COL32(25,20,15,255), IM_COL32(140,130,110,255), 1, false, false, "FX" },
    /* 4: 4x12 straight, brown/oxblood vinyl */
    { IM_COL32(78,38,32,255),  IM_COL32(115,55,48,255), IM_COL32(180,155,95,255),  IM_COL32(130,110,70,255),  IM_COL32(35,28,24,255), IM_COL32(18,14,12,255), IM_COL32(130,95,55,255),   4, false, true,  "0xFX" },
    /* 5: 1x12 blue tolex */
    { IM_COL32(30,52,88,255),  IM_COL32(60,85,125,255), IM_COL32(195,195,195,255), IM_COL32(140,145,150,255), IM_COL32(38,34,30,255), IM_COL32(18,16,14,255), IM_COL32(140,145,155,255), 1, false, true,  "FX" },
    /* 6: 2x12 oversized, forest green tolex */
    { IM_COL32(28,58,38,255),  IM_COL32(55,95,65,255),  IM_COL32(92,78,55,255),    IM_COL32(60,50,32,255),    IM_COL32(35,30,26,255), IM_COL32(18,15,13,255), IM_COL32(120,110,95,255),  2, false, false, "0xFX" },
    /* 7: 4x12 straight, white tolex, black grill */
    { IM_COL32(210,210,205,255),IM_COL32(165,165,160,255),IM_COL32(22,20,18,255),   IM_COL32(55,52,50,255),    IM_COL32(25,22,20,255), IM_COL32(12,11,10,255), IM_COL32(180,180,185,255), 4, false, true,  "FX" },
    /* 8: 4x12 slant, red tolex, gold grill */
    { IM_COL32(130,28,32,255), IM_COL32(175,55,58,255), IM_COL32(200,165,70,255),  IM_COL32(150,120,45,255),  IM_COL32(40,32,28,255), IM_COL32(20,15,13,255), IM_COL32(210,180,90,255),  4, true,  true,  "0xFX" },
    /* 9: 1x12 open-back, purple tolex */
    { IM_COL32(58,32,78,255),  IM_COL32(95,55,120,255), IM_COL32(180,175,165,255), IM_COL32(120,115,105,255), IM_COL32(38,32,30,255), IM_COL32(18,15,14,255), IM_COL32(150,140,160,255), 1, false, false, "FX" },
};

static void draw_procedural_cab(ImDrawList *dl, ImVec2 p0, ImVec2 p1, unsigned seed) {
    const cab_design_t &d = s_cab_designs[seed % 10];

    float x0 = p0.x, y0 = p0.y, x1 = p1.x, y1 = p1.y;
    float w = x1 - x0, h = y1 - y0;

    /* Slant shifts the top edge down on the right side */
    float top_right_y = y0 + (d.slant ? h * 0.10f : 0.0f);

    /* Cab body — tolex */
    ImVec2 poly[4] = {
        ImVec2(x0, y0),
        ImVec2(x1, top_right_y),
        ImVec2(x1, y1),
        ImVec2(x0, y1),
    };
    dl->AddConvexPolyFilled(poly, 4, d.body);
    dl->AddPolyline(poly, 4, d.body_edge, ImDrawFlags_Closed, 2.0f);

    /* Top/bottom tolex piping lines for depth */
    dl->AddLine(ImVec2(x0 + 3, y0 + 3), ImVec2(x1 - 3, top_right_y + 3),
                d.body_edge, 1.0f);
    dl->AddLine(ImVec2(x0 + 3, y1 - 3), ImVec2(x1 - 3, y1 - 3), d.body_edge, 1.0f);

    /* Grill cloth area — inset from body */
    float pad = w * 0.06f;
    float grill_top    = y0 + h * 0.12f;
    float grill_bot    = y1 - h * 0.12f;
    float grill_left   = x0 + pad;
    float grill_right  = x1 - pad;
    if (d.slant) grill_top += (top_right_y - y0) * 0.6f;
    dl->AddRectFilled(ImVec2(grill_left, grill_top),
                      ImVec2(grill_right, grill_bot), d.grill, 3.0f);
    dl->AddRect      (ImVec2(grill_left, grill_top),
                      ImVec2(grill_right, grill_bot), d.body_edge, 3.0f, 0, 1.5f);

    /* Grill weave — diagonal crosshatch */
    float gw = grill_right - grill_left;
    float gh = grill_bot - grill_top;
    float step = 6.0f;
    for (float k = -gh; k < gw; k += step) {
        float ax = grill_left + k,      ay = grill_top;
        float bx = grill_left + k + gh, by = grill_bot;
        if (ax < grill_left)  { ay = grill_top + (grill_left - ax);  ax = grill_left;  }
        if (bx > grill_right) { by = grill_bot - (bx - grill_right); bx = grill_right; }
        if (ay > grill_bot || by < grill_top) continue;
        dl->AddLine(ImVec2(ax, ay), ImVec2(bx, by), d.grill_line, 0.6f);
    }
    for (float k = 0; k < gw + gh; k += step) {
        float ax = grill_left + k,      ay = grill_top;
        float bx = grill_left + k - gh, by = grill_bot;
        if (ax > grill_right) { ay = grill_top + (ax - grill_right); ax = grill_right; }
        if (bx < grill_left)  { by = grill_bot - (grill_left - bx);  bx = grill_left;  }
        if (ay > grill_bot || by < grill_top) continue;
        dl->AddLine(ImVec2(ax, ay), ImVec2(bx, by), d.grill_line, 0.6f);
    }

    /* Speaker cones showing through grill (subtle) */
    int rows = (d.speakers == 4) ? 2 : 1;
    int cols = (d.speakers == 1) ? 1 : 2;
    float cell_w = gw / cols, cell_h = gh / rows;
    float radius = (cell_w < cell_h ? cell_w : cell_h) * 0.36f;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            float cx = grill_left + (c + 0.5f) * cell_w;
            float cy = grill_top  + (r + 0.5f) * cell_h;
            dl->AddCircleFilled(ImVec2(cx, cy), radius,
                                IM_COL32(0, 0, 0, 60), 24);
            dl->AddCircleFilled(ImVec2(cx, cy), radius * 0.55f, d.speaker, 24);
            dl->AddCircleFilled(ImVec2(cx, cy), radius * 0.22f, d.dust_cap, 16);
        }
    }

    /* Chrome corner caps (optional) */
    if (d.chrome) {
        float cap = w * 0.05f;
        dl->AddRectFilled(ImVec2(x0,       y0),       ImVec2(x0 + cap, y0 + cap), d.corner, 2.0f);
        dl->AddRectFilled(ImVec2(x1 - cap, top_right_y), ImVec2(x1, top_right_y + cap), d.corner, 2.0f);
        dl->AddRectFilled(ImVec2(x0,       y1 - cap), ImVec2(x0 + cap, y1),        d.corner, 2.0f);
        dl->AddRectFilled(ImVec2(x1 - cap, y1 - cap), ImVec2(x1,       y1),        d.corner, 2.0f);
    }

    /* Badge plate */
    if (d.badge && *d.badge) {
        ImVec2 ts = ImGui::CalcTextSize(d.badge);
        float bx = x0 + (w - ts.x) * 0.5f;
        float by = grill_bot + (y1 - grill_bot - ts.y) * 0.5f;
        dl->AddText(ImVec2(bx, by), IM_COL32(230, 200, 130, 220), d.badge);
    }
}

/* ── Pedal gallery: category-organized pedal browser ──────────── */

struct PedalEntry {
    fx_pedal_type_t type;
    const char     *name;
};

struct PedalCategory {
    const char  *label;
    PedalEntry   pedals[8];
    int          count;
};

static const PedalCategory s_pedal_categories[] = {
    { "OVERDRIVE", {
        { FX_PEDAL_JADE_DRIVE,  "Jade Drive"  },
        { FX_PEDAL_GOLD_DRIVE,  "Gold Drive"  },
        { FX_PEDAL_BLUES_GRIT,  "Blues Grit"  },
    }, 3 },
    { "DISTORTION", {
        { FX_PEDAL_RODENT,      "Rodent"      },
        { FX_PEDAL_ORANGE_DIST, "Orange Dist" },
        { FX_PEDAL_METAL_ZONE,  "Metal Zone"  },
        { FX_PEDAL_AMP_BOX,     "Amp Box"     },
    }, 4 },
    { "FUZZ", {
        { FX_PEDAL_MAMMOTH_FUZZ, "Mammoth Fuzz" },
        { FX_PEDAL_ROUND_FUZZ,   "Round Fuzz"   },
        { FX_PEDAL_WRAITH_FUZZ,  "Wraith Fuzz"  },
        { FX_PEDAL_CHAOS_FUZZ,   "Chaos Fuzz"   },
    }, 4 },
    { "DELAY", {
        { FX_PEDAL_ECHO_DELAY,   "Echo Delay"   },
        { FX_PEDAL_CARBON_DELAY, "Carbon Delay" },
        { FX_PEDAL_TAPE_MACHINE, "Tape Machine" },
        { FX_PEDAL_MEMORY_ECHO,  "Memory Echo"  },
    }, 4 },
    { "REVERB", {
        { FX_PEDAL_DRIP_VERB,    "Drip Verb"    },
        { FX_PEDAL_HALL_VERB,    "Hall Verb"    },
        { FX_PEDAL_PLATE_VERB,   "Plate Verb"   },
        { FX_PEDAL_SHIMMER_VERB, "Shimmer Verb" },
        { FX_PEDAL_CLOUD_VERB,   "Cloud Verb"   },
    }, 5 },
    { "MODULATION", {
        { FX_PEDAL_LIQUID_CHORUS, "Liquid Chorus" },
        { FX_PEDAL_PHASE_SWEEP,   "Phase Sweep"   },
        { FX_PEDAL_JET_FLANGER,   "Jet Flanger"   },
        { FX_PEDAL_PULSE_TREM,    "Pulse Trem"    },
        { FX_PEDAL_DRIFT_VIBRATO, "Drift Vibrato" },
    }, 5 },
    { "DYNAMICS", {
        { FX_PEDAL_SQUEEZE_BOX, "Squeeze Box" },
        { FX_PEDAL_GLASS_COMP,  "Glass Comp"  },
        { FX_PEDAL_PUNCH_COMP,  "Punch Comp"  },
        { FX_PEDAL_NOISE_GATE,  "Noise Gate"  },
    }, 4 },
    { "FILTER/EQ", {
        { FX_PEDAL_HOWL_WAH,      "Howl Wah"      },
        { FX_PEDAL_QUACK_FILTER,  "Quack Filter"  },
        { FX_PEDAL_TONE_SCULPTOR, "Tone Sculptor" },
        { FX_PEDAL_PRECISION_EQ,  "Precision EQ"  },
    }, 4 },
    { "PITCH", {
        { FX_PEDAL_OCTAVE_ENGINE, "Octave Engine" },
        { FX_PEDAL_PITCH_WARP,    "Pitch Warp"    },
    }, 2 },
    { "UTILITY", {
        { FX_PEDAL_GRIT_CRUSH, "Grit Crush" },
        { FX_PEDAL_RING_TONE,  "Ring Tone"  },
        { FX_PEDAL_WARM_TAPE,  "Warm Tape"  },
    }, 3 },
    { "EXPERIMENTAL", {
        { FX_PEDAL_INFINITE_HOLD, "Infinite Hold" },
        { FX_PEDAL_GRAIN_CLOUD,   "Grain Cloud"   },
        { FX_PEDAL_LOOP_STATION,  "Loop Station"  },
    }, 3 },
};
static const int s_pedal_category_count = 11;

/* Shared pedal menu for add-pedal popups */
static const struct { fx_pedal_type_t type; const char *name; } s_pedal_menu[] = {
    { FX_PEDAL_JADE_DRIVE,    "Jade Drive (OD)" },
    { FX_PEDAL_GOLD_DRIVE,    "Gold Drive (Transparent OD)" },
    { FX_PEDAL_RODENT,        "Rodent (Distortion)" },
    { FX_PEDAL_ECHO_DELAY,    "Echo Delay" },
    { FX_PEDAL_HALL_VERB,     "Hall Verb (Reverb)" },
    { FX_PEDAL_DRIP_VERB,     "Drip Verb (Spring Reverb)" },
    { FX_PEDAL_SQUEEZE_BOX,   "Squeeze Box (Compressor)" },
    { FX_PEDAL_NOISE_GATE,    "Noise Gate" },
    { FX_PEDAL_TONE_SCULPTOR, "Tone Sculptor (Graphic EQ)" },
    { FX_PEDAL_MAMMOTH_FUZZ,  "Mammoth Fuzz (Big Muff)" },
    { FX_PEDAL_ROUND_FUZZ,    "Round Fuzz (Germanium)" },
    { FX_PEDAL_CHAOS_FUZZ,    "Chaos Fuzz (Gated)" },
    { FX_PEDAL_GRIT_CRUSH,    "Grit Crush (Bitcrusher)" },
    { FX_PEDAL_RING_TONE,     "Ring Tone (Ring Mod)" },
    { FX_PEDAL_WARM_TAPE,     "Warm Tape (Tape Sat)" },
    { FX_PEDAL_DRIFT_VIBRATO, "Drift Vibrato (Pitch Vibrato)" },
    { FX_PEDAL_JET_FLANGER,   "Jet Flanger (Through-Zero)" },
    { FX_PEDAL_PLATE_VERB,    "Plate Verb (Plate Reverb)" },
    { FX_PEDAL_SHIMMER_VERB,  "Shimmer Verb (Octave Shimmer)" },
    { FX_PEDAL_CLOUD_VERB,    "Cloud Verb (Ambient/Freeze)" },
    { FX_PEDAL_OCTAVE_ENGINE, "Octave Engine (Polyphonic Octave)" },
    { FX_PEDAL_LOOP_STATION,  "Loop Station (Looper)" },
    { FX_PEDAL_INFINITE_HOLD, "Infinite Hold (Freeze/Drone)" },
    { FX_PEDAL_GRAIN_CLOUD,   "Grain Cloud (Granular Delay)" },
};
static const int s_pedal_menu_count = 24;

/* ── Texture path helpers ──────────────────────────────────────── */

/* Convert a display name like "Jade Drive" -> "jade_drive" */
static void type_to_filename(const char *type_name, char *out, int out_size) {
    int i = 0;
    for (; type_name[i] && i < out_size - 1; i++) {
        char c = type_name[i];
        if (c == ' ') c = '_';
        else if (c >= 'A' && c <= 'Z') c = c + 32;
        out[i] = c;
    }
    out[i] = '\0';
}

/* Cab type enum -> filename base (without _nobg.png) */
static const char *s_cab_filenames[] = {
    "1x12_open",      /* FX_CAB_1X12_OPEN */
    "2x12_closed",    /* FX_CAB_2X12_CLOSED */
    "4x12_straight",  /* FX_CAB_4X12_STRAIGHT */
    "4x12_slant",     /* FX_CAB_4X12_SLANT */
};

/* Special-case pedal name overrides where type_to_filename doesn't match the asset */
static uintptr_t load_pedal_texture(const char *type_name) {
    char fname[128];
    type_to_filename(type_name, fname, sizeof(fname));

    /* "Orange Distortion" -> "orange_distortion" but asset is "orange_dist" */
    if (strcmp(fname, "orange_distortion") == 0) {
        strcpy(fname, "orange_dist");
    }

    char path[256];
    snprintf(path, sizeof(path), "resources/pedals/%s_body_nobg.png", fname);
    return fx_texture_load(path);
}

/* Map display amp names to asset filenames where they differ */
static void amp_name_to_filename(const char *type_name, char *out, int out_size) {
    type_to_filename(type_name, out, out_size);
    /* "British Crunch" -> "british_crunch" but asset is "brit_crunch" */
    if (strcmp(out, "british_crunch") == 0) strcpy(out, "brit_crunch");
}

static uintptr_t load_amp_body_texture(const char *type_name) {
    char fname[128];
    amp_name_to_filename(type_name, fname, sizeof(fname));
    char path[256];
    snprintf(path, sizeof(path), "resources/amps/%s_body_nobg.png", fname);
    return fx_texture_load(path);
}

static uintptr_t load_amp_face_texture(const char *type_name) {
    char fname[128];
    amp_name_to_filename(type_name, fname, sizeof(fname));
    char path[256];
    snprintf(path, sizeof(path), "resources/amps/%s_nobg.png", fname);
    return fx_texture_load(path);
}

static uintptr_t load_cab_texture(int cab_type_idx) {
    if (cab_type_idx < 0 || cab_type_idx >= FX_CAB_TYPE_COUNT) {
        /* Default to 4x12 straight */
        return fx_texture_load("resources/cabs/4x12_straight_nobg.png");
    }
    char path[256];
    snprintf(path, sizeof(path), "resources/cabs/%s_nobg.png", s_cab_filenames[cab_type_idx]);
    return fx_texture_load(path);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    fx_log_init(NULL);
    fx_crash_init();
    FX_INFO("GUI started");

    /* SDL init */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        FX_ERROR("SDL_Init error: %s", SDL_GetError());
        fx_log_shutdown();
        return 1;
    }

    if (NFD_Init() != NFD_OKAY) {
        FX_WARN("NFD_Init failed — custom IR / image upload will be unavailable");
    }

    custom_cabs_load();

    /* OpenGL 3.3 */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    /* Load session config (window size, device prefs) */
    SessionConfig s_session_cfg;
    session_config_load(&s_session_cfg);
    if (s_session_cfg.window_w < 800)  s_session_cfg.window_w = 800;
    if (s_session_cfg.window_h < 500)  s_session_cfg.window_h = 500;

    SDL_Window *window = SDL_CreateWindow(
        "0xFX — Guitar Amp Sim & Pedalboard",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        s_session_cfg.window_w, s_session_cfg.window_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS |
        SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_SetWindowMinimumSize(window, 800, 500);
    SDL_SetWindowHitTest(window, fx_window_hit_test, NULL);
#ifdef _WIN32
    install_borderless_wndproc(window);
#endif

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); /* vsync */

    /* ImGui init */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigDebugHighlightIdConflicts = false;

    if (s_session_cfg.theme_id >= 0 && s_session_cfg.theme_id < FX_THEME_COUNT)
        s_theme = (fx_theme_id_t)s_session_cfg.theme_id;
    setup_theme();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    /* Texture loader smoke test */
    {
        uintptr_t test_tex = fx_texture_load("resources/knobs/knob_chicken_cream.png");
        if (test_tex) FX_INFO("Texture loaded: %lu", (unsigned long)test_tex);
    }

    /* Audio + engine + MIDI init */
    fx_audio_init();
    fx_midi_init();
    fx_engine_t *engine = fx_engine_create(44100.0f);
    FX_INFO("Engine created");

    /* Audio device / settings state */
    int num_input_devices = fx_audio_get_device_count();
    int num_output_devices = fx_audio_get_output_count();
    int num_midi_devices = fx_midi_get_device_count();
    /* Apply saved device selections */
    static int   s_selected_input   = -1;
    static int   s_selected_output  = -1;
    static int   s_selected_buf_idx = 2;    /* default: 256 frames */
    static int   s_selected_sr_idx  = 0;    /* default: 44100 Hz  */
    static bool  s_audio_active     = false;
    static bool  s_monitor_only     = false; /* device open for metering, output muted */
    static int   s_selected_midi    = -1;
    static bool  s_midi_active      = false;

    /* Input gain trim state (standalone only) */
    static float s_input_gain_db  = 0.0f;   /* -24..+12 dB, default 0 dB */
    static bool  s_input_pad      = false;  /* -20 dB pad toggle */

    /* Restore from session config */
    if (s_session_cfg.input_device_idx  >= 0 && s_session_cfg.input_device_idx  < num_input_devices)
        s_selected_input  = s_session_cfg.input_device_idx;
    if (s_session_cfg.output_device_idx >= 0 && s_session_cfg.output_device_idx < num_output_devices)
        s_selected_output = s_session_cfg.output_device_idx;
    if (s_session_cfg.buf_size_idx >= 0 && s_session_cfg.buf_size_idx < 5)
        s_selected_buf_idx = s_session_cfg.buf_size_idx;
    if (s_session_cfg.sr_idx >= 0 && s_session_cfg.sr_idx < 2)
        s_selected_sr_idx = s_session_cfg.sr_idx;
    /* Restore input gain — apply to audio layer immediately */
    s_input_gain_db = s_session_cfg.input_gain_db;
    s_input_pad     = s_session_cfg.input_pad;
    fx_audio_set_input_gain_db(s_input_gain_db);
    fx_audio_set_input_pad(s_input_pad);

    static const int   buf_sizes[]  = { 64, 128, 256, 512, 1024 };
    static const char *buf_labels[] = { "64", "128", "256", "512", "1024" };
    static const int   sr_values[]  = { 44100, 48000 };
    static const char *sr_labels[]  = { "44100 Hz", "48000 Hz" };

    FX_INFO("Launched muted. Select input+output devices to start audio.");

    /* Auto-start monitoring if we have saved device preferences */
    if (s_selected_input >= 0 && num_input_devices > 0 && !s_monitor_only) {
        if (s_selected_output >= 0) fx_audio_set_output(s_selected_output);
        if (fx_audio_set_device(engine, s_selected_input)) {
            fx_audio_set_mute_output(true);
            s_monitor_only = true;
            FX_INFO("Auto-monitoring input: %s", fx_audio_get_device_name(s_selected_input));
        }
    }

    /* Auto-load last session preset first, then fall back to default */
    /* NOTE: GUI sync happens after static locals are declared below */
    static bool s_needs_gui_sync = false;
    static bool s_did_sync = false;
    static char s_preset_name[128] = "Untitled";
    static bool s_preset_modified = false;
    {
        bool loaded = fx_preset_load(engine, "presets/last_session.0xfx");
        if (!loaded) loaded = fx_preset_load(engine, "../presets/last_session.0xfx");
        if (loaded) {
            s_needs_gui_sync = true;
            snprintf(s_preset_name, sizeof(s_preset_name), "Last Session");
            FX_INFO("Last session preset restored");
        } else {
            loaded = fx_preset_load(engine, "presets/clean_sparkle.0xfx");
            if (!loaded) loaded = fx_preset_load(engine, "../presets/clean_sparkle.0xfx");
            if (loaded) {
                s_needs_gui_sync = true;
                snprintf(s_preset_name, sizeof(s_preset_name), "Clean Sparkle");
                FX_INFO("Default preset loaded: Clean Sparkle");
            } else {
                FX_WARN("Could not load any preset, using engine defaults");
            }
        }
    }

    /* Pedal ID registries — tracks IDs returned by fx_chain_add_pedal */
    static fx_pedal_id s_pre_ids[32];
    static int         s_pre_id_count = 0;
    static fx_pedal_id s_post_ids[32];
    static int         s_post_id_count = 0;

    /* Studio processor ID registry */
    static fx_studio_id s_studio_ids[8];
    static int          s_studio_id_count = 0;

    /* Sync GUI ID arrays from engine after preset load */
    if (s_needs_gui_sync) {
        s_needs_gui_sync = false;
        s_pre_id_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_PRE);
        if (s_pre_id_count > 32) s_pre_id_count = 32;
        for (int i = 0; i < s_pre_id_count; i++)
            s_pre_ids[i] = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_PRE, i);

        s_post_id_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_POST);
        if (s_post_id_count > 32) s_post_id_count = 32;
        for (int i = 0; i < s_post_id_count; i++)
            s_post_ids[i] = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_POST, i);

        FX_INFO("GUI sync: %d pre-pedals, %d post-pedals", s_pre_id_count, s_post_id_count);
        s_did_sync = true;
    }

    /* Signal chain selection state */
    static int  s_selected_node = -1;  /* index into the flattened chain array */
    static int  s_cab_type = 0;        /* current cab type for chain 0 (for texture lookup) */
    static int  s_cab_type_b = 0;      /* cab type for chain 1 (dual mode) */

    /* Dual-chain (Y-split) state */
    static fx_chain_id s_chain_b = -1; /* chain ID for the second parallel path, -1 = single */

    /* Sync dual chain state from engine after preset load */
    if (s_did_sync) {
        s_did_sync = false;
        int chain_count = fx_chain_get_count(engine);
        s_chain_b = (chain_count > 1) ? 1 : -1;
        s_selected_node = -1;
        FX_INFO("Chain sync: %d chains, chain_b=%d", chain_count, (int)s_chain_b);
    }

    /* Layout constants */
    static const float TOOLBAR_H      = 64.0f;
    static const float LOOPER_H       = 140.0f;
    static const float STATUS_H       = 50.0f;
    static const float NODE_W         = 80.0f;
    static const float NODE_H         = 60.0f;
    static const float NODE_SPACING   = 56.0f;  /* wider to fit 40px [+] btn */
    static const float ADD_BTN_W      = 26.0f;
    static const float CHAIN_PADDING  = 20.0f;

    /* ── Main loop ────────────────────────────────────────────── */
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        /* Start ImGui frame */
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        float win_w = io.DisplaySize.x;
        float win_h = io.DisplaySize.y;

        /* Corner dirt vignettes — draw on the foreground drawlist so they
         * tint over the toolbar / looper panel / status bar (otherwise
         * those opaque windows cover the background drawlist). The dirt
         * colors all have partial alpha, so widgets remain readable. */

        /* ── Theme textures (loaded once) ────────────────────────── */
        static uintptr_t s_tex_pedalboard  = 0;
        static uintptr_t s_tex_tolex       = 0;
        static bool      s_theme_tex_tried = false;
        if (!s_theme_tex_tried) {
            s_tex_pedalboard = fx_texture_load("resources/theme/pedalboard_surface_nobg.png");
            s_tex_tolex      = fx_texture_load("resources/theme/tolex_surface_nobg.png");
            s_theme_tex_tried = true;
        }

        /* Save-As state (shared between preset browser and Ctrl+Shift+S) */
        static bool s_save_as_open = false;
        static char s_save_as_name[128] = "";
        static char s_save_toast[512] = "";
        static float s_save_toast_timer = 0.0f;

        /* ── Toolbar ──────────────────────────────────────────── */
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(win_w, TOOLBAR_H));
            ImGui::Begin("##toolbar", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

            /* Toolbar gradient: panel at top, bg at bottom, border separator. */
            {
                const fx_theme_t *th = fx_theme_get(s_theme);
                ImDrawList *dl_tb = ImGui::GetWindowDrawList();
                ImVec2 tb_min = ImGui::GetWindowPos();
                ImVec2 tb_max = ImVec2(tb_min.x + win_w, tb_min.y + TOOLBAR_H);
                ImU32 c_top = theme_col32(scale_rgb(th->panel, 1.15f));
                ImU32 c_bot = theme_col32(th->bg);
                dl_tb->AddRectFilledMultiColor(tb_min, tb_max,
                                               c_top, c_top, c_bot, c_bot);
                ImVec4 sep = th->border; sep.w = 0.7f;
                dl_tb->AddLine(
                    ImVec2(tb_min.x, tb_max.y - 1.0f),
                    ImVec2(tb_max.x, tb_max.y - 1.0f),
                    theme_col32(sep), 1.0f);
            }

            /* Neon logo image (cached) — pre-trimmed + faded asset */
            {
                static uintptr_t s_logo_tex = 0;
                static bool s_logo_tried = false;
                static float s_logo_aspect = 1.96f;
                if (!s_logo_tried) {
                    s_logo_tex = fx_texture_load("resources/logo/logo_neon_v4_red_trim_fade.png");
                    s_logo_tried = true;
                    int lw = 0, lh = 0;
                    if (s_logo_tex && fx_texture_get_size(s_logo_tex, &lw, &lh) && lh > 0)
                        s_logo_aspect = (float)lw / (float)lh;
                }
                if (s_logo_tex) {
                    float logo_h = TOOLBAR_H - 8.0f;
                    float logo_w = logo_h * s_logo_aspect;
                    /* Draw via DrawList (not ImGui::Image) so the logo doesn't
                     * register as a hovered item and block toolbar drag. */
                    ImVec2 cp = ImGui::GetCursorScreenPos();
                    cp.y += 4.0f;
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)s_logo_tex, cp,
                        ImVec2(cp.x + logo_w, cp.y + logo_h));
                } else {
                    ImGui::TextUnformatted("0xFX");
                }
            }
            ImGui::SameLine(130);

            /* Tuner */
            {
                float freq = fx_tuner_get_frequency(engine);
                bool active = (freq > 20.0f);
                const char *note = active ? fx_tuner_get_note_name(engine) : "--";
                float raw_cents = active ? fx_tuner_get_cents(engine) : 0.0f;

                /* Time-constant EMA smoothing — frame-rate independent and
                 * slower than before (tau=0.22s → roughly half the jitter of
                 * the old alpha=0.15). A 0.4¢ deadzone snaps the dot to
                 * center when the player is in-tune, killing the shimmer. */
                static float smoothed_cents = 0.0f;
                if (!active) {
                    smoothed_cents = 0.0f;
                } else {
                    const float tau = 0.45f;
                    float dt = ImGui::GetIO().DeltaTime;
                    if (dt < 0.001f) dt = 0.001f;
                    if (dt > 0.100f) dt = 0.100f;
                    float a = 1.0f - expf(-dt / tau);
                    smoothed_cents += a * (raw_cents - smoothed_cents);
                    if (fabsf(smoothed_cents - raw_cents) < 1.2f)
                        smoothed_cents = raw_cents;
                }
                float cents = smoothed_cents;

                ImVec4 note_color;
                if (!active) {
                    note_color = ImVec4(0.45f, 0.40f, 0.35f, 1.0f);
                } else if (cents < 0.0f ? -cents < 5.0f : cents < 5.0f) {
                    note_color = ImVec4(0.20f, 0.90f, 0.30f, 1.0f);
                } else if (cents < 0.0f ? -cents < 15.0f : cents < 15.0f) {
                    note_color = ImVec4(0.95f, 0.85f, 0.10f, 1.0f);
                } else {
                    note_color = ImVec4(0.95f, 0.25f, 0.20f, 1.0f);
                }

                ImGui::PushStyleColor(ImGuiCol_Text, note_color);
                ImGui::SetWindowFontScale(1.4f);
                ImGui::Text("%s", note);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                /* Lock the bar to a fixed X so a 1-char ("E") vs 2-char
                 * ("D#") note doesn't shove the tuner sideways. */
                ImGui::SameLine(175.0f);

                /* Cents bar */
                {
                    const float bar_w    = 200.0f;
                    const float bar_h    = 10.0f;
                    const float dot_r    = 6.0f;
                    const float padding  = dot_r;

                    ImVec2 cursor = ImGui::GetCursorScreenPos();
                    float toolbar_top = ImGui::GetWindowPos().y;
                    float bar_cx_y   = toolbar_top + TOOLBAR_H * 0.5f;
                    float bar_top_y  = bar_cx_y - bar_h * 0.5f;
                    float bar_bot_y  = bar_cx_y + bar_h * 0.5f;

                    float bar_x0 = cursor.x + padding;
                    float bar_x1 = bar_x0 + bar_w;

                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(ImVec2(bar_x0, bar_top_y), ImVec2(bar_x1, bar_bot_y),
                                      IM_COL32(50, 45, 40, 255), 3.0f);

                    float mid_x = bar_x0 + bar_w * 0.5f;
                    dl->AddRectFilled(ImVec2(mid_x - 1.0f, bar_top_y - 2.0f),
                                      ImVec2(mid_x + 1.0f, bar_bot_y + 2.0f),
                                      IM_COL32(120, 110, 90, 255));

                    if (active) {
                        float t = (cents + 50.0f) / 100.0f;
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                        float dot_x = bar_x0 + t * bar_w;
                        ImU32 dot_col = IM_COL32(
                            (int)(note_color.x * 255), (int)(note_color.y * 255),
                            (int)(note_color.z * 255), 255);
                        dl->AddCircleFilled(ImVec2(dot_x, bar_cx_y), dot_r, dot_col);
                    }

                    /* Advance the cursor without submitting a hoverable item —
                     * an invisible Dummy here would register as "hovered" and
                     * block the SDL hit-test's window-drag path over the tuner
                     * strip. The next widget anchors absolutely via SameLine. */
                    ImGui::SetCursorScreenPos(ImVec2(bar_x1 + padding, cursor.y));

                    /* TUNER label */
                    {
                        ImDrawList *tdl = ImGui::GetWindowDrawList();
                        ImGui::SetWindowFontScale(0.65f);
                        const char *tlbl = "TUNER";
                        ImVec2 tsz = ImGui::CalcTextSize(tlbl);
                        float tlx = cursor.x + padding + (bar_w - tsz.x) * 0.5f;
                        float tly = bar_bot_y + 4.0f;
                        tdl->AddText(ImVec2(tlx, tly), IM_COL32(120, 110, 90, 150), tlbl);
                        ImGui::SetWindowFontScale(1.0f);
                    }
                }
            }

            ImGui::SameLine(405);

            /* ── Preset name display ──────────────────────────── */
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.55f, 1.0f));
                ImGui::SetWindowFontScale(0.9f);
                ImGui::AlignTextToFramePadding();
                if (s_preset_modified)
                    ImGui::Text("%s (unsaved)", s_preset_name);
                else
                    ImGui::Text("%s", s_preset_name);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Current preset — Ctrl+S to save, Ctrl+Shift+S to save as");
            }

            ImGui::SameLine(0, 10);

            /* ── Presets browser button ───────────────────────── */
            {
                push_toolbar_button_colors();
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button("Presets", ImVec2(70.0f, 32.0f))) {
                    if (s_browser_needs_scan) preset_browser_scan();
                    ImGui::OpenPopup("preset_browser_popup");
                }
                ImGui::PopStyleVar();
                pop_toolbar_button_colors();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Browse factory and user presets");
            }

            ImGui::SameLine(0, 6);

            /* ── Looper panel toggle ──────────────────────────── */
            {
                const fx_theme_t *th = fx_theme_get(s_theme);
                ImVec4 btn_bg = s_looper_panel_open
                    ? scale_rgb(th->accent, 0.55f)
                    : th->frame;
                ImGui::PushStyleColor(ImGuiCol_Button, btn_bg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, th->frame_hover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  th->frame_active);
                ImGui::PushStyleColor(ImGuiCol_Text,          th->text);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button("Looper", ImVec2(70.0f, 32.0f))) {
                    s_looper_panel_open = !s_looper_panel_open;
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("9-slot looper (1-9 tap, R arm, Space play/pause, Tab focus)");
            }

            /* ── Preset browser popup ────────────────────────── */
            ImGui::SetNextWindowSizeConstraints(ImVec2(640.0f, 0.0f),
                                                ImVec2(640.0f, FLT_MAX));
            if (ImGui::BeginPopup("preset_browser_popup")) {
                if (s_browser_needs_scan) preset_browser_scan();

                const float pb_popup_w = 640.0f;
                const float pb_btn_w   = 22.0f;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.78f, 0.6f, 1.0f));
                ImGui::Text("Preset Library");
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::SetCursorPosX(pb_popup_w - pb_btn_w - ImGui::GetStyle().WindowPadding.x);
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
                if (ImGui::Button("X##preset_close", ImVec2(pb_btn_w, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor(3);
                ImGui::Separator();

                /* ── Surprise Me button with rainbow border ──── */
                {
                    float t = (float)ImGui::GetTime();
                    float hue = fmodf(t * 0.3f, 1.0f);
                    ImVec4 rainbow = hsv_to_rgb(hue, 0.8f, 0.9f);
                    (void)hsv_to_rgb(hue, 0.5f, 0.6f); /* rainbow_dim available if needed */

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.10f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.15f, 0.12f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.07f, 0.05f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, rainbow);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

                    if (ImGui::Button("Mystery Rig", ImVec2(ImGui::GetContentRegionAvail().x, 36.0f))) {
                        surprise_me_generate(engine, s_preset_name, sizeof(s_preset_name));
                        s_preset_modified = false;
                        /* Immediate sync */
                        s_pre_id_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_PRE);
                        if (s_pre_id_count > 32) s_pre_id_count = 32;
                        for (int si = 0; si < s_pre_id_count; si++)
                            s_pre_ids[si] = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_PRE, si);
                        s_post_id_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_POST);
                        if (s_post_id_count > 32) s_post_id_count = 32;
                        for (int si = 0; si < s_post_id_count; si++)
                            s_post_ids[si] = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_POST, si);
                        s_studio_id_count = 0;
                        s_selected_node = -1;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(4);

                    /* Draw rainbow border around the button */
                    {
                        ImDrawList *dl = ImGui::GetWindowDrawList();
                        ImVec2 bmin = ImGui::GetItemRectMin();
                        ImVec2 bmax = ImGui::GetItemRectMax();
                        float pulse = 0.7f + 0.3f * sinf(t * 4.0f);
                        ImU32 border_col = ImGui::ColorConvertFloat4ToU32(
                            ImVec4(rainbow.x * pulse, rainbow.y * pulse, rainbow.z * pulse, 0.9f));
                        dl->AddRect(bmin, bmax, border_col, 6.0f, 0, 2.0f);
                        /* Outer glow */
                        ImU32 glow_col = ImGui::ColorConvertFloat4ToU32(
                            ImVec4(rainbow.x * 0.4f, rainbow.y * 0.4f, rainbow.z * 0.4f, 0.3f * pulse));
                        dl->AddRect(ImVec2(bmin.x - 1, bmin.y - 1),
                                    ImVec2(bmax.x + 1, bmax.y + 1), glow_col, 7.0f, 0, 2.0f);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Generate a random rig — surprise yourself!");

                    ImGui::Spacing();
                }

                /* ── Category tabs ───────────────────────────── */
                static int s_selected_cat = 0;

                /* Tab-style category selector */
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
                for (int c = 0; c < s_preset_category_count; c++) {
                    /* Count presets in this category */
                    int cat_count = 0;
                    for (int p = 0; p < s_browser_preset_count; p++) {
                        if (strcmp(s_browser_presets[p].category, s_preset_categories[c]) == 0)
                            cat_count++;
                    }
                    if (cat_count == 0) continue;

                    if (c > 0) ImGui::SameLine();
                    bool selected = (s_selected_cat == c);
                    if (selected) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.25f, 0.15f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.6f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.10f, 0.08f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.55f, 0.45f, 1.0f));
                    }
                    char tab_label[64];
                    snprintf(tab_label, sizeof(tab_label), "%s (%d)", s_preset_categories[c], cat_count);
                    if (ImGui::Button(tab_label)) {
                        s_selected_cat = c;
                    }
                    ImGui::PopStyleColor(2);
                }
                ImGui::PopStyleVar();

                ImGui::Separator();

                /* ── Preset list for selected category ───────── */
                ImGui::BeginChild("preset_list", ImVec2(480, 320), true);

                const char *sel_cat = s_preset_categories[s_selected_cat];
                for (int p = 0; p < s_browser_preset_count; p++) {
                    PresetEntry *pe = &s_browser_presets[p];
                    if (strcmp(pe->category, sel_cat) != 0) continue;

                    ImGui::PushID(p);

                    /* Factory presets get a gold icon, user presets get a blue icon */
                    if (pe->is_factory) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.7f, 0.3f, 1.0f));
                        ImGui::Text("[F]");
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 0.85f, 1.0f));
                        ImGui::Text("[U]");
                    }
                    ImGui::PopStyleColor();
                    ImGui::SameLine();

                    /* Preset name as selectable */
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.82f, 0.65f, 1.0f));
                    if (ImGui::Selectable(pe->name, false, ImGuiSelectableFlags_None, ImVec2(440, 0))) {
                        /* Load this preset */
                        bool ok = fx_preset_load(engine, pe->path);
                        if (ok) {
                            snprintf(s_preset_name, sizeof(s_preset_name), "%s", pe->name);
                            s_preset_modified = false;
                            /* Immediate sync */
                            s_pre_id_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_PRE);
                            if (s_pre_id_count > 32) s_pre_id_count = 32;
                            for (int si = 0; si < s_pre_id_count; si++)
                                s_pre_ids[si] = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_PRE, si);
                            s_post_id_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_POST);
                            if (s_post_id_count > 32) s_post_id_count = 32;
                            for (int si = 0; si < s_post_id_count; si++)
                                s_post_ids[si] = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_POST, si);
                            s_studio_id_count = 0;
                            s_selected_node = -1;
                            FX_INFO("Loaded preset: %s (%d pre, %d post)", pe->name, s_pre_id_count, s_post_id_count);
                        } else {
                            FX_ERROR("Failed to load preset: %s", pe->path);
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor();

                    /* Description tooltip */
                    if (ImGui::IsItemHovered() && pe->description[0]) {
                        ImGui::SetTooltip("%s", pe->description);
                    }

                    /* Right-click context menu — delete for user presets only */
                    if (!pe->is_factory && ImGui::BeginPopupContextItem()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.30f, 1.0f));
                        if (ImGui::Selectable("Delete")) {
                            remove(pe->path);
                            FX_INFO("Deleted preset: %s", pe->name);
                            s_browser_needs_scan = true;
                        }
                        ImGui::PopStyleColor();
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }

                ImGui::EndChild();

                ImGui::Separator();

                /* ── Save / Save As at bottom ────────────────── */
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.15f, 0.12f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.24f, 0.18f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.68f, 0.5f, 1.0f));

                    if (ImGui::Button("Save (Ctrl+S)", ImVec2(150, 0))) {
                        /* If a named preset is loaded, overwrite it; otherwise prompt Save As */
                        bool has_name = (s_preset_name[0] != '\0'
                            && strcmp(s_preset_name, "Untitled") != 0
                            && strcmp(s_preset_name, "Last Session") != 0);
                        if (has_name) {
                            char path[400];
                            snprintf(path, sizeof(path), "presets/%s.0xfx", s_preset_name);
                            bool ok = fx_preset_save(engine, path);
                            if (!ok) {
                                snprintf(path, sizeof(path), "../presets/%s.0xfx", s_preset_name);
                                ok = fx_preset_save(engine, path);
                            }
                            if (ok) {
                                s_preset_modified = false;
                                s_browser_needs_scan = true;
                                FX_INFO("Saved preset: %s", s_preset_name);
                            } else {
                                FX_ERROR("Save failed: %s", s_preset_name);
                            }
                            ImGui::CloseCurrentPopup();
                        } else {
                            /* No preset loaded — open Save As */
                            ImGui::CloseCurrentPopup();
                            s_save_as_open = true;
                        }
                        /* Always quick-save session too */
                        fx_preset_save(engine, "presets/last_session.0xfx");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Save As... (Ctrl+Shift+S)", ImVec2(200, 0))) {
                        ImGui::CloseCurrentPopup();
                        /* Will trigger save-as popup via keyboard shortcut path */
                        s_save_as_open = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Refresh", ImVec2(80, 0))) {
                        s_browser_needs_scan = true;
                        preset_browser_scan();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Rescan preset directories");

                    ImGui::PopStyleColor(3);
                }

                ImGui::EndPopup();
            }

            ImGui::SameLine(0, 10);

            /* ── LIVE button — theme accent when ON, frame tone when OFF ── */
            {
                ImVec2 live_sz(80.0f, 32.0f);
                const fx_theme_t *th = fx_theme_get(s_theme);
                if (s_audio_active) {
                    float t = (float)ImGui::GetTime();
                    float pulse = 0.85f + 0.15f * sinf(t * 3.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button,        scale_rgb(th->accent,        pulse));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, th->accent_hover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  th->accent_active);
                    ImGui::PushStyleColor(ImGuiCol_Text,          scale_rgb(th->accent_glow, pulse));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,        th->frame);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, th->frame_hover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  th->frame_active);
                    ImGui::PushStyleColor(ImGuiCol_Text,          th->text_dim);
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button(s_audio_active ? "LIVE [ON]" : "LIVE [OFF]", live_sz)) {
                    if (s_audio_active) {
                        /* Switch to monitor mode — keep device open, mute output */
                        fx_audio_set_mute_output(true);
                        s_audio_active = false;
                        s_monitor_only = true;
                        FX_INFO("LIVE off (monitoring input)");
                    } else {
                        if (s_selected_input < 0 && num_input_devices > 0) s_selected_input = 0;
                        if (s_selected_output < 0 && num_output_devices > 0) s_selected_output = 0;
                        if (s_selected_input >= 0) {
                            if (s_monitor_only) {
                                /* Already monitoring — just unmute */
                                fx_audio_set_mute_output(false);
                                s_audio_active = true;
                                s_monitor_only = false;
                                FX_INFO("LIVE on (unmuted)");
                            } else {
                                /* Open device fresh */
                                if (s_selected_output >= 0) fx_audio_set_output(s_selected_output);
                                if (fx_audio_set_device(engine, s_selected_input)) {
                                    fx_audio_set_mute_output(false);
                                    s_audio_active = true;
                                    FX_INFO("LIVE on: in=%s out=%s",
                                        fx_audio_get_device_name(s_selected_input),
                                        s_selected_output >= 0 ? fx_audio_get_output_name(s_selected_output) : "(default)");
                                }
                            }
                        }
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
                /* Glow ring */
                {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
                    float t = (float)ImGui::GetTime();
                    if (s_audio_active) {
                        /* Hot glow when active */
                        int g = (int)(120.0f * (0.3f + 0.2f * sinf(t * 3.0f)));
                        dl->AddRect(ImVec2(bmin.x-2,bmin.y-2), ImVec2(bmax.x+2,bmax.y+2),
                                    IM_COL32(255, g, 30, g), 6.0f, 0, 2.0f);
                    } else {
                        /* Pulsing "attention" glow when inactive — draw users to click */
                        float pulse = 0.4f + 0.4f * sinf(t * 2.0f);
                        int a = (int)(pulse * 120.0f);
                        dl->AddRect(ImVec2(bmin.x-3,bmin.y-3), ImVec2(bmax.x+3,bmax.y+3),
                                    IM_COL32(200, 140, 30, a), 8.0f, 0, 2.0f);
                        dl->AddRect(ImVec2(bmin.x-6,bmin.y-6), ImVec2(bmax.x+6,bmax.y+6),
                                    IM_COL32(200, 140, 30, a/3), 10.0f, 0, 1.5f);
                    }
                }
                /* Tooltip */
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Toggle audio processing on/off (Space)");
            }
            ImGui::SameLine();

            /* ── DUAL/SINGLE chain toggle ──────────────────────── */
            {
                bool is_dual = (s_chain_b >= 0);
                const fx_theme_t *thd = fx_theme_get(s_theme);
                if (is_dual) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        scale_rgb(thd->accent, 0.55f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, thd->accent_hover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  thd->accent_active);
                    ImGui::PushStyleColor(ImGuiCol_Text,          thd->accent_glow);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,        thd->frame);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, thd->frame_hover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  thd->frame_active);
                    ImGui::PushStyleColor(ImGuiCol_Text,          thd->text_dim);
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button(is_dual ? "DUAL##split" : "SINGLE##split",
                                  ImVec2(72.0f, 32.0f))) {
                    if (is_dual) {
                        /* Switch back to SINGLE — destroy chain B */
                        if (s_chain_b >= 0) {
                            fx_chain_destroy(engine, s_chain_b);
                            s_chain_b = -1;
                        }
                        /* Deselect if we had selected chain B's nodes */
                        s_selected_node = -1;
                    } else {
                        /* Switch to DUAL — create chain B */
                        s_chain_b = fx_chain_create(engine);
                        if (s_chain_b >= 0) {
                            fx_chain_set_mix(engine, FX_CHAIN_DEFAULT, 0.5f);
                            fx_chain_set_mix(engine, s_chain_b, 0.5f);
                            FX_INFO("Dual chain enabled: chain B id=%d", (int)s_chain_b);
                        }
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);

                /* Glow border when dual is active */
                if (is_dual) {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
                    ImVec4 glow = thd->accent_glow; glow.w = 0.70f;
                    dl->AddRect(ImVec2(bmin.x-2,bmin.y-2), ImVec2(bmax.x+2,bmax.y+2),
                                theme_col32(glow), 6.0f, 0, 1.5f);
                }
            }

            ImGui::SameLine();

            /* REC button + format selector (REC first, format after) */
            {
                static int rec_format_idx = 0;

                /* Init recording directory to ~/Music/0xFX once */
                if (!s_rec_dir_inited) {
                    const char *home = getenv("HOME");
                    if (!home) home = getenv("USERPROFILE");
                    if (home) {
                        snprintf(s_rec_dir, sizeof(s_rec_dir), "%s/Music/0xFX", home);
                    } else {
                        snprintf(s_rec_dir, sizeof(s_rec_dir), "recordings");
                    }
                    s_rec_dir_inited = true;
                }

                bool recording = fx_recorder_active();

                /* REC button — theme frame tone when idle, red pulse when recording. */
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                {
                    const fx_theme_t *thr = fx_theme_get(s_theme);
                    if (recording) {
                        float t = (float)ImGui::GetTime();
                        float pulse = 0.7f + 0.3f * sinf(t * 4.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button,
                            ImVec4(0.50f * pulse, 0.05f, 0.05f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text,
                            ImVec4(1.0f, 0.3f, 0.2f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(0.65f, 0.12f, 0.10f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            ImVec4(0.40f, 0.08f, 0.08f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button,        thr->frame);
                        ImGui::PushStyleColor(ImGuiCol_Text,          thr->text_dim);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, thr->frame_hover);
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  thr->frame_active);
                    }
                }

                char rec_label[32];
                if (recording) {
                    float dur = fx_recorder_duration();
                    int mins = (int)(dur / 60.0f);
                    int secs = (int)dur % 60;
                    snprintf(rec_label, sizeof(rec_label), "%d:%02d", mins, secs);
                } else {
                    snprintf(rec_label, sizeof(rec_label), "REC");
                }

                if (ImGui::Button(rec_label, ImVec2(recording ? 56.0f : 48.0f, 32.0f))) {
                    if (recording) {
                        fx_recorder_stop();
                        ImGui::OpenPopup("rec_saved_popup");
                        s_rec_saved_popup_open = true;
                    } else {
                        const char *exts[] = { ".wav", ".wav", ".mp3", ".mp3", ".flac", ".flac" };
                        char rec_path[768];
                        /* Ensure recording directory exists */
                        #ifdef _WIN32
                        mkdir(s_rec_dir);
                        #else
                        mkdir(s_rec_dir, 0755);
                        #endif
                        /* Timestamp filename: recording_2026-03-21_110555.wav */
                        time_t now = time(NULL);
                        struct tm *t = localtime(&now);
                        snprintf(rec_path, sizeof(rec_path),
                            "%s/recording_%04d-%02d-%02d_%02d%02d%02d%s",
                            s_rec_dir,
                            t->tm_year+1900, t->tm_mon+1, t->tm_mday,
                            t->tm_hour, t->tm_min, t->tm_sec,
                            exts[rec_format_idx]);
                        fx_recorder_start(rec_path, (fx_record_format_t)rec_format_idx, 44100.0f);
                        strncpy(s_last_rec_path, rec_path, sizeof(s_last_rec_path) - 1);
                        s_last_rec_path[sizeof(s_last_rec_path) - 1] = '\0';
                    }
                }
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
                if (ImGui::IsItemHovered()) {
                    if (recording) {
                        float dur = fx_recorder_duration();
                        ImGui::SetTooltip("Stop recording (%.1f sec)", dur);
                    } else {
                        ImGui::SetTooltip("Record processed output (%s)",
                            fx_recorder_format_name((fx_record_format_t)rec_format_idx));
                    }
                }

                /* Folder icon — opens the recording directory in the OS
                 * file browser. Always shown next to REC. */
                ImGui::SameLine();
                {
                    ImVec2 fld_sz(30.0f, 30.0f);
                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    bool clicked = ImGui::InvisibleButton("##rec_folder", fld_sz);
                    bool hov = ImGui::IsItemHovered();
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    const fx_theme_t *thf = fx_theme_get(s_theme);
                    ImU32 fg = theme_col32(hov ? thf->text : thf->text_dim);

                    /* Simple manila-folder glyph: tab + body. */
                    float cx = p0.x + fld_sz.x * 0.5f;
                    float cy = p0.y + fld_sz.y * 0.5f;
                    float w2 = 9.0f;   /* half-width of folder body */
                    float h2 = 6.0f;   /* half-height of folder body */
                    /* Tab on top-left */
                    ImVec2 tab[4] = {
                        ImVec2(cx - w2,        cy - h2 - 3.0f),
                        ImVec2(cx - w2 + 6.0f, cy - h2 - 3.0f),
                        ImVec2(cx - w2 + 8.0f, cy - h2),
                        ImVec2(cx - w2,        cy - h2),
                    };
                    dl->AddConvexPolyFilled(tab, 4, fg);
                    /* Body */
                    dl->AddRectFilled(ImVec2(cx - w2, cy - h2),
                                      ImVec2(cx + w2, cy + h2), fg, 1.5f);
                    /* Inner slit for folder look */
                    dl->AddRectFilled(ImVec2(cx - w2 + 1.0f, cy - h2 + 2.0f),
                                      ImVec2(cx + w2 - 1.0f, cy - h2 + 3.0f),
                                      theme_col32(thf->bg));

                    if (clicked) fx_open_folder(s_rec_dir);
                    if (hov)
                        ImGui::SetTooltip("Open recording folder\n%s", s_rec_dir);
                }

                /* Format dropdown after REC button */
                if (!recording) {
                    ImGui::SameLine();
                    /* FramePadding.y = 8 makes the Combo 32 px tall to match
                     * the Presets/Looper/Settings/REC buttons on the toolbar. */
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 8.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                    {
                        const fx_theme_t *thf = fx_theme_get(s_theme);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, thf->frame);
                        ImGui::PushStyleColor(ImGuiCol_Text,    thf->text);
                    }
                    ImGui::PushItemWidth(110.0f);
                    const char *fmt_names[] = {
                        "WAV 16-bit", "WAV 24-bit",
                        "MP3 192k", "MP3 320k",
                        "FLAC 16-bit", "FLAC 24-bit"
                    };
                    ImGui::Combo("##rec_fmt", &rec_format_idx, fmt_names, FX_RECORD_FORMAT_COUNT);
                    ImGui::PopItemWidth();
                    ImGui::PopStyleColor(2);
                    ImGui::PopStyleVar(2);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Recording format");

                    /* Gear button — opens the recording save-location modal. */
                    ImGui::SameLine();
                    {
                        ImVec2 gear_sz(30.0f, 30.0f);
                        ImVec2 p0 = ImGui::GetCursorScreenPos();
                        bool clicked = ImGui::InvisibleButton("##rec_dir", gear_sz);
                        bool hov = ImGui::IsItemHovered();
                        ImDrawList *dl = ImGui::GetWindowDrawList();
                        const fx_theme_t *thg = fx_theme_get(s_theme);
                        /* Transparent background — the gear floats on the toolbar
                         * gradient. The "hole" in the gear center is drawn with the
                         * gear's own foreground color + alpha=0 so we only punch
                         * the icon, not a box around it. */
                        ImU32 fg = theme_col32(hov ? thg->text : thg->text_dim);

                        ImVec2 c(p0.x + gear_sz.x * 0.5f, p0.y + gear_sz.y * 0.5f);
                        const float R_out = 10.0f;
                        const float R_ring = 7.5f;
                        const float R_hole = 3.0f;
                        const int   teeth = 8;
                        const float tooth_half = 0.22f;
                        for (int i = 0; i < teeth; i++) {
                            float a = (float)i * (6.2831853f / teeth);
                            float a0 = a - tooth_half, a1 = a + tooth_half;
                            ImVec2 q[4] = {
                                ImVec2(c.x + cosf(a0) * R_ring, c.y + sinf(a0) * R_ring),
                                ImVec2(c.x + cosf(a0) * R_out,  c.y + sinf(a0) * R_out),
                                ImVec2(c.x + cosf(a1) * R_out,  c.y + sinf(a1) * R_out),
                                ImVec2(c.x + cosf(a1) * R_ring, c.y + sinf(a1) * R_ring),
                            };
                            dl->AddConvexPolyFilled(q, 4, fg);
                        }
                        dl->AddCircleFilled(c, R_ring, fg, 24);
                        /* Center "hole" — draw the toolbar gradient through by
                         * using the theme bg. */
                        dl->AddCircleFilled(c, R_hole, theme_col32(thg->bg), 16);

                        if (clicked) {
                            strncpy(s_rec_dir_edit, s_rec_dir, sizeof(s_rec_dir_edit));
                            s_rec_dir_modal = true;
                        }
                        if (hov)
                            ImGui::SetTooltip("Recordings save to:\n%s", s_rec_dir);
                    }
                }

                /* ── Recording saved popup ────────────────────────
                 * Shown after fx_recorder_stop() to confirm where the file
                 * landed and offer a one-click path to the OS file browser. */
                ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 0.0f),
                                                    ImVec2(520.0f, FLT_MAX));
                if (ImGui::BeginPopup("rec_saved_popup")) {
                    const float rs_popup_w = 520.0f;
                    const float rs_btn_w   = 22.0f;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.78f, 0.6f, 1.0f));
                    ImGui::Text("Recording saved");
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(rs_popup_w - rs_btn_w - ImGui::GetStyle().WindowPadding.x);
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
                    if (ImGui::Button("X##rec_saved_close", ImVec2(rs_btn_w, 0))) {
                        ImGui::CloseCurrentPopup();
                        s_rec_saved_popup_open = false;
                    }
                    ImGui::PopStyleColor(3);
                    ImGui::Separator();

                    /* Split file from dir for readable display. */
                    const char *slash = strrchr(s_last_rec_path, '/');
#ifdef _WIN32
                    const char *bslash = strrchr(s_last_rec_path, '\\');
                    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
                    const char *fname = slash ? slash + 1 : s_last_rec_path;

                    ImGui::TextDisabled("File");
                    ImGui::TextWrapped("%s", fname);
                    ImGui::Spacing();
                    ImGui::TextDisabled("Folder");
                    ImGui::TextWrapped("%s", s_rec_dir);
                    ImGui::Spacing();
                    if (ImGui::Button("Open Folder", ImVec2(140, 28))) {
                        fx_open_folder(s_rec_dir);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Close", ImVec2(80, 28))) {
                        ImGui::CloseCurrentPopup();
                        s_rec_saved_popup_open = false;
                    }
                    ImGui::EndPopup();
                }
            }

            /* Recording directory modal */
            if (s_rec_dir_modal) {
                ImGui::OpenPopup("rec_dir_popup");
            }
            if (ImGui::BeginPopupModal("rec_dir_popup", &s_rec_dir_modal,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Recording Save Location");
                ImGui::Separator();
                ImGui::SetNextItemWidth(400);
                ImGui::InputText("##rec_dir_path", s_rec_dir_edit,
                                 sizeof(s_rec_dir_edit));
                ImGui::Spacing();
                ImGui::TextDisabled("Recordings will be saved to this directory.");
                ImGui::TextDisabled("The folder will be created automatically.");
                ImGui::Spacing();
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    strncpy(s_rec_dir, s_rec_dir_edit, sizeof(s_rec_dir));
                    s_rec_dir[sizeof(s_rec_dir) - 1] = '\0';
                    s_rec_dir_modal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    s_rec_dir_modal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine();

            /* Theme picker button — cycles through themes on click, also Ctrl+T. */
            {
                char tlabel[48];
                snprintf(tlabel, sizeof(tlabel), "Theme: %s", fx_theme_name(s_theme));
                push_toolbar_button_colors();
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
                if (ImGui::Button(tlabel, ImVec2(0.0f, 32.0f))) {
                    ImGui::OpenPopup("theme_picker_popup");
                }
                ImGui::PopStyleVar();
                pop_toolbar_button_colors();
                if (ImGui::IsItemHovered()) {
                    const fx_theme_t *t = fx_theme_get(s_theme);
                    ImGui::SetTooltip("%s\n%s\n\nCtrl+T to cycle", t->name, t->description);
                }

                if (ImGui::BeginPopup("theme_picker_popup")) {
                    ImGui::TextDisabled("Theme");
                    ImGui::Separator();
                    for (int i = 0; i < (int)FX_THEME_COUNT; i++) {
                        const fx_theme_t *ti = fx_theme_get((fx_theme_id_t)i);
                        bool selected = (i == (int)s_theme);
                        if (ImGui::Selectable(ti->name, selected)) {
                            apply_theme((fx_theme_id_t)i);
                            s_session_cfg.theme_id = i;
                            FX_INFO("Theme changed: %s", ti->name);
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", ti->description);
                    }
                    ImGui::EndPopup();
                }
            }

            ImGui::SameLine(0, 6);

            /* Audio settings gear — last toolbar item before window controls */
            push_toolbar_button_colors();
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
            if (ImGui::Button("Settings", ImVec2(80.0f, 32.0f))) {
                ImGui::OpenPopup("audio_settings_popup");
            }
            ImGui::PopStyleVar();
            pop_toolbar_button_colors();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Audio device and buffer settings");

            /* Audio settings popup — fixed width so the X button can anchor
             * to the right edge without auto-size feedback. */
            ImGui::SetNextWindowSizeConstraints(ImVec2(370.0f, 0.0f),
                                                ImVec2(370.0f, FLT_MAX));
            if (ImGui::BeginPopup("audio_settings_popup")) {
                const float popup_w = 370.0f;
                const float btn_w   = 22.0f;
                ImGui::Text("Audio Settings");
                /* Close (X) button in top-right — positioned absolutely so the
                 * popup width does not depend on content-region math. */
                ImGui::SameLine();
                ImGui::SetCursorPosX(popup_w - btn_w - ImGui::GetStyle().WindowPadding.x);
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
                if (ImGui::Button("X##audio_close", ImVec2(btn_w, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor(3);
                ImGui::Separator();

                ImGui::TextDisabled("Input Device (Guitar):");
                struct InGetter {
                    static bool get(void *, int idx, const char **out) {
                        const char *n = fx_audio_get_device_name(idx);
                        if (!n) return false;
                        *out = n; return true;
                    }
                };
                ImGui::SetNextItemWidth(300);
                if (ImGui::Combo("##input", &s_selected_input,
                                 InGetter::get, nullptr, num_input_devices)) {
                }

                ImGui::Spacing();

                ImGui::TextDisabled("Output Device (Speakers/Headphones):");
                struct OutGetter {
                    static bool get(void *, int idx, const char **out) {
                        const char *n = fx_audio_get_output_name(idx);
                        if (!n) return false;
                        *out = n; return true;
                    }
                };
                ImGui::SetNextItemWidth(300);
                if (ImGui::Combo("##output", &s_selected_output,
                                 OutGetter::get, nullptr, num_output_devices)) {
                    fx_audio_set_output(s_selected_output);
                }

                ImGui::Spacing();

                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Buffer");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                if (ImGui::Combo("##buffer", &s_selected_buf_idx, buf_labels, 5)) {
                    fx_audio_set_buffer_size(engine, buf_sizes[s_selected_buf_idx]);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Rate");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                if (ImGui::Combo("##rate", &s_selected_sr_idx, sr_labels, 2)) {
                    fx_audio_set_sample_rate(engine, (float)sr_values[s_selected_sr_idx]);
                }

                /* ── Input Gain Trim ──────────────────────────── */
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Input Gain Trim:");
                ImGui::SetNextItemWidth(220);
                if (ImGui::SliderFloat("##input_gain", &s_input_gain_db, -24.0f, 12.0f, "%.1f dB")) {
                    fx_audio_set_input_gain_db(s_input_gain_db);
                    s_session_cfg.input_gain_db = s_input_gain_db;
                }
                if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
                    s_input_gain_db += ImGui::GetIO().MouseWheel * 0.5f;
                    if (s_input_gain_db < -24.0f) s_input_gain_db = -24.0f;
                    if (s_input_gain_db >  12.0f) s_input_gain_db =  12.0f;
                    fx_audio_set_input_gain_db(s_input_gain_db);
                    s_session_cfg.input_gain_db = s_input_gain_db;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    s_input_gain_db = 0.0f;
                    fx_audio_set_input_gain_db(s_input_gain_db);
                    s_session_cfg.input_gain_db = s_input_gain_db;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Pre-chain input gain trim: -24 to +12 dB\n"
                                      "Use to compensate for hot/weak capture sources.\n"
                                      "Stacks with the -20 dB pad.\n"
                                      "Scroll to adjust (0.5 dB/notch). Right-click to reset to 0 dB.");
                ImGui::SameLine();
                /* Pad button: highlight amber when active */
                if (s_input_pad) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.65f, 0.42f, 0.05f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.55f, 0.10f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.50f, 0.32f, 0.03f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.00f, 0.90f, 0.50f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.16f, 0.13f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.24f, 0.18f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.12f, 0.10f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.60f, 0.55f, 0.45f, 1.0f));
                }
                if (ImGui::Button("-20 dB PAD", ImVec2(90, 0))) {
                    s_input_pad = !s_input_pad;
                    fx_audio_set_input_pad(s_input_pad);
                    s_session_cfg.input_pad = s_input_pad;
                }
                ImGui::PopStyleColor(4);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Fixed -20 dB pad — attenuates the input by 20 dB.\n"
                                      "Use for hot mic arrays or line-level sources with\n"
                                      "too much headroom. Independent of the gain slider\n"
                                      "(total gain = slider + pad).");
                {
                    float total_db = s_input_gain_db + (s_input_pad ? -20.0f : 0.0f);
                    ImGui::SameLine();
                    ImGui::TextDisabled("= %.1f dB", total_db);
                }

                ImGui::Spacing();
                ImGui::Separator();

                if (!s_audio_active) {
                    if (s_selected_input >= 0) {
                        /* Auto-start monitoring if not already open */
                        if (!s_monitor_only) {
                            if (s_selected_output >= 0) fx_audio_set_output(s_selected_output);
                            if (fx_audio_set_device(engine, s_selected_input)) {
                                fx_audio_set_mute_output(true);
                                s_monitor_only = true;
                                FX_INFO("Monitoring input: %s", fx_audio_get_device_name(s_selected_input));
                            }
                        }
                        if (ImGui::Button("Start Audio (Go LIVE)", ImVec2(200, 30))) {
                            fx_audio_set_mute_output(false);
                            s_audio_active = true;
                            s_monitor_only = false;
                            FX_INFO("Audio started: in=%s out=%s",
                                fx_audio_get_device_name(s_selected_input),
                                s_selected_output >= 0 ? fx_audio_get_output_name(s_selected_output) : "(default)");
                        }
                    } else {
                        ImGui::TextDisabled("Select an input device first");
                    }
                } else {
                    if (ImGui::Button("Stop Audio", ImVec2(200, 30))) {
                        fx_audio_shutdown();
                        fx_audio_init();
                        num_input_devices = fx_audio_get_device_count();
                        num_output_devices = fx_audio_get_output_count();
                        s_audio_active = false;
                        FX_INFO("Audio stopped");
                    }
                }

                /* ── MIDI Settings ─────────────────────────────── */
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("MIDI Settings");
                ImGui::Separator();

                ImGui::TextDisabled("MIDI Input Device:");
                struct MidiGetter {
                    static bool get(void *, int idx, const char **out) {
                        const char *n = fx_midi_get_device_name(idx);
                        if (!n) return false;
                        *out = n; return true;
                    }
                };
                ImGui::SetNextItemWidth(300);
                if (num_midi_devices > 0) {
                    if (ImGui::Combo("##midi_input", &s_selected_midi,
                                     MidiGetter::get, nullptr, num_midi_devices)) {
                        /* Selection changed — open new device */
                        if (s_selected_midi >= 0) {
                            if (fx_midi_open(s_selected_midi)) {
                                s_midi_active = true;
                                FX_INFO("MIDI opened: %s",
                                    fx_midi_get_device_name(s_selected_midi));
                            } else {
                                s_midi_active = false;
                                FX_ERROR("Failed to open MIDI device");
                            }
                        }
                    }
                } else {
                    ImGui::TextDisabled("No MIDI devices found");
                }

                if (s_midi_active) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Active");
                }

                /* MIDI Learn */
                ImGui::Spacing();
                if (fx_midi_learn_active()) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
                    if (ImGui::Button("Cancel MIDI Learn", ImVec2(200, 28))) {
                        fx_midi_learn_cancel();
                    }
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                       "Move a CC knob on your controller...");
                } else {
                    if (ImGui::Button("MIDI Learn", ImVec2(200, 28))) {
                        /* Map next CC to param 0 — GUI can set specific target */
                        fx_midi_learn_start(0);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click, then move a CC knob to map it");
                }

                /* CC Mapping table */
                ImGui::Spacing();
                ImGui::TextDisabled("CC Mappings:");
                {
                    bool has_mappings = false;
                    for (int cc = 0; cc < 128; cc++) {
                        int param = fx_midi_get_mapped_param(cc);
                        if (param >= 0) {
                            has_mappings = true;
                            ImGui::Text("  CC %3d -> Param %d", cc, param);
                            ImGui::SameLine();
                            char unmap_id[32];
                            snprintf(unmap_id, sizeof(unmap_id), "X##cc%d", cc);
                            if (ImGui::SmallButton(unmap_id)) {
                                fx_midi_unmap_cc(cc);
                            }
                        }
                    }
                    if (!has_mappings) {
                        ImGui::TextDisabled("  (none)");
                    }
                }

                ImGui::EndPopup();
            }

            /* ── Window controls: ? _ [] X (borderless) ───────── */
            {
                ImVec2 wc_sz(35.0f, 28.0f);
                float controls_w = wc_sz.x * 4 + 6 + 8;
                ImGui::SameLine(ImGui::GetWindowWidth() - controls_w);

                /* Help button — opens a popup listing all keybinds. */
                push_toolbar_button_colors();
                if (ImGui::Button("?##whelp", wc_sz))
                    ImGui::OpenPopup("app_keybinds");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Keyboard shortcuts");
                pop_toolbar_button_colors();
                ImGui::SetNextWindowSizeConstraints(ImVec2(460.0f, 0.0f),
                                                    ImVec2(460.0f, FLT_MAX));
                if (ImGui::BeginPopup("app_keybinds")) {
                    const float kb_popup_w = 460.0f;
                    const float kb_btn_w   = 22.0f;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.78f, 0.6f, 1.0f));
                    ImGui::Text("Keyboard shortcuts");
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(kb_popup_w - kb_btn_w - ImGui::GetStyle().WindowPadding.x);
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
                    if (ImGui::Button("X##keybinds_close", ImVec2(kb_btn_w, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor(3);
                    ImGui::Separator();
                    ImGui::TextDisabled("Global");
                    ImGui::BulletText("Space          LIVE on/off (or tap focused loop slot)");
                    ImGui::BulletText("Ctrl + S       save preset");
                    ImGui::BulletText("Ctrl + Shift + S   save preset as…");
                    ImGui::BulletText("Ctrl + T       cycle theme");
                    ImGui::BulletText("Esc            quit");
                    ImGui::Separator();
                    ImGui::TextDisabled("Looper (panel open)");
                    ImGui::BulletText("1 – 9          tap slot (rec → play → overdub)");
                    ImGui::BulletText("Shift + 1-9    mute / unmute slot");
                    ImGui::BulletText("Alt + 1-9      clear slot");
                    ImGui::BulletText("Space          tap the focused slot");
                    ImGui::BulletText("R              arm next empty slot");
                    ImGui::BulletText("Tab            cycle focused slot");
                    ImGui::BulletText("Ctrl + Z       undo last overdub on focused");
                    ImGui::Separator();
                    ImGui::TextDisabled("Clicking a looper pad also selects it for Space.");

                    /* File locations — rebuilt per-open so s_rec_dir reflects
                     * any edits in the recording save-location modal. */
                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.78f, 0.6f, 1.0f));
                    ImGui::Text("File locations");
                    ImGui::PopStyleColor();
#ifdef _WIN32
                    const char *platform_note = "Windows: %APPDATA%\\0xFX";
#elif defined(__APPLE__)
                    const char *platform_note = "macOS: ~/.0xfx";
#else
                    const char *platform_note = "Linux: ~/.0xfx";
#endif
                    ImGui::TextDisabled("%s", platform_note);
                    ImGui::BulletText("Config:      %s", get_config_path());
                    ImGui::BulletText("Custom cabs: %s", custom_cabs_path());
                    ImGui::BulletText("Recordings:  %s",
                        s_rec_dir[0] ? s_rec_dir : "(not set — open with the gear icon)");
                    ImGui::BulletText("Presets:     presets/ (relative to app)");
                    ImGui::BulletText("Session:     presets/last_session.0xfx");
                    ImGui::EndPopup();
                }
                ImGui::SameLine(0, 2);

                push_toolbar_button_colors();
                if (ImGui::Button("_##wmin", wc_sz)) {
#ifdef _WIN32
                    SDL_SysWMinfo wmInfo; SDL_VERSION(&wmInfo.version);
                    if (SDL_GetWindowWMInfo(window, &wmInfo))
                        ShowWindow(wmInfo.info.win.window, SW_MINIMIZE);
#else
                    SDL_MinimizeWindow(window);
#endif
                }
                pop_toolbar_button_colors();
                ImGui::SameLine(0, 2);

                push_toolbar_button_colors();
                {
                    bool maximized = false;
#ifdef _WIN32
                    SDL_SysWMinfo wmI; SDL_VERSION(&wmI.version);
                    if (SDL_GetWindowWMInfo(window, &wmI))
                        maximized = IsZoomed(wmI.info.win.window) != 0;
#else
                    maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
#endif
                    if (ImGui::Button(maximized ? "[]##wmax" : "[ ]##wmax", wc_sz)) {
#ifdef _WIN32
                        SDL_SysWMinfo wmI2; SDL_VERSION(&wmI2.version);
                        if (SDL_GetWindowWMInfo(window, &wmI2))
                            ShowWindow(wmI2.info.win.window, maximized ? SW_RESTORE : SW_MAXIMIZE);
#else
                        if (maximized) SDL_RestoreWindow(window);
                        else SDL_MaximizeWindow(window);
#endif
                    }
                }
                pop_toolbar_button_colors();
                ImGui::SameLine(0, 2);

                /* Close — theme frame idle, hot red on hover (universal close). */
                {
                    const fx_theme_t *thc = fx_theme_get(s_theme);
                    ImGui::PushStyleColor(ImGuiCol_Button,        thc->frame);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.25f, 0.25f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text,          thc->text);
                }
                if (ImGui::Button("X##wclose", wc_sz)) {
                    running = false;
                }
                ImGui::PopStyleColor(4);
            }

            /* ── Window drag (click empty toolbar area to move window) ── */
            {
                static bool dragging_window = false;
                static int drag_start_wx, drag_start_wy, drag_start_mx, drag_start_my;

                bool in_toolbar = (io.MousePos.y < TOOLBAR_H);
                bool over_widget = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();

                if (io.MouseDoubleClicked[0] && in_toolbar && !over_widget) {
#ifdef _WIN32
                    SDL_SysWMinfo wmI; SDL_VERSION(&wmI.version);
                    if (SDL_GetWindowWMInfo(window, &wmI)) {
                        bool is_max = IsZoomed(wmI.info.win.window) != 0;
                        ShowWindow(wmI.info.win.window, is_max ? SW_RESTORE : SW_MAXIMIZE);
                    }
#else
                    bool is_max = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
                    if (is_max) SDL_RestoreWindow(window); else SDL_MaximizeWindow(window);
#endif
                }

                if (io.MouseClicked[0] && in_toolbar && !over_widget) {
                    dragging_window = true;
                    SDL_GetWindowPosition(window, &drag_start_wx, &drag_start_wy);
                    SDL_GetGlobalMouseState(&drag_start_mx, &drag_start_my);
                }
                if (dragging_window) {
                    if (io.MouseDown[0]) {
                        int mx, my;
                        SDL_GetGlobalMouseState(&mx, &my);
                        SDL_SetWindowPosition(window, drag_start_wx + mx - drag_start_mx,
                                                       drag_start_wy + my - drag_start_my);
                    } else {
                        dragging_window = false;
                    }
                }
            }

            /* Publish toolbar widget-hover state for the SDL hit-test so the
             * OS-level window drag kicks in only when the pointer is over
             * empty toolbar space, not a button or dropdown. */
            g_toolbar_pointer_on_widget =
                ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();

            ImGui::End();
        }

        /* ── Keyboard shortcuts ──────────────────────────────────── */
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            running = false;
        }
        /* Space: tap focused looper slot when panel is open (start/stop rec,
         * then overdub), otherwise LIVE toggle. */
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
            FX_INFO("GUI: Space pressed (looper_open=%d focused=%d want_cap_kbd=%d)",
                    s_looper_panel_open ? 1 : 0,
                    s_looper_panel_open ? fx_looper_focused(engine) : -1,
                    ImGui::GetIO().WantCaptureKeyboard ? 1 : 0);
        }
        /* Space in the looper panel always taps the focused slot — ImGui sets
         * WantCaptureKeyboard after a pad click (nav focus), but we explicitly
         * want Space to drive the looper regardless. The no-text-input check
         * is still needed so Space doesn't fire while typing in an input. */
        bool typing = ImGui::GetIO().WantTextInput;
        if (s_looper_panel_open && !typing
            && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            int f = fx_looper_focused(engine);
            FX_INFO("looper GUI: Space tap focused=%d", f);
            fx_looper_slot_tap(engine, f);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Space) && !ImGui::GetIO().WantCaptureKeyboard) {
            if (s_audio_active) {
                fx_audio_shutdown(); fx_audio_init();
                num_input_devices  = fx_audio_get_device_count();
                num_output_devices = fx_audio_get_output_count();
                s_audio_active = false;
                FX_INFO("Audio stopped via Space");
            } else {
                if (s_selected_input < 0 && num_input_devices > 0) s_selected_input = 0;
                if (s_selected_output < 0 && num_output_devices > 0) s_selected_output = 0;
                if (s_selected_input >= 0) {
                    if (s_selected_output >= 0) fx_audio_set_output(s_selected_output);
                    if (fx_audio_set_device(engine, s_selected_input)) {
                        s_audio_active = true;
                        FX_INFO("LIVE on via Space");
                    }
                }
            }
        }

        /* Looper key handler (Space above handles the focused-slot tap).
         * Gated on WantTextInput — not WantCaptureKeyboard — because a
         * click-focused pad sets WantCaptureKeyboard but must not swallow
         * 1-9 / R / Tab / Alt+N / Shift+N / Ctrl+Z. */
        if (s_looper_panel_open && !ImGui::GetIO().WantTextInput) {
            looper_handle_keys(engine);
        }
        /* Ctrl+T = cycle themes */
        if (ImGui::GetIO().KeyCtrl
            && ImGui::IsKeyPressed(ImGuiKey_T, false)
            && !ImGui::GetIO().WantTextInput) {
            fx_theme_id_t next = (fx_theme_id_t)(((int)s_theme + 1) % (int)FX_THEME_COUNT);
            apply_theme(next);
            s_session_cfg.theme_id = (int)next;
            FX_INFO("Theme cycled: %s", fx_theme_name(next));
        }

        /* Ctrl+S = quick-save preset */
        {
            bool ctrl = ImGui::GetIO().KeyCtrl;
            bool shift = ImGui::GetIO().KeyShift;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
                if (shift) {
                    /* Ctrl+Shift+S = Save As */
                    strcpy(s_save_as_name, "");
                    s_save_as_open = true;
                    ImGui::OpenPopup("save_as_popup");
                } else {
                    /* Ctrl+S = save current preset, or Save As if untitled */
                    bool has_name = (s_preset_name[0] != '\0'
                        && strcmp(s_preset_name, "Untitled") != 0
                        && strcmp(s_preset_name, "Last Session") != 0);
                    if (has_name) {
                        char path[400];
                        snprintf(path, sizeof(path), "presets/%s.0xfx", s_preset_name);
                        bool ok = fx_preset_save(engine, path);
                        if (!ok) {
                            snprintf(path, sizeof(path), "../presets/%s.0xfx", s_preset_name);
                            ok = fx_preset_save(engine, path);
                        }
                        if (ok) { s_preset_modified = false; s_browser_needs_scan = true; }
                        FX_INFO(ok ? "Saved: %s" : "Save failed: %s", s_preset_name);
                    } else {
                        s_save_as_open = true;
                    }
                    fx_preset_save(engine, "presets/last_session.0xfx");
                }
            }
        }
        /* Save As popup (Ctrl+Shift+S) */
        if (s_save_as_open) {
            ImGui::OpenPopup("save_as_popup");
        }
        if (ImGui::BeginPopupModal("save_as_popup", &s_save_as_open,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Save Preset As");
            ImGui::Separator();
            ImGui::SetNextItemWidth(280);
            bool enter_pressed = ImGui::InputText("Preset Name", s_save_as_name,
                                                  sizeof(s_save_as_name),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();
            if ((ImGui::Button("Save", ImVec2(120, 0)) || enter_pressed) &&
                s_save_as_name[0] != '\0') {
                char path[400];
                snprintf(path, sizeof(path), "presets/%s.0xfx", s_save_as_name);
                bool ok = fx_preset_save(engine, path);
                if (!ok) {
                    snprintf(path, sizeof(path), "../presets/%s.0xfx", s_save_as_name);
                    ok = fx_preset_save(engine, path);
                }
                if (ok) {
                    s_browser_needs_scan = true;
                    /* Resolve to absolute path for display */
                    char abs_path[512] = "";
                    #ifdef _WIN32
                    _fullpath(abs_path, path, sizeof(abs_path));
                    #else
                    realpath(path, abs_path);
                    #endif
                    if (abs_path[0])
                        snprintf(s_save_toast, sizeof(s_save_toast), "Saved to: %s", abs_path);
                    else
                        snprintf(s_save_toast, sizeof(s_save_toast), "Saved to: %s", path);
                    s_save_toast_timer = 5.0f;
                } else {
                    snprintf(s_save_toast, sizeof(s_save_toast), "Save failed: %s", s_save_as_name);
                    s_save_toast_timer = 4.0f;
                }
                FX_INFO(ok ? "Saved preset: %s" : "Save failed: %s", s_save_as_name);
                s_save_as_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                s_save_as_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        /* Save toast notification */
        if (s_save_toast_timer > 0.0f) {
            s_save_toast_timer -= io.DeltaTime;
            float alpha = s_save_toast_timer > 1.0f ? 1.0f : s_save_toast_timer;
            if (alpha > 0.0f) {
                ImVec2 toast_sz = ImGui::CalcTextSize(s_save_toast);
                float toast_x = (win_w - toast_sz.x - 20.0f) * 0.5f;
                float toast_y = win_h - 60.0f;
                ImDrawList *dl = ImGui::GetForegroundDrawList();
                dl->AddRectFilled(
                    ImVec2(toast_x - 10.0f, toast_y - 6.0f),
                    ImVec2(toast_x + toast_sz.x + 10.0f, toast_y + toast_sz.y + 6.0f),
                    IM_COL32(30, 28, 22, (int)(220 * alpha)), 6.0f);
                dl->AddRect(
                    ImVec2(toast_x - 10.0f, toast_y - 6.0f),
                    ImVec2(toast_x + toast_sz.x + 10.0f, toast_y + toast_sz.y + 6.0f),
                    IM_COL32(180, 140, 40, (int)(160 * alpha)), 6.0f);
                dl->AddText(ImVec2(toast_x, toast_y),
                    IM_COL32(220, 200, 160, (int)(255 * alpha)), s_save_toast);
            }
        }

        /* ============================================================
         * BUILD THE FLATTENED SIGNAL CHAIN
         *
         * SINGLE: INPUT -> [pre pedals] -> AMP -> CAB -> [post pedals] -> OUTPUT
         * DUAL:   INPUT -> [pre pedals] -> SPLIT -> AMP A -> CAB A -> MERGE
         *                                        -> AMP B -> CAB B ->
         *                               -> [post pedals] -> OUTPUT
         *
         * In DUAL mode the chain array stores nodes sequentially; the
         * special SPLIT/MERGE nodes encode where paths diverge/converge.
         * The second path's nodes carry chain_id=s_chain_b so the
         * renderer can lay them out on a separate vertical lane.
         * ============================================================ */
        ChainNode chain[256];
        int chain_len = 0;
        bool is_dual = (s_chain_b >= 0);

        /* INPUT */
        chain[chain_len++] = { NODE_INPUT, -1, -1, 0 };

        /* Pre-amp pedals */
        for (int i = 0; i < s_pre_id_count && chain_len < 250; i++) {
            chain[chain_len++] = { NODE_PEDAL_PRE, i, s_pre_ids[i], 0 };
        }

        if (is_dual) {
            /* SPLIT diamond */
            chain[chain_len++] = { NODE_SPLIT, -1, -1, 0 };

            /* Chain A: AMP A + CAB A (chain 0 = top lane) */
            chain[chain_len++] = { NODE_AMP, -1, -1, 0 };
            chain[chain_len++] = { NODE_CAB, -1, -1, 0 };

            /* Chain B: AMP B + CAB B (s_chain_b = bottom lane) */
            chain[chain_len++] = { NODE_AMP, -1, -1, (int)s_chain_b };
            chain[chain_len++] = { NODE_CAB, -1, -1, (int)s_chain_b };

            /* MERGE (mix) diamond */
            chain[chain_len++] = { NODE_MERGE, -1, -1, 0 };
        } else {
            /* Single path AMP + CAB */
            chain[chain_len++] = { NODE_AMP, -1, -1, 0 };
            chain[chain_len++] = { NODE_CAB, -1, -1, 0 };
        }

        /* Post-amp pedals */
        for (int i = 0; i < s_post_id_count && chain_len < 254; i++) {
            chain[chain_len++] = { NODE_PEDAL_POST, i, s_post_ids[i], 0 };
        }

        /* Studio processors (post-amp rack gear) */
        for (int i = 0; i < s_studio_id_count && chain_len < 254; i++) {
            chain[chain_len++] = { NODE_STUDIO, i, s_studio_ids[i], 0 };
        }

        /* OUTPUT */
        chain[chain_len++] = { NODE_OUTPUT, -1, -1, 0 };

        /* Clamp selected_node */
        if (s_selected_node >= chain_len) s_selected_node = -1;

        /* ============================================================
         * LOOPER PANEL (docked strip between toolbar and signal chain)
         * ============================================================ */
        if (s_looper_panel_open) {
            looper_render_panel(engine, 0.0f, TOOLBAR_H,
                                (float)win_w, LOOPER_H);
        }

        /* ============================================================
         * SIGNAL CHAIN VIEW (~35% of window, below toolbar + looper)
         * ============================================================ */
        {
            float looper_h = s_looper_panel_open ? LOOPER_H : 0.0f;
            float chain_top = TOOLBAR_H + looper_h;
            float chain_area_h = (win_h - chain_top - STATUS_H) * 0.35f;
            ImGui::SetNextWindowPos(ImVec2(0, chain_top));
            ImGui::SetNextWindowSize(ImVec2(win_w, chain_area_h));
            ImGui::Begin("##signal_chain", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_HorizontalScrollbar);

            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 win_pos = ImGui::GetWindowPos();
            ImVec2 content_min = ImGui::GetCursorScreenPos();

            /* Vertical center of the chain area (used as the "main" lane) */
            float cy = win_pos.y + chain_area_h * 0.45f;

            /* In DUAL mode we stack two lanes above/below cy */
            const float LANE_OFFSET = NODE_H * 1.25f;  /* vertical gap between parallel paths */
            float cy_a = is_dual ? cy - LANE_OFFSET * 0.5f : cy;  /* top lane (chain A) */
            float cy_b = is_dual ? cy + LANE_OFFSET * 0.5f : cy;  /* bottom lane (chain B) */

            /* ── Layout: compute x-column assignments ──────────────────
             * SINGLE: each node occupies one column, separated by NODE_SPACING.
             * DUAL:   SPLIT / MERGE columns span both lanes.
             *         Chain-A and chain-B nodes share the same x-column
             *         (stacked vertically).
             *
             * We compute a "column" index for each node, then map to x.
             * ─────────────────────────────────────────────────────────── */
            int node_col[256];  /* which x-column does node ni occupy? */
            int num_cols = 0;
            {
                bool in_split = false;
                int col = 0;
                int amp_a_col = -1;
                int cab_a_col = -1;
                for (int ni = 0; ni < chain_len; ni++) {
                    NodeKind k = chain[ni].kind;
                    if (k == NODE_SPLIT) {
                        node_col[ni] = col++;
                        in_split = true;
                        amp_a_col = -1; cab_a_col = -1;
                    } else if (k == NODE_MERGE) {
                        in_split = false;
                        node_col[ni] = col++;
                    } else if (in_split && k == NODE_AMP) {
                        if (chain[ni].chain_id == 0) {
                            amp_a_col = col;
                            node_col[ni] = col++;
                        } else {
                            /* chain B AMP shares column with chain A AMP */
                            node_col[ni] = (amp_a_col >= 0) ? amp_a_col : col;
                        }
                    } else if (in_split && k == NODE_CAB) {
                        if (chain[ni].chain_id == 0) {
                            cab_a_col = col;
                            node_col[ni] = col++;
                        } else {
                            /* chain B CAB shares column with chain A CAB */
                            node_col[ni] = (cab_a_col >= 0) ? cab_a_col : col;
                        }
                    } else {
                        node_col[ni] = col++;
                    }
                }
                num_cols = col;
            }

            /* Total width for scrolling */
            float total_w = num_cols * NODE_W + (num_cols - 1) * NODE_SPACING
                          + CHAIN_PADDING * 2.0f;

            /* Reserve scrollable content area (extra height for two lanes in dual mode) */
            float content_h = is_dual ? chain_area_h * 0.85f : (chain_area_h - 20.0f);
            ImGui::Dummy(ImVec2(total_w, content_h));

            /* Track where the [+] button popup should insert */
            static fx_chain_pos_t s_add_popup_pos = FX_CHAIN_POS_PRE;
            static int s_add_popup_insert_slot = -1;

            /* Center the chain horizontally */
            float chain_area_w = win_w;
            float chain_content_w = num_cols * NODE_W + (num_cols - 1) * NODE_SPACING;
            float center_offset = (chain_area_w > chain_content_w)
                ? (chain_area_w - chain_content_w) * 0.5f
                : CHAIN_PADDING;

            /* Map column index -> screen x (left edge of node) */
            auto col_to_x = [&](int col) -> float {
                return content_min.x + center_offset - ImGui::GetScrollX()
                     + col * (NODE_W + NODE_SPACING);
            };

            /* ── Draw SPLIT / MERGE bezier paths first (behind nodes) ── */
            if (is_dual) {
                /* Find the SPLIT and MERGE node columns */
                int split_col = -1, merge_col = -1;
                int amp_col_a = -1, cab_col_a = -1, cab_col_b = -1;
                for (int ni = 0; ni < chain_len; ni++) {
                    if (chain[ni].kind == NODE_SPLIT) split_col = node_col[ni];
                    if (chain[ni].kind == NODE_MERGE) merge_col = node_col[ni];
                    if (chain[ni].kind == NODE_AMP && chain[ni].chain_id == 0) amp_col_a = node_col[ni];
                    if (chain[ni].kind == NODE_CAB && chain[ni].chain_id == 0) cab_col_a = node_col[ni];
                    if (chain[ni].kind == NODE_CAB && chain[ni].chain_id != 0) cab_col_b = node_col[ni];
                }
                if (split_col >= 0 && merge_col >= 0 && amp_col_a >= 0) {
                    float sx   = col_to_x(split_col) + NODE_W;   /* right of SPLIT */
                    float mx   = col_to_x(merge_col);             /* left of MERGE  */
                    float ax   = col_to_x(amp_col_a);             /* left of AMP column */
                    float cp   = 40.0f;

                    ImU32 cable_col = s_audio_active
                        ? IM_COL32(210, 150, 30, 200)
                        : IM_COL32(110, 85, 30, 160);
                    ImU32 cable_shd = IM_COL32(20, 15, 5, 100);

                    /* SPLIT -> AMP A (upper path) */
                    {
                        ImVec2 p0(sx, cy), p3(ax, cy_a + NODE_H * 0.5f);
                        ImVec2 p1(sx + cp, cy), p2(ax - cp, cy_a + NODE_H * 0.5f);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 16);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 16);
                    }
                    /* SPLIT -> AMP B (lower path) */
                    {
                        ImVec2 p0(sx, cy), p3(ax, cy_b + NODE_H * 0.5f);
                        ImVec2 p1(sx + cp, cy), p2(ax - cp, cy_b + NODE_H * 0.5f);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 16);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 16);
                    }
                    /* AMP A -> CAB A */
                    if (cab_col_a >= 0) {
                        float a_rx = col_to_x(amp_col_a) + NODE_W;
                        float c_lx = col_to_x(cab_col_a);
                        float acy  = cy_a + NODE_H * 0.5f;
                        ImVec2 p0(a_rx, acy), p3(c_lx, acy);
                        ImVec2 p1(a_rx + 20, acy), p2(c_lx - 20, acy);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 12);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 12);
                    }
                    /* AMP B -> CAB B */
                    if (cab_col_b >= 0) {
                        float a_rx = col_to_x(amp_col_a) + NODE_W;
                        float c_lx = col_to_x(cab_col_b);
                        float bcy  = cy_b + NODE_H * 0.5f;
                        ImVec2 p0(a_rx, bcy), p3(c_lx, bcy);
                        ImVec2 p1(a_rx + 20, bcy), p2(c_lx - 20, bcy);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 12);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 12);
                    }
                    /* CAB A -> MERGE */
                    if (cab_col_a >= 0) {
                        float c_rx = col_to_x(cab_col_a) + NODE_W;
                        float acy  = cy_a + NODE_H * 0.5f;
                        ImVec2 p0(c_rx, acy), p3(mx, cy);
                        ImVec2 p1(c_rx + cp, acy), p2(mx - cp, cy);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 16);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 16);
                    }
                    /* CAB B -> MERGE */
                    if (cab_col_b >= 0) {
                        float c_rx = col_to_x(cab_col_b) + NODE_W;
                        float bcy  = cy_b + NODE_H * 0.5f;
                        ImVec2 p0(c_rx, bcy), p3(mx, cy);
                        ImVec2 p1(c_rx + cp, bcy), p2(mx - cp, cy);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 16);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 16);
                    }
                }
            }

            /* ── Section labels above the signal chain ──────────────── */
            {
                ImGui::SetWindowFontScale(0.75f);
                ImU32 label_col = IM_COL32(160, 140, 110, 120);
                float label_y = cy - NODE_H * 0.5f - 18.0f;
                if (is_dual) label_y = cy_a - NODE_H * 0.5f - 18.0f;

                /* Find first/last column for each section */
                for (int ni = 0; ni < chain_len; ni++) {
                    const char *section = nullptr;
                    if (chain[ni].kind == NODE_PEDAL_PRE && (ni == 0 || chain[ni-1].kind != NODE_PEDAL_PRE))
                        section = "PEDALS";
                    else if (chain[ni].kind == NODE_AMP && (ni == 0 || (chain[ni-1].kind != NODE_AMP && chain[ni-1].kind != NODE_SPLIT)))
                        section = "AMP";
                    else if (chain[ni].kind == NODE_CAB && (ni == 0 || chain[ni-1].kind != NODE_CAB))
                        section = "CABINET";
                    else if (chain[ni].kind == NODE_STUDIO && (ni == 0 || chain[ni-1].kind != NODE_STUDIO))
                        section = "RACK FX";

                    if (section) {
                        float nx = col_to_x(node_col[ni]);
                        ImVec2 sz = ImGui::CalcTextSize(section);
                        dl->AddText(ImVec2(nx + (NODE_W - sz.x) * 0.5f, label_y), label_col, section);
                    }
                }
                ImGui::SetWindowFontScale(1.0f);
            }

            /* ── Draw all nodes ──────────────────────────────────────── */
            for (int ni = 0; ni < chain_len; ni++) {
                ChainNode &n = chain[ni];
                bool is_selected = (s_selected_node == ni);
                bool is_bypassed = false;

                if (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST) {
                    is_bypassed = fx_pedal_get_bypass(engine, n.pedal_id);
                } else if (n.kind == NODE_STUDIO) {
                    is_bypassed = fx_studio_get_bypass(engine, n.pedal_id);
                } else if (n.kind == NODE_CAB) {
                    is_bypassed = fx_cab_get_bypass(engine, (fx_chain_id)n.chain_id);
                }

                /* Determine Y position for this node */
                float node_cy;
                if (is_dual) {
                    if (n.chain_id != 0) {
                        node_cy = cy_b;  /* chain B = bottom lane */
                    } else if (n.kind == NODE_AMP || n.kind == NODE_CAB) {
                        node_cy = cy_a;  /* chain A amp/cab = top lane */
                    } else {
                        node_cy = cy;    /* center lane (pre/post/split/merge/in/out) */
                    }
                } else {
                    node_cy = cy;
                }

                float nx = col_to_x(node_col[ni]);
                float ny = node_cy - NODE_H * 0.5f;

                /* ── SPLIT / MERGE: draw as diamond ─────────────── */
                if (n.kind == NODE_SPLIT || n.kind == NODE_MERGE) {
                    float dm  = NODE_H * 0.55f;  /* half-size of diamond */
                    float dcx = nx + NODE_W * 0.5f;
                    float dcy = node_cy;
                    ImU32 fill_col = IM_COL32(60, 48, 12, 255);
                    ImU32 edge_col = is_selected
                        ? IM_COL32(255, 220, 60, 255)
                        : IM_COL32(220, 170, 30, 255);
                    ImVec2 top(dcx, dcy - dm);
                    ImVec2 rgt(dcx + dm, dcy);
                    ImVec2 bot(dcx, dcy + dm);
                    ImVec2 lft(dcx - dm, dcy);
                    dl->AddQuadFilled(top, rgt, bot, lft, fill_col);
                    dl->AddQuad(top, rgt, bot, lft, edge_col, 2.0f);

                    /* Label inside diamond */
                    const char *dlbl = (n.kind == NODE_SPLIT) ? "Y" : "M";
                    ImVec2 dlbl_sz = ImGui::CalcTextSize(dlbl);
                    dl->AddText(ImVec2(dcx - dlbl_sz.x * 0.5f, dcy - dlbl_sz.y * 0.5f),
                                IM_COL32(220, 200, 100, 255), dlbl);

                    /* Below label */
                    const char *blbl = (n.kind == NODE_SPLIT) ? "SPLIT" : "MIX";
                    ImVec2 blbl_sz = ImGui::CalcTextSize(blbl);
                    dl->AddText(ImVec2(dcx - blbl_sz.x * 0.5f, dcy + dm + 4.0f),
                                IM_COL32(180, 165, 120, 200), blbl);

                    /* Selection glow ring */
                    if (is_selected) {
                        dl->AddQuad(ImVec2(top.x, top.y - 3), ImVec2(rgt.x + 3, rgt.y),
                                    ImVec2(bot.x, bot.y + 3), ImVec2(lft.x - 3, lft.y),
                                    IM_COL32(255, 220, 60, 200), 2.5f);
                    }

                    /* Invisible button for click detection */
                    ImGui::SetCursorScreenPos(ImVec2(dcx - dm, dcy - dm));
                    char btn_id[32];
                    snprintf(btn_id, sizeof(btn_id), "##node_%d", ni);
                    if (ImGui::InvisibleButton(btn_id, ImVec2(dm * 2.0f, dm * 2.0f))) {
                        s_selected_node = (s_selected_node == ni) ? -1 : ni;
                    }
                    /* SPLIT skips cable drawing (handled by bezier pass),
                     * but MERGE falls through to draw cable + [+] to next node */
                    if (n.kind == NODE_SPLIT) continue;
                }

                /* ── Regular rectangular node ───────────────────── */
                bool drew_texture = false;
                {
                    uintptr_t tex = 0;
                    if (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST) {
                        fx_pedal_type_t pt = fx_pedal_get_type(engine, n.pedal_id);
                        if (pt < FX_PEDAL_TYPE_COUNT) {
                            const char *tname = fx_pedal_get_type_name(pt);
                            tex = load_pedal_texture(tname);
                        }
                    } else if (n.kind == NODE_AMP) {
                        const char *aname = fx_amp_get_type_name(
                            fx_amp_get_model(engine, (fx_chain_id)n.chain_id));
                        tex = load_amp_body_texture(aname);
                    } else if (n.kind == NODE_CAB) {
                        fx_chain_id cab_cid = (n.chain_id == 0)
                            ? FX_CHAIN_DEFAULT : s_chain_b;
                        const char *cab_custom = (cab_cid >= 0)
                            ? fx_cab_get_custom_ir_path(engine, cab_cid) : NULL;
                        if (cab_custom && cab_custom[0]) {
                            /* Custom IR — leave tex=0 so the procedural
                             * painter below draws a cab scaled to the node. */
                            tex = 0;
                        } else {
                            int ctype = (n.chain_id == 0) ? s_cab_type : s_cab_type_b;
                            tex = load_cab_texture(ctype);
                        }
                    } else if (n.kind == NODE_STUDIO) {
                        static const char *rack_fnames[] = {
                            "iron_squeeze", "glass_eq", "reel_warmth", "brick_wall",
                            "velvet_press", "glue_bus", "valve_color", "precision_eq", "room_engine"
                        };
                        fx_studio_type_t st = fx_studio_get_type(engine, n.pedal_id);
                        if (st >= 0 && st < FX_STUDIO_COUNT) {
                            char spath[256];
                            snprintf(spath, sizeof(spath), "resources/studio/%s_nobg.png", rack_fnames[st]);
                            tex = fx_texture_load(spath);
                        }
                    } else if (n.kind == NODE_INPUT) {
                        tex = fx_texture_load("resources/cables/trs_plug_input.png");
                        /* Render INPUT flipped horizontally (plug tip faces left) */
                        if (tex) {
                            ImVec4 tint = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                            ImGui::SetCursorScreenPos(ImVec2(nx, ny));
                            ImGui::Image((ImTextureID)tex, ImVec2(NODE_W, NODE_H),
                                         ImVec2(1, 0), ImVec2(0, 1), tint);
                            drew_texture = true;
                            tex = 0; /* skip default rendering below */
                        }
                    } else if (n.kind == NODE_OUTPUT) {
                        tex = fx_texture_load("resources/cables/xlr_plug_output.png");
                    }
                    if (tex) {
                        ImVec4 tint = is_bypassed
                            ? ImVec4(0.5f, 0.5f, 0.5f, 0.7f)
                            : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                        ImGui::SetCursorScreenPos(ImVec2(nx, ny));
                        ImGui::Image((ImTextureID)tex, ImVec2(NODE_W, NODE_H),
                                     ImVec2(0, 0), ImVec2(1, 1), tint);
                        drew_texture = true;
                    }
                }
                if (!drew_texture) {
                    if (n.kind == NODE_CAB) {
                        /* Procedural cab for custom IRs — seed by IR path so
                         * the same file always renders the same visual. */
                        fx_chain_id cab_cid2 = (n.chain_id == 0)
                            ? FX_CHAIN_DEFAULT : s_chain_b;
                        const char *ir_path = (cab_cid2 >= 0)
                            ? fx_cab_get_custom_ir_path(engine, cab_cid2) : NULL;
                        unsigned seed = 1;
                        if (ir_path) for (const char *s = ir_path; *s; s++)
                            seed = seed * 131u + (unsigned)*s;
                        draw_procedural_cab(dl, ImVec2(nx, ny),
                                            ImVec2(nx + NODE_W, ny + NODE_H), seed);
                    } else {
                        ImU32 bg_col = node_color(n.kind, is_bypassed);
                        dl->AddRectFilled(ImVec2(nx, ny),
                                          ImVec2(nx + NODE_W, ny + NODE_H),
                                          bg_col, 6.0f);
                    }
                }

                /* Selection highlight */
                if (is_selected) {
                    dl->AddRect(ImVec2(nx - 2, ny - 2),
                                ImVec2(nx + NODE_W + 2, ny + NODE_H + 2),
                                IM_COL32(230, 180, 60, 255), 6.0f, 0, 2.5f);
                }

                /* Node label (centered below node) */
                const char *lbl = node_label(n.kind, engine, n.pedal_id, n.chain_id);
                /* Override cab label with actual cab type name (or custom IR
                 * name when the user has loaded their own). */
                if (n.kind == NODE_CAB) {
                    fx_chain_id cid = (n.chain_id == 0) ? FX_CHAIN_DEFAULT : s_chain_b;
                    const char *custom_ir = (cid >= 0)
                        ? fx_cab_get_custom_ir_path(engine, cid) : NULL;
                    if (custom_ir && custom_ir[0]) {
                        const char *cname = fx_cab_get_custom_name(engine, cid);
                        lbl = (cname && cname[0]) ? cname : "Custom";
                    } else {
                        int ctype = (n.chain_id == 0) ? s_cab_type : s_cab_type_b;
                        if (ctype >= 0 && ctype < FX_CAB_TYPE_COUNT)
                            lbl = s_cab_type_names[ctype];
                    }
                }
                char short_lbl[16];
                ImVec2 lbl_size = ImGui::CalcTextSize(lbl);
                if (lbl_size.x > NODE_W - 4.0f) {
                    int copy_len = 9;
                    if (copy_len > (int)strlen(lbl)) copy_len = (int)strlen(lbl);
                    memcpy(short_lbl, lbl, copy_len);
                    short_lbl[copy_len] = '\0';
                    lbl = short_lbl;
                    lbl_size = ImGui::CalcTextSize(lbl);
                }
                float lbl_x = nx + (NODE_W - lbl_size.x) * 0.5f;
                float lbl_y = ny + NODE_H + 4.0f;
                dl->AddText(ImVec2(lbl_x, lbl_y),
                            is_bypassed ? IM_COL32(100, 90, 80, 200)
                                        : IM_COL32(210, 200, 180, 255),
                            lbl);

                /* Bypass LED */
                if (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST) {
                    const char *led_path = is_bypassed
                        ? "resources/leds/led_red_off_nobg.png"
                        : "resources/leds/led_green_on_nobg.png";
                    uintptr_t led_tex = fx_texture_load(led_path);
                    const float LED_SZ = 12.0f;
                    float led_x = nx + NODE_W - LED_SZ - 3.0f;
                    float led_y = ny + 3.0f;
                    if (led_tex) {
                        ImGui::SetCursorScreenPos(ImVec2(led_x, led_y));
                        ImGui::Image((ImTextureID)(uintptr_t)led_tex, ImVec2(LED_SZ, LED_SZ));
                    } else {
                        ImU32 dot_col = is_bypassed
                            ? IM_COL32(200, 60, 50, 200)
                            : IM_COL32(60, 200, 60, 220);
                        dl->AddCircleFilled(
                            ImVec2(led_x + LED_SZ*0.5f, led_y + LED_SZ*0.5f),
                            LED_SZ * 0.5f, dot_col, 12);
                    }
                }

                /* Click detection: left-click = select, right-click/double-click = stomp (bypass toggle) */
                {
                    char btn_id[32];
                    snprintf(btn_id, sizeof(btn_id), "##node_%d", ni);
                    ImGui::SetCursorScreenPos(ImVec2(nx, ny));
                    if (ImGui::InvisibleButton(btn_id, ImVec2(NODE_W, NODE_H))) {
                        if (n.kind != NODE_INPUT && n.kind != NODE_OUTPUT) {
                            s_selected_node = (s_selected_node == ni) ? -1 : ni;
                        }
                    }
                    /* Right-click or double-click = stomp footswitch (toggle bypass) */
                    bool stomped = false;
                    if (ImGui::IsItemHovered()) {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                            stomped = true;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            stomped = true;
                    }
                    if (stomped) {
                        if (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST) {
                            bool bp = fx_pedal_get_bypass(engine, n.pedal_id);
                            fx_pedal_set_bypass(engine, n.pedal_id, !bp);
                        } else if (n.kind == NODE_STUDIO) {
                            bool bp = fx_studio_get_bypass(engine, n.pedal_id);
                            fx_studio_set_bypass(engine, n.pedal_id, !bp);
                        } else if (n.kind == NODE_CAB) {
                            bool bp = fx_cab_get_bypass(engine, (fx_chain_id)n.chain_id);
                            fx_cab_set_bypass(engine, (fx_chain_id)n.chain_id, !bp);
                        }
                    }
                    /* Tooltip hint */
                    if (ImGui::IsItemHovered() && (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST || n.kind == NODE_STUDIO)) {
                        ImGui::SetTooltip("Right-click to bypass/activate");
                    }
                }

                /* ── Draw connecting cable to next node (single-lane sections) ── */
                if (ni < chain_len - 1) {
                    NodeKind cur_kind = chain[ni].kind;
                    NodeKind nxt_kind = chain[ni + 1].kind;

                    /* Only draw cables for sequential single-lane sections.
                       Split-section cables are drawn in the bezier pass above. */
                    bool is_linear = false;
                    bool show_add  = false;
                    fx_chain_pos_t add_pos = FX_CHAIN_POS_PRE;
                    int  add_insert_slot   = 0;

                    if (!is_dual) {
                        /* Pre-amp zone */
                        if ((cur_kind == NODE_INPUT || cur_kind == NODE_PEDAL_PRE) &&
                            (nxt_kind == NODE_PEDAL_PRE || nxt_kind == NODE_AMP)) {
                            is_linear = true; show_add = true; add_pos = FX_CHAIN_POS_PRE;
                            for (int j = 0; j <= ni; j++)
                                if (chain[j].kind == NODE_PEDAL_PRE) add_insert_slot++;
                        }
                        /* Post-amp zone — rack effects */
                        else if ((cur_kind == NODE_CAB || cur_kind == NODE_PEDAL_POST ||
                                  cur_kind == NODE_STUDIO) &&
                                 (nxt_kind == NODE_PEDAL_POST || nxt_kind == NODE_STUDIO ||
                                  nxt_kind == NODE_OUTPUT)) {
                            is_linear = true; show_add = true; add_pos = FX_CHAIN_POS_POST;
                            for (int j = 0; j <= ni; j++)
                                if (chain[j].kind == NODE_PEDAL_POST || chain[j].kind == NODE_STUDIO)
                                    add_insert_slot++;
                        }
                        /* AMP -> CAB */
                        else if (cur_kind == NODE_AMP && nxt_kind == NODE_CAB) {
                            is_linear = true;
                        }
                    } else {
                        /* Dual mode: only linear pre/post sections */
                        if ((cur_kind == NODE_INPUT || cur_kind == NODE_PEDAL_PRE) &&
                            (nxt_kind == NODE_PEDAL_PRE || nxt_kind == NODE_SPLIT)) {
                            is_linear = true;
                            if (nxt_kind == NODE_PEDAL_PRE || nxt_kind == NODE_SPLIT) {
                                show_add = true; add_pos = FX_CHAIN_POS_PRE;
                                for (int j = 0; j <= ni; j++)
                                    if (chain[j].kind == NODE_PEDAL_PRE) add_insert_slot++;
                            }
                        } else if ((cur_kind == NODE_MERGE || cur_kind == NODE_PEDAL_POST ||
                                    cur_kind == NODE_STUDIO) &&
                                   (nxt_kind == NODE_PEDAL_POST || nxt_kind == NODE_STUDIO ||
                                    nxt_kind == NODE_OUTPUT)) {
                            is_linear = true;
                            show_add = true; add_pos = FX_CHAIN_POS_POST;
                            for (int j = 0; j <= ni; j++)
                                if (chain[j].kind == NODE_PEDAL_POST || chain[j].kind == NODE_STUDIO)
                                    add_insert_slot++;
                        }
                    }

                    if (is_linear) {
                        float x_right = col_to_x(node_col[ni]) + NODE_W;
                        float x_next  = col_to_x(node_col[ni + 1]);
                        float lcy     = node_cy;

                        /* [+] button: register invisible button first (for click detection),
                         * but defer visual drawing until after cable is rendered */
                        float add_x = 0, add_y = 0;
                        bool add_clicked = false, add_hovered = false;
                        if (show_add) {
                            add_x = x_right + (x_next - x_right - ADD_BTN_W) * 0.5f;
                            add_y = lcy - ADD_BTN_W * 0.5f;
                            /* Register invisible button for click detection */
                            char inv_id[32];
                            snprintf(inv_id, sizeof(inv_id), "##addinv_%d", ni);
                            ImGui::SetCursorScreenPos(ImVec2(add_x, add_y));
                            add_clicked = ImGui::InvisibleButton(inv_id, ImVec2(ADD_BTN_W, ADD_BTN_W));
                            add_hovered = ImGui::IsItemHovered();
                            if (add_clicked) {
                                s_add_popup_pos = add_pos;
                                s_add_popup_insert_slot = add_insert_slot;
                                if (add_pos == FX_CHAIN_POS_POST)
                                    ImGui::OpenPopup("add_studio_popup");
                                else
                                    ImGui::OpenPopup("add_pedal_popup");
                            }
                        }

                        /* Cable — realistic patch cable between nodes (drawn BEFORE [+] so [+] is on top) */
                        float cable_x0 = x_right, cable_y0 = lcy;
                        float cable_x1 = x_next,  cable_y1 = lcy;

                        /* INPUT/OUTPUT: adjust endpoints + droop */
                        bool has_droop = false;
                        if (cur_kind == NODE_INPUT) {
                            /* Flipped plug — cable exits from bottom-right where the cable end is */
                            cable_x0 = col_to_x(node_col[ni]) + NODE_W * 0.55f;
                            cable_y0 = lcy + NODE_H * 0.45f;
                            has_droop = true;
                        }
                        if (nxt_kind == NODE_OUTPUT) {
                            cable_x1 = col_to_x(node_col[ni + 1]) + NODE_W * 0.15f;
                            cable_y1 = lcy + NODE_H * 0.15f;
                            has_droop = true;
                        }

                        float span = cable_x1 - cable_x0;
                        ImVec2 p0(cable_x0, cable_y0);
                        ImVec2 p3(cable_x1, cable_y1);
                        ImVec2 p1, p2;

                        if (has_droop) {
                            /* Natural cable sag for instrument/mic cables */
                            float sag = 18.0f + span * 0.12f;
                            p1 = ImVec2(cable_x0 + span * 0.25f, cable_y0 + sag);
                            p2 = ImVec2(cable_x1 - span * 0.25f, cable_y1 + sag);
                        } else {
                            /* Straight patch cable between pedals/amps/cabs */
                            p1 = ImVec2(cable_x0 + span * 0.33f, cable_y0);
                            p2 = ImVec2(cable_x1 - span * 0.33f, cable_y1);
                        }

                        /* Shadow layer */
                        dl->AddBezierCubic(
                            ImVec2(p0.x + 1, p0.y + 2),
                            ImVec2(p1.x + 1, p1.y + 2),
                            ImVec2(p2.x + 1, p2.y + 2),
                            ImVec2(p3.x + 1, p3.y + 2),
                            IM_COL32(0, 0, 0, 120), 7.0f, 24);
                        /* Cable body — thick dark rubber */
                        dl->AddBezierCubic(p0, p1, p2, p3,
                            IM_COL32(35, 30, 25, 255), 5.0f, 24);
                        /* Highlight stripe — subtle sheen along top */
                        dl->AddBezierCubic(
                            ImVec2(p0.x, p0.y - 1),
                            ImVec2(p1.x, p1.y - 1),
                            ImVec2(p2.x, p2.y - 1),
                            ImVec2(p3.x, p3.y - 1),
                            IM_COL32(70, 60, 45, 100), 1.5f, 24);

                        /* [+] button visual — drawn ON TOP of cable */
                        if (show_add) {
                            float t_pulse = (float)ImGui::GetTime();
                            float glow_a  = add_hovered ? 1.0f : 0.55f + 0.25f * sinf(t_pulse * 2.5f);
                            ImU32 bcol = IM_COL32((int)(200*glow_a),(int)(140*glow_a),(int)(20*glow_a),(int)(220*glow_a));
                            ImU32 bgcol = add_hovered ? IM_COL32(80,55,12,220) : IM_COL32(35,28,8,180);
                            float r = 6.0f;
                            dl->AddRectFilled(ImVec2(add_x,add_y), ImVec2(add_x+ADD_BTN_W,add_y+ADD_BTN_W), bgcol, r);
                            dl->AddRect(ImVec2(add_x,add_y), ImVec2(add_x+ADD_BTN_W,add_y+ADD_BTN_W), bcol, r, 0, 2.0f);
                            float cx2 = add_x + ADD_BTN_W * 0.5f;
                            float cy2 = add_y + ADD_BTN_W * 0.5f;
                            float arm = ADD_BTN_W * 0.28f;
                            ImU32 pcol = IM_COL32((int)(230*glow_a),(int)(175*glow_a),(int)(40*glow_a),255);
                            dl->AddLine(ImVec2(cx2-arm,cy2), ImVec2(cx2+arm,cy2), pcol, 2.5f);
                            dl->AddLine(ImVec2(cx2,cy2-arm), ImVec2(cx2,cy2+arm), pcol, 2.5f);
                            if (add_hovered)
                                dl->AddRect(ImVec2(add_x-2,add_y-2),ImVec2(add_x+ADD_BTN_W+2,add_y+ADD_BTN_W+2),
                                            IM_COL32(220,160,30,100), r+2, 0, 3.0f);
                        }
                    }
                }
            }

            /* ── Pedal gallery popup (500x400) ────────────────────── */
            /* Helper lambda to add a pedal from the gallery */
            auto gallery_add_pedal = [&](fx_pedal_type_t ptype) {
                fx_pedal_id nid = fx_chain_add_pedal(engine, ptype, s_add_popup_pos);
                if (nid >= 0) {
                    if (s_add_popup_pos == FX_CHAIN_POS_PRE && s_pre_id_count < 32) {
                        int slot = s_add_popup_insert_slot;
                        if (slot > s_pre_id_count) slot = s_pre_id_count;
                        for (int j = s_pre_id_count; j > slot; j--)
                            s_pre_ids[j] = s_pre_ids[j - 1];
                        s_pre_ids[slot] = nid;
                        s_pre_id_count++;
                        fx_chain_move_pedal(engine, nid, FX_CHAIN_POS_PRE, slot);
                    } else if (s_add_popup_pos == FX_CHAIN_POS_POST && s_post_id_count < 32) {
                        int slot = s_add_popup_insert_slot;
                        if (slot > s_post_id_count) slot = s_post_id_count;
                        for (int j = s_post_id_count; j > slot; j--)
                            s_post_ids[j] = s_post_ids[j - 1];
                        s_post_ids[slot] = nid;
                        s_post_id_count++;
                        fx_chain_move_pedal(engine, nid, FX_CHAIN_POS_POST, slot);
                    }
                }
            };

            ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_Always);
            if (ImGui::BeginPopup("add_pedal_popup",
                                  ImGuiWindowFlags_NoResize)) {
                /* Header */
                {
                    const char *title = (s_add_popup_pos == FX_CHAIN_POS_PRE)
                        ? "Add Pedal  [PRE-AMP]"
                        : "Add Pedal  [POST-AMP]";
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.90f, 0.65f, 0.20f, 1.0f));
                    ImGui::SetWindowFontScale(1.15f);
                    ImGui::Text("%s", title);
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::PopStyleColor();
                }
                ImGui::Separator();
                ImGui::Spacing();

                /* 3-column grid of categories */
                static const int COLS = 3;
                float col_w = (520.0f - 30.0f) / COLS;

                if (ImGui::BeginChild("##gallery_scroll", ImVec2(0, 0),
                                      false, ImGuiWindowFlags_HorizontalScrollbar)) {
                    if (ImGui::BeginTable("##gallery_table", COLS,
                                          ImGuiTableFlags_SizingFixedFit)) {
                        for (int ci = 0; ci < s_pedal_category_count; ci++) {
                            ImGui::TableNextColumn();
                            const PedalCategory &cat = s_pedal_categories[ci];

                            /* Category header */
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                ImVec4(0.80f, 0.60f, 0.18f, 1.0f));
                            ImGui::TextUnformatted(cat.label);
                            ImGui::PopStyleColor();
                            ImGui::PushStyleColor(ImGuiCol_Separator,
                                ImVec4(0.50f, 0.35f, 0.10f, 0.6f));
                            ImGui::Separator();
                            ImGui::PopStyleColor();

                            /* Pedal rows */
                            for (int pi = 0; pi < cat.count; pi++) {
                                const PedalEntry &pe = cat.pedals[pi];

                                /* Build unique selectable ID */
                                char sel_id[64];
                                snprintf(sel_id, sizeof(sel_id),
                                         "%s##gal_%d_%d", pe.name, ci, pi);

                                ImGui::PushStyleColor(ImGuiCol_Header,
                                    ImVec4(0.25f, 0.18f, 0.06f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                    ImVec4(0.60f, 0.42f, 0.10f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                    ImVec4(0.80f, 0.55f, 0.12f, 1.0f));

                                if (ImGui::Selectable(sel_id, false,
                                                      ImGuiSelectableFlags_None,
                                                      ImVec2(col_w - 8.0f, 0))) {
                                    gallery_add_pedal(pe.type);
                                    ImGui::CloseCurrentPopup();
                                }
                                if (ImGui::IsItemHovered()) {
                                    const char *tip = get_pedal_tooltip(pe.type);
                                    if (tip) ImGui::SetTooltip("%s", tip);
                                }
                                ImGui::PopStyleColor(3);
                            }
                            ImGui::Spacing();
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndChild();
                }
                ImGui::EndPopup();
            }

            /* ── Rack effect add popup (post-amp [+]) ──── */
            ImGui::SetNextWindowSize(ImVec2(320, 340), ImGuiCond_Always);
            if (ImGui::BeginPopup("add_studio_popup",
                                  ImGuiWindowFlags_NoResize)) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(0.90f, 0.65f, 0.20f, 1.0f));
                ImGui::SetWindowFontScale(1.15f);
                ImGui::Text("Add Rack Effect  [POST-AMP]");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::Spacing();

                static const struct { fx_studio_type_t type; const char *name; const char *desc; } studio_menu[] = {
                    { FX_STUDIO_IRON_SQUEEZE, "Iron Squeeze",  "FET compressor — punchy, fast attack" },
                    { FX_STUDIO_VELVET_PRESS, "Velvet Press",  "Optical compressor — smooth, musical" },
                    { FX_STUDIO_GLUE_BUS,     "Glue Bus",      "VCA bus compressor — glue, punch" },
                    { FX_STUDIO_GLASS_EQ,     "Glass EQ",      "Passive EQ — musical, sweet top end" },
                    { FX_STUDIO_PRECISION_EQ, "Precision EQ",  "Channel EQ — warm, proportional-Q" },
                    { FX_STUDIO_REEL_WARMTH,  "Reel Warmth",   "Tape saturation — warmth, harmonics" },
                    { FX_STUDIO_VALVE_COLOR,  "Valve Color",   "Tube saturation — rich harmonics" },
                    { FX_STUDIO_BRICK_WALL,   "Brick Wall",    "Brickwall limiter — output protection" },
                    { FX_STUDIO_ROOM_ENGINE,  "Room Engine",   "Room simulation — studio ambience" },
                };

                for (int si = 0; si < 9; si++) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.22f, 0.30f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.35f, 0.50f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.30f, 0.45f, 0.65f, 1.0f));

                    char sel_id[64];
                    snprintf(sel_id, sizeof(sel_id), "%s##studio_%d",
                             studio_menu[si].name, si);
                    if (ImGui::Selectable(sel_id, false)) {
                        if (s_studio_id_count < 8) {
                            fx_studio_id nid = fx_studio_add(engine, studio_menu[si].type);
                            if (nid >= 0) {
                                s_studio_ids[s_studio_id_count++] = nid;
                            }
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", studio_menu[si].desc);

                    ImGui::PopStyleColor(3);
                }

                ImGui::EndPopup();
            }

            ImGui::End();
        }

        /* ============================================================
         * DETAIL VIEW (~50% of window)
         * Shows knobs/controls for the selected node
         * ============================================================ */
        {
            float looper_h = s_looper_panel_open ? LOOPER_H : 0.0f;
            float chain_top = TOOLBAR_H + looper_h;
            float chain_area_h = (win_h - chain_top - STATUS_H) * 0.35f;
            float detail_y = chain_top + chain_area_h;
            float detail_h = win_h - detail_y - STATUS_H;

            ImGui::SetNextWindowPos(ImVec2(0, detail_y));
            ImGui::SetNextWindowSize(ImVec2(win_w, detail_h));
            ImGui::Begin("##detail_view", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_HorizontalScrollbar);

            /* ── Detail view background: amp rack interior (tolex) ── */
            {
                ImDrawList *dl_dv = ImGui::GetWindowDrawList();
                ImVec2 dv_min = ImGui::GetWindowPos();
                ImVec2 dv_max = ImVec2(dv_min.x + win_w, dv_min.y + detail_h);

                if (s_tex_tolex) {
                    /* Tile tolex texture at 256x256 — even more subtle than
                     * pedalboard; this is the amp rack padded interior */
                    const float TILE = 256.0f;
                    dl_dv->PushClipRect(dv_min, dv_max, true);
                    for (float ty = dv_min.y; ty < dv_max.y; ty += TILE) {
                        for (float tx = dv_min.x; tx < dv_max.x; tx += TILE) {
                            float tx1 = (tx + TILE < dv_max.x) ? tx + TILE : dv_max.x;
                            float ty1 = (ty + TILE < dv_max.y) ? ty + TILE : dv_max.y;
                            float u1 = (tx1 - tx) / TILE;
                            float v1 = (ty1 - ty) / TILE;
                            /* Darker than pedalboard (alpha 40 vs 55) */
                            dl_dv->AddImage((ImTextureID)s_tex_tolex,
                                ImVec2(tx, ty), ImVec2(tx1, ty1),
                                ImVec2(0.0f, 0.0f), ImVec2(u1, v1),
                                IM_COL32(255, 255, 255, 40));
                        }
                    }
                    dl_dv->PopClipRect();
                } else {
                    /* Fallback: slightly different shade than pedalboard area */
                    dl_dv->AddRectFilled(dv_min, dv_max, IM_COL32(18, 16, 13, 255));
                }

                /* Top inner shadow: chain area "casts" shadow downward */
                dl_dv->AddRectFilledMultiColor(
                    dv_min, ImVec2(dv_max.x, dv_min.y + 14.0f),
                    IM_COL32(0, 0, 0, 100), IM_COL32(0, 0, 0, 100),
                    IM_COL32(0, 0, 0,   0), IM_COL32(0, 0, 0,   0));
                /* Thin top border line separating chain from detail */
                dl_dv->AddLine(
                    ImVec2(dv_min.x, dv_min.y),
                    ImVec2(dv_max.x, dv_min.y),
                    IM_COL32(45, 38, 28, 200), 1.0f);
            }

            if (s_selected_node < 0 || s_selected_node >= chain_len) {
                /* Nothing selected */
                float avail_w = ImGui::GetContentRegionAvail().x;
                float avail_h = ImGui::GetContentRegionAvail().y;
                const char *msg = "Click a node in the signal chain to edit";
                ImVec2 ts = ImGui::CalcTextSize(msg);
                ImGui::SetCursorPos(ImVec2((avail_w - ts.x) * 0.5f, (avail_h - ts.y) * 0.5f));
                ImGui::TextDisabled("%s", msg);
            }
            else {
                ChainNode &sel = chain[s_selected_node];

                /* ── SPLIT / MERGE (mixer) detail view ───────── */
                if (sel.kind == NODE_SPLIT) {
                    float avail_w = ImGui::GetContentRegionAvail().x;
                    ImGui::SetWindowFontScale(1.35f);
                    const char *title = "Y-Split";
                    ImVec2 ts = ImGui::CalcTextSize(title);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                    ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", title);
                    ImGui::SetWindowFontScale(1.0f);
                    const char *sub = "Signal splits into two parallel amp chains";
                    ImVec2 sub_sz = ImGui::CalcTextSize(sub);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                    ImGui::TextDisabled("%s", sub);
                    ImGui::Dummy(ImVec2(0.0f, 12.0f));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - 300.0f) * 0.5f);
                    ImGui::TextDisabled("Click the AMP A / AMP B or CAB A / CAB B nodes to edit each path.");
                    ImGui::Dummy(ImVec2(0.0f, 8.0f));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - 300.0f) * 0.5f);
                    ImGui::TextDisabled("Click MIX to set per-path blend levels.");
                }

                /* ── MERGE (mixer) detail view ────────────────── */
                else if (sel.kind == NODE_MERGE) {
                    ImGui::PushID("merge_detail");
                    float avail_w = ImGui::GetContentRegionAvail().x;
                    ImGui::SetWindowFontScale(1.35f);
                    const char *title = "Mix / Blend";
                    ImVec2 ts = ImGui::CalcTextSize(title);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                    ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", title);
                    ImGui::SetWindowFontScale(1.0f);
                    const char *sub = "Per-chain blend levels";
                    ImVec2 sub_sz = ImGui::CalcTextSize(sub);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                    ImGui::TextDisabled("%s", sub);
                    ImGui::Dummy(ImVec2(0.0f, 4.0f));
                    {
                        ImVec2 sep_p0 = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddLine(
                            sep_p0, ImVec2(sep_p0.x + avail_w, sep_p0.y),
                            IM_COL32(180, 130, 40, 100), 1.0f);
                        ImGui::Dummy(ImVec2(0.0f, 8.0f));
                    }

                    float slider_w = 280.0f;
                    float slider_x = (avail_w - slider_w) * 0.5f;
                    if (slider_x < 0.0f) slider_x = 0.0f;

                    /* Chain A mix slider */
                    {
                        float mix_a = fx_chain_get_mix(engine, FX_CHAIN_DEFAULT);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slider_x);
                        ImGui::SetNextItemWidth(slider_w);
                        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.80f, 0.55f, 0.15f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.70f, 0.20f, 1.0f));
                        if (ImGui::SliderFloat("Chain A Level", &mix_a, 0.0f, 1.0f, "%.2f")) {
                            fx_chain_set_mix(engine, FX_CHAIN_DEFAULT, mix_a);
                        }
                        ImGui::PopStyleColor(2);
                    }
                    ImGui::Dummy(ImVec2(0.0f, 8.0f));
                    /* Chain B mix slider */
                    if (s_chain_b >= 0) {
                        float mix_b = fx_chain_get_mix(engine, s_chain_b);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slider_x);
                        ImGui::SetNextItemWidth(slider_w);
                        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.30f, 0.60f, 0.80f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.40f, 0.75f, 1.0f, 1.0f));
                        if (ImGui::SliderFloat("Chain B Level", &mix_b, 0.0f, 1.0f, "%.2f")) {
                            fx_chain_set_mix(engine, s_chain_b, mix_b);
                        }
                        ImGui::PopStyleColor(2);
                    }
                    ImGui::PopID(); /* merge_detail */
                }

                /* ── AMP detail view ──────────────────────────── */
                else if (sel.kind == NODE_AMP) {
                    fx_chain_id amp_chain = (fx_chain_id)sel.chain_id;
                    fx_amp_type_t amp_type = fx_amp_get_model(engine, amp_chain);

                    static const char *amp_names[] = {
                        "Fullerton Clean", "British Crunch", "Southwest Lead",
                        "Essex Chime", "Tweed Blues", "Meridian High Gain",
                        "Citrus Roar", "Citrus Terror", "Regent 800",
                        "Solar Monolith", "Eclipse Drone", "Emerald Ratrod Deluxe"
                    };
                    int current_amp = (int)amp_type;
                    float avail_w = ImGui::GetContentRegionAvail().x;

                    /* Title — large amp name + "Amp Model" subtitle */
                    {
                        const char *amp_name = fx_amp_get_type_name(amp_type);
                        ImGui::SetWindowFontScale(1.35f);
                        ImVec2 text_size = ImGui::CalcTextSize(amp_name);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - text_size.x) * 0.5f);
                        ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", amp_name);
                        ImGui::SetWindowFontScale(1.0f);
                        char sub[64];
                        snprintf(sub, sizeof(sub), "Amp Model %s",
                                 is_dual ? (amp_chain == 0 ? "— Chain A" : "— Chain B") : "");
                        ImVec2 sub_sz = ImGui::CalcTextSize(sub);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                        ImGui::TextDisabled("%s", sub);
                    }

                    ImGui::Dummy(ImVec2(0.0f, 4.0f));
                    {
                        ImVec2 sep_p0 = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddLine(
                            sep_p0, ImVec2(sep_p0.x + avail_w, sep_p0.y),
                            IM_COL32(180, 130, 40, 100), 1.0f);
                        ImGui::Dummy(ImVec2(0.0f, 3.0f));
                    }

                    /* Model selector — "Model | [dropdown]" centered */
                    {
                        float label_w = ImGui::CalcTextSize("Model").x;
                        float combo_w = 200.0f;
                        float total_w = label_w + 8.0f + combo_w;
                        float combo_off = (avail_w - total_w) * 0.5f;
                        if (combo_off > 0.0f)
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + combo_off);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Model");
                        ImGui::SameLine(0, 8);
                        ImGui::SetNextItemWidth(combo_w);
                        char model_label[32];
                        snprintf(model_label, sizeof(model_label), "##amp_model_%d", (int)amp_chain);
                        if (ImGui::Combo(model_label, &current_amp, amp_names, FX_AMP_COUNT)) {
                            fx_amp_set_model(engine, amp_chain, (fx_amp_type_t)current_amp);
                        }
                        /* Scroll wheel to cycle through amp models when hovered */
                        if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
                            float wheel = ImGui::GetIO().MouseWheel;
                            if (wheel != 0.0f) {
                                int next = current_amp + (wheel < 0.0f ? 1 : -1);
                                if (next < 0) next = FX_AMP_COUNT - 1;
                                if (next >= FX_AMP_COUNT) next = 0;
                                current_amp = next;
                                fx_amp_set_model(engine, amp_chain, (fx_amp_type_t)current_amp);
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            static const char *amp_descs[] = {
                                "Clean, chimey American tone — silver panel era",
                                "Classic British crunch — plexi-era overdrive",
                                "High-gain American lead — tight, aggressive",
                                "British chime and jangle — Class A character",
                                "Warm vintage blues — tweed era breakup",
                                "Brutal modern metal — scooped, crushing gain",
                                "Thick British roar — EL34 warmth and fuzz",
                                "Small but fierce — Class A lunchbox grit",
                                "Classic British rock — single-channel aggression",
                                "Massive doom — thunderous clean into crushing fuzz",
                                "Extreme drone — subsonic doom with feedback sustain",
                                "American hotrod combo — clean to gritty drive, 6L6 punch",
                            };
                            if (current_amp >= 0 && current_amp < FX_AMP_COUNT)
                                ImGui::SetTooltip("%s", amp_descs[current_amp]);
                        }
                    }

                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    /* Amp face image with interactive overlay knobs */
                    int param_count = fx_amp_get_param_count(amp_type);
                    auto has_param = [&](fx_amp_param_t p) -> bool {
                        /* Standard contiguous params (Gain..Cut) */
                        if ((int)p < param_count) return true;
                        /* Citrus Terror uses the Tone param */
                        if (amp_type == FX_AMP_CITRUS_TERROR && p == FX_AMP_PARAM_TONE)
                            return true;
                        /* Eclipse Drone uses the Feedback param */
                        if (amp_type == FX_AMP_ECLIPSE_DRONE && p == FX_AMP_PARAM_FEEDBACK)
                            return true;
                        return false;
                    };

                    {
                        const char *aname = fx_amp_get_type_name(amp_type);
                        uintptr_t face_tex = load_amp_face_texture(aname);

                        /* Use actual image aspect ratio */
                        float img_w = 500.0f;
                        float img_h = img_w * 0.65f;
                        if (face_tex) {
                            int tw = 0, th = 0;
                            if (fx_texture_get_size(face_tex, &tw, &th) && th > 0) {
                                float aspect = (float)tw / (float)th;
                                img_h = img_w / aspect;
                            }
                        }
                        float img_x = (avail_w - img_w) * 0.5f;
                        if (img_x < 0.0f) img_x = 0.0f;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + img_x);

                        ImVec2 img_pos = ImGui::GetCursorScreenPos();

                        if (face_tex) {
                            ImGui::Image((ImTextureID)face_tex, ImVec2(img_w, img_h),
                                         ImVec2(0, 0), ImVec2(1, 1),
                                         ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                        } else {
                            ImGui::Dummy(ImVec2(img_w, img_h));
                        }

                        /* Knob position maps — normalized (x,y) on the amp face image.
                         * Each amp model has knobs at different positions on its faceplate.
                         * Positions estimated from the generated amp images. */
                        struct AmpKnobPos {
                            fx_amp_param_t param;
                            float nx, ny;   /* normalized position on image */
                        };

                        /* Knob size on the image */
                        const float OVERLAY_KNOB_SZ = 34.0f;
                        const char *knob_tex = "resources/knobs/knob_dome_silver_nobg.png";

                        /* Positions from red dots — size-filtered detection.
                         * -1 param = dummy knob (fills hole, not interactive) */
                        const int DUMMY = -1;

                        /* Fullerton Clean: 7 knobs */
                        static const AmpKnobPos fullerton_knobs[] = {
                            { FX_AMP_PARAM_VOLUME,   0.299f, 0.476f },
                            { FX_AMP_PARAM_TREBLE,   0.375f, 0.475f },
                            { FX_AMP_PARAM_MID,      0.451f, 0.477f },
                            { FX_AMP_PARAM_BASS,     0.527f, 0.476f },
                            { FX_AMP_PARAM_PRESENCE, 0.603f, 0.477f },
                            { FX_AMP_PARAM_GAIN,     0.678f, 0.476f },
                            { FX_AMP_PARAM_SAG,      0.753f, 0.476f },
                        };

                        /* British Crunch: 7 knobs */
                        static const AmpKnobPos brit_crunch_knobs[] = {
                            { FX_AMP_PARAM_PRESENCE, 0.341f, 0.536f },
                            { FX_AMP_PARAM_BASS,     0.419f, 0.538f },
                            { FX_AMP_PARAM_MID,      0.496f, 0.541f },
                            { FX_AMP_PARAM_TREBLE,   0.574f, 0.539f },
                            { FX_AMP_PARAM_VOLUME,   0.653f, 0.538f },
                            { FX_AMP_PARAM_GAIN,     0.730f, 0.538f },
                            { FX_AMP_PARAM_MASTER,   0.808f, 0.538f },
                        };

                        /* Southwest Lead: 6 knobs */
                        static const AmpKnobPos southwest_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.343f, 0.651f },
                            { FX_AMP_PARAM_BASS,     0.439f, 0.650f },
                            { FX_AMP_PARAM_MID,      0.533f, 0.652f },
                            { FX_AMP_PARAM_TREBLE,   0.630f, 0.652f },
                            { FX_AMP_PARAM_PRESENCE, 0.727f, 0.652f },
                            { FX_AMP_PARAM_MASTER,   0.822f, 0.651f },
                        };

                        /* Essex Chime: 7 dots, 6 params — last 1 dummy */
                        static const AmpKnobPos essex_knobs[] = {
                            { FX_AMP_PARAM_VOLUME,   0.288f, 0.378f },
                            { FX_AMP_PARAM_BASS,     0.369f, 0.378f },
                            { FX_AMP_PARAM_TREBLE,   0.450f, 0.379f },
                            { FX_AMP_PARAM_CUT,      0.530f, 0.379f },
                            { FX_AMP_PARAM_PRESENCE, 0.610f, 0.378f },
                            { FX_AMP_PARAM_GAIN,     0.688f, 0.378f },
                            { (fx_amp_param_t)DUMMY,  0.769f, 0.378f },
                        };

                        /* Tweed Blues: 5 knobs */
                        static const AmpKnobPos tweed_knobs[] = {
                            { FX_AMP_PARAM_VOLUME,   0.344f, 0.575f },
                            { FX_AMP_PARAM_BASS,     0.438f, 0.575f },
                            { FX_AMP_PARAM_TREBLE,   0.532f, 0.573f },
                            { FX_AMP_PARAM_GAIN,     0.625f, 0.573f },
                            { FX_AMP_PARAM_MASTER,   0.718f, 0.574f },
                        };

                        /* Meridian High Gain: 7 knobs */
                        static const AmpKnobPos meridian_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.323f, 0.600f },
                            { FX_AMP_PARAM_BASS,     0.411f, 0.601f },
                            { FX_AMP_PARAM_MID,      0.498f, 0.600f },
                            { FX_AMP_PARAM_TREBLE,   0.583f, 0.600f },
                            { FX_AMP_PARAM_PRESENCE, 0.668f, 0.601f },
                            { FX_AMP_PARAM_VOLUME,   0.753f, 0.601f },
                            { FX_AMP_PARAM_MASTER,   0.838f, 0.600f },
                        };

                        /* Citrus Roar: 5 knobs */
                        static const AmpKnobPos citrus_roar_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.348f, 0.546f },
                            { FX_AMP_PARAM_BASS,     0.415f, 0.550f },
                            { FX_AMP_PARAM_MID,      0.484f, 0.559f },
                            { FX_AMP_PARAM_TREBLE,   0.554f, 0.567f },
                            { FX_AMP_PARAM_VOLUME,   0.625f, 0.573f },
                        };

                        /* Citrus Terror: 3 knobs */
                        static const AmpKnobPos citrus_terror_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.299f, 0.528f },
                            { FX_AMP_PARAM_TONE,     0.423f, 0.527f },
                            { FX_AMP_PARAM_VOLUME,   0.541f, 0.527f },
                        };

                        /* Regent 800: 7 knobs */
                        static const AmpKnobPos regent_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.404f, 0.601f },
                            { FX_AMP_PARAM_BASS,     0.472f, 0.602f },
                            { FX_AMP_PARAM_MID,      0.539f, 0.601f },
                            { FX_AMP_PARAM_TREBLE,   0.607f, 0.602f },
                            { FX_AMP_PARAM_PRESENCE, 0.675f, 0.602f },
                            { FX_AMP_PARAM_VOLUME,   0.743f, 0.600f },
                            { FX_AMP_PARAM_MASTER,   0.810f, 0.600f },
                        };

                        /* Solar Monolith: 6 knobs */
                        static const AmpKnobPos solar_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.231f, 0.513f },
                            { FX_AMP_PARAM_BASS,     0.328f, 0.514f },
                            { FX_AMP_PARAM_MID,      0.425f, 0.513f },
                            { FX_AMP_PARAM_TREBLE,   0.524f, 0.515f },
                            { FX_AMP_PARAM_VOLUME,   0.619f, 0.513f },
                            { FX_AMP_PARAM_MASTER,   0.718f, 0.514f },
                        };

                        /* Eclipse Drone: 6 knobs */
                        static const AmpKnobPos eclipse_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.136f, 0.507f },
                            { FX_AMP_PARAM_BASS,     0.241f, 0.507f },
                            { FX_AMP_PARAM_MID,      0.347f, 0.506f },
                            { FX_AMP_PARAM_TREBLE,   0.454f, 0.507f },
                            { FX_AMP_PARAM_FEEDBACK, 0.557f, 0.505f },
                            { FX_AMP_PARAM_VOLUME,   0.662f, 0.507f },
                        };

                        /* Emerald Deluxe: 7 knobs (same layout as Fullerton Clean) */
                        static const AmpKnobPos emerald_deluxe_knobs[] = {
                            { FX_AMP_PARAM_VOLUME,   0.345f, 0.289f },
                            { FX_AMP_PARAM_TREBLE,   0.409f, 0.286f },
                            { FX_AMP_PARAM_MID,      0.472f, 0.286f },
                            { FX_AMP_PARAM_BASS,     0.536f, 0.286f },
                            { FX_AMP_PARAM_PRESENCE, 0.599f, 0.289f },
                            { FX_AMP_PARAM_GAIN,     0.663f, 0.289f },
                            { FX_AMP_PARAM_SAG,      0.726f, 0.289f },
                        };

                        /* Select the right map */
                        const AmpKnobPos *knob_map = nullptr;
                        int knob_map_count = 0;
                        switch (amp_type) {
                            case FX_AMP_FULLERTON_CLEAN:
                                knob_map = fullerton_knobs;
                                knob_map_count = 7;
                                break;
                            case FX_AMP_BRIT_CRUNCH:
                                knob_map = brit_crunch_knobs;
                                knob_map_count = 7;
                                break;
                            case FX_AMP_SOUTHWEST_LEAD:
                                knob_map = southwest_knobs;
                                knob_map_count = 6;
                                break;
                            case FX_AMP_ESSEX_CHIME:
                                knob_map = essex_knobs;
                                knob_map_count = 7;
                                break;
                            case FX_AMP_TWEED_BLUES:
                                knob_map = tweed_knobs;
                                knob_map_count = 5;
                                break;
                            case FX_AMP_MERIDIAN_HIGH_GAIN:
                                knob_map = meridian_knobs;
                                knob_map_count = 7;
                                break;
                            case FX_AMP_CITRUS_ROAR:
                                knob_map = citrus_roar_knobs;
                                knob_map_count = 5;
                                break;
                            case FX_AMP_CITRUS_TERROR:
                                knob_map = citrus_terror_knobs;
                                knob_map_count = 3;
                                break;
                            case FX_AMP_REGENT_800:
                                knob_map = regent_knobs;
                                knob_map_count = 7;
                                break;
                            case FX_AMP_SOLAR_MONOLITH:
                                knob_map = solar_knobs;
                                knob_map_count = 6;
                                break;
                            case FX_AMP_ECLIPSE_DRONE:
                                knob_map = eclipse_knobs;
                                knob_map_count = 6;
                                break;
                            case FX_AMP_EMERALD_DELUXE:
                                knob_map = emerald_deluxe_knobs;
                                knob_map_count = 7;
                                break;
                            default: break;
                        }

                        /* Render overlay knobs on the amp face */
                        if (knob_map) {
                            for (int ki = 0; ki < knob_map_count; ki++) {
                                const AmpKnobPos &kp = knob_map[ki];
                                float kx = img_pos.x + kp.nx * img_w - OVERLAY_KNOB_SZ * 0.5f;
                                float ky = img_pos.y + kp.ny * img_h - OVERLAY_KNOB_SZ * 0.5f;

                                if ((int)kp.param == DUMMY || !has_param(kp.param)) {
                                    /* Dummy knob — static, no interaction */
                                    float dummy = 0.5f;
                                    char did[32];
                                    snprintf(did, sizeof(did), "##amp_dummy_%d_%d", (int)amp_chain, ki);
                                    knob_overlay(did, &dummy, 0.0f, 1.0f, 0.5f, 0.0f,
                                                 kx, ky, OVERLAY_KNOB_SZ, knob_tex);
                                } else {
                                    const char *pname = fx_amp_get_param_name(amp_type, kp.param);
                                    float val = fx_amp_get_param(engine, amp_chain, kp.param);
                                    char kid[48];
                                    snprintf(kid, sizeof(kid), "%s##amp_ov_%d_%d",
                                             pname, (int)amp_chain, ki);
                                    if (knob_overlay(kid, &val, 0.0f, 1.0f, 0.5f, 0.01f,
                                                     kx, ky, OVERLAY_KNOB_SZ, knob_tex)) {
                                        fx_amp_set_param(engine, amp_chain, kp.param, val);
                                    }
                                }
                            }
                        }
                    }

                    /* Fallback: any params NOT in the overlay map still get regular knobs */
                    {
                        /* Check which params are mapped */
                        bool param_mapped[FX_AMP_PARAM_COUNT] = {};
                        /* (overlay knobs handle the main params — show remaining below) */

                        /* Show unmapped params as regular knobs */
                        bool has_unmapped = false;
                        for (int p = 0; p < param_count; p++) {
                            if (!param_mapped[p]) { has_unmapped = true; break; }
                        }
                        /* All amp params are covered by the overlay maps above,
                         * but keep this fallback for safety */
                        (void)has_unmapped;
                    }
                }

                /* ── CAB detail view ──────────────────────────── */
                else if (sel.kind == NODE_CAB) {
                    fx_chain_id cab_chain = (fx_chain_id)sel.chain_id;
                    int &cab_type_ref = (sel.chain_id == 0) ? s_cab_type : s_cab_type_b;
                    float avail_w = ImGui::GetContentRegionAvail().x;

                    /* Large title: "Cabinet" */
                    {
                        const char *title = is_dual ? (sel.chain_id == 0 ? "Cabinet A" : "Cabinet B") : "Cabinet";
                        ImGui::SetWindowFontScale(1.35f);
                        ImVec2 ts = ImGui::CalcTextSize(title);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                        ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", title);
                        ImGui::SetWindowFontScale(1.0f);
                        /* Subtitle: "Cabinet — 4x12 Straight" or "Cabinet — <Custom Name>" */
                        const char *sub_active_ir = fx_cab_get_custom_ir_path(engine, cab_chain);
                        bool sub_is_custom = (sub_active_ir && *sub_active_ir);
                        const char *cab_name;
                        if (sub_is_custom) {
                            cab_name = fx_cab_get_custom_name(engine, cab_chain);
                            if (!cab_name || !*cab_name) cab_name = "Custom IR";
                        } else {
                            cab_name = (cab_type_ref >= 0 && cab_type_ref < FX_CAB_TYPE_COUNT)
                                ? s_cab_type_names[cab_type_ref] : "Unknown";
                        }
                        char sub[80];
                        snprintf(sub, sizeof(sub), "Cabinet \xe2\x80\x94 %s", cab_name);
                        ImVec2 sub_sz = ImGui::CalcTextSize(sub);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                        ImGui::TextDisabled("%s", sub);
                    }
                    ImGui::Dummy(ImVec2(0.0f, 4.0f));
                    {
                        ImVec2 sep_p0 = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddLine(
                            sep_p0, ImVec2(sep_p0.x + avail_w, sep_p0.y),
                            IM_COL32(180, 130, 40, 100), 1.0f);
                        ImGui::Dummy(ImVec2(0.0f, 3.0f));
                    }
                    ImGui::Dummy(ImVec2(0.0f, 4.0f));

                    /* Cab type selector — "Cab Type | [dropdown] [Load IR...]" centered */
                    const float cab_combo_width = 200.0f;
                    const float cab_load_btn_w  = 110.0f;
                    {
                        float label_w = ImGui::CalcTextSize("Cab Type").x;
                        float total_w = label_w + 8.0f + cab_combo_width + 6.0f + cab_load_btn_w;
                        float combo_off = (avail_w - total_w) * 0.5f;
                        if (combo_off > 0.0f)
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + combo_off);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Cab Type");
                        ImGui::SameLine(0, 8);
                    }
                    /* Dropdown: stock cabs + custom cabs from library + "Add..." entry.
                     * "Custom" is active when the engine reports a custom_ir_path. */
                    const char *active_ir  = fx_cab_get_custom_ir_path(engine, cab_chain);
                    bool is_custom_active  = (active_ir && *active_ir);
                    const char *preview    = is_custom_active
                        ? fx_cab_get_custom_name(engine, cab_chain)
                        : s_cab_type_names[cab_type_ref];
                    if (!preview || !*preview) preview = "(unnamed)";

                    ImGui::SetNextItemWidth(cab_combo_width);
                    char cab_combo_id[32];
                    snprintf(cab_combo_id, sizeof(cab_combo_id), "##cab_sel_%d", sel.chain_id);
                    if (ImGui::BeginCombo(cab_combo_id, preview)) {
                        for (int i = 0; i < FX_CAB_TYPE_COUNT; i++) {
                            bool sel_i = !is_custom_active && (cab_type_ref == i);
                            if (ImGui::Selectable(s_cab_type_names[i], sel_i)) {
                                cab_type_ref = i;
                                load_cab_for_type(engine, cab_chain, (fx_cab_type_t)i);
                            }
                        }
                        if (s_custom_cab_count > 0) {
                            ImGui::Separator();
                            for (int i = 0; i < s_custom_cab_count; i++) {
                                bool sel_i = is_custom_active &&
                                             strcmp(active_ir, s_custom_cabs[i].ir_path) == 0;
                                char label[80];
                                snprintf(label, sizeof(label), "%s##cc%d",
                                         s_custom_cabs[i].name, i);
                                if (ImGui::Selectable(label, sel_i)) {
                                    if (fx_cab_load_ir(engine, cab_chain,
                                                       s_custom_cabs[i].ir_path)) {
                                        fx_cab_set_custom_name(engine, cab_chain,
                                                               s_custom_cabs[i].name);
                                        fx_cab_set_custom_image_path(engine, cab_chain,
                                                                     s_custom_cabs[i].image_path);
                                    } else {
                                        FX_WARN("Custom IR load failed: %s",
                                                s_custom_cabs[i].ir_path);
                                        snprintf(s_save_toast, sizeof(s_save_toast),
                                                 "Invalid IR: %s — must be mono/stereo "
                                                 "44.1/48kHz WAV, under 2s",
                                                 s_custom_cabs[i].name);
                                        s_save_toast_timer = 4.0f;
                                    }
                                }
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::Selectable("+ Add Custom IR...")) {
                            char picked[1024];
                            nfdu8filteritem_t filt[1] = { { "Wav audio", "wav" } };
                            if (open_file_picker(filt, 1, picked, sizeof(picked))) {
                                int idx = custom_cab_add(picked);
                                if (idx >= 0 &&
                                    fx_cab_load_ir(engine, cab_chain,
                                                   s_custom_cabs[idx].ir_path)) {
                                    fx_cab_set_custom_name(engine, cab_chain,
                                                           s_custom_cabs[idx].name);
                                    fx_cab_set_custom_image_path(engine, cab_chain,
                                                                 s_custom_cabs[idx].image_path);
                                    custom_cabs_save();
                                    FX_INFO("Custom IR added: %s", picked);
                                } else if (idx >= 0) {
                                    FX_WARN("Failed to load picked IR %s — removing from library",
                                            picked);
                                    custom_cab_remove(idx);
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                    /* Capture combo hover BEFORE emitting the button — otherwise
                     * IsItemHovered() below would refer to the button instead. */
                    bool combo_hovered = ImGui::IsItemHovered();

                    /* "Import IR..." button opens a popup with Single / Folder choices. */
                    ImGui::SameLine(0, 6);
                    {
                        char load_btn_id[48], popup_id[48];
                        snprintf(load_btn_id, sizeof(load_btn_id),
                                 "Import IR...##cab_import_%d", sel.chain_id);
                        snprintf(popup_id, sizeof(popup_id),
                                 "cab_import_popup_%d", sel.chain_id);
                        if (ImGui::Button(load_btn_id, ImVec2(cab_load_btn_w, 0))) {
                            ImGui::OpenPopup(popup_id);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Upload a single IR or bulk-import a folder");
                        }
                        if (ImGui::BeginPopup(popup_id)) {
                            if (ImGui::MenuItem("Single WAV file...")) {
                                char picked[1024];
                                nfdu8filteritem_t filt[1] = { { "Wav audio", "wav" } };
                                if (open_file_picker(filt, 1, picked, sizeof(picked))) {
                                    int idx = custom_cab_add(picked);
                                    if (idx >= 0 &&
                                        fx_cab_load_ir(engine, cab_chain,
                                                       s_custom_cabs[idx].ir_path)) {
                                        fx_cab_set_custom_name(engine, cab_chain,
                                                               s_custom_cabs[idx].name);
                                        fx_cab_set_custom_image_path(engine, cab_chain,
                                                                     s_custom_cabs[idx].image_path);
                                        custom_cabs_save();
                                        FX_INFO("Custom IR added: %s", picked);
                                    } else if (idx >= 0) {
                                        FX_WARN("Failed to load picked IR %s — removing from library",
                                                picked);
                                        custom_cab_remove(idx);
                                        snprintf(s_save_toast, sizeof(s_save_toast),
                                                 "Invalid IR — must be mono/stereo "
                                                 "44.1/48kHz WAV, under 2s");
                                        s_save_toast_timer = 4.0f;
                                    }
                                }
                            }
                            if (ImGui::MenuItem("Folder (bulk, recursive)...")) {
                                char folder[1024];
                                if (open_folder_picker(folder, sizeof(folder))) {
                                    bulk_import_result_t r =
                                        custom_cab_bulk_import(folder);
                                    if (r.added > 0) custom_cabs_save();
                                    FX_INFO("Bulk import from '%s': %d scanned, "
                                            "%d added, %d already in library%s",
                                            folder, r.scanned, r.added,
                                            r.skipped_existing,
                                            r.capacity_hit
                                              ? " (library cap hit)" : "");
                                }
                            }
                            ImGui::EndPopup();
                        }
                    }

                    /* Scroll wheel cycles through ALL cabs in the dropdown —
                     * stock first, then every custom entry in the library.
                     * Invalid IRs (too long, wrong SR, etc.) are skipped
                     * automatically so the user isn't stuck on a bad file. */
                    if (combo_hovered &&
                        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
                        float wheel = ImGui::GetIO().MouseWheel;
                        if (wheel != 0.0f) {
                            int total = FX_CAB_TYPE_COUNT + s_custom_cab_count;
                            int cur;
                            if (is_custom_active) {
                                int ci = custom_cab_find(active_ir);
                                cur = (ci >= 0) ? (FX_CAB_TYPE_COUNT + ci)
                                                : cab_type_ref;
                            } else {
                                cur = cab_type_ref;
                            }
                            int dir = (wheel < 0.0f ? 1 : -1);
                            int next = cur;
                            int skipped = 0;
                            bool loaded = false;
                            for (int attempt = 0; attempt < total; attempt++) {
                                next += dir;
                                if (next < 0) next = total - 1;
                                if (next >= total) next = 0;
                                if (next == cur) break;
                                if (next < FX_CAB_TYPE_COUNT) {
                                    cab_type_ref = next;
                                    load_cab_for_type(engine, cab_chain,
                                                      (fx_cab_type_t)cab_type_ref);
                                    loaded = true;
                                    break;
                                }
                                int ci = next - FX_CAB_TYPE_COUNT;
                                if (fx_cab_load_ir(engine, cab_chain,
                                                   s_custom_cabs[ci].ir_path)) {
                                    fx_cab_set_custom_name(engine, cab_chain,
                                                           s_custom_cabs[ci].name);
                                    fx_cab_set_custom_image_path(engine, cab_chain,
                                                                 s_custom_cabs[ci].image_path);
                                    loaded = true;
                                    break;
                                }
                                skipped++;
                            }
                            if (!loaded && skipped > 0) {
                                snprintf(s_save_toast, sizeof(s_save_toast),
                                         "Skipped %d invalid IR%s (all remaining rejected)",
                                         skipped, skipped == 1 ? "" : "s");
                                s_save_toast_timer = 4.0f;
                            } else if (skipped > 0) {
                                snprintf(s_save_toast, sizeof(s_save_toast),
                                         "Skipped %d invalid IR%s", skipped,
                                         skipped == 1 ? "" : "s");
                                s_save_toast_timer = 2.5f;
                            }
                        }
                    }

                    /* Custom-cab controls: rename, load image, remove from library */
                    if (is_custom_active) {
                        ImGui::Dummy(ImVec2(0.0f, 6.0f));
                        int lib_idx = custom_cab_find(active_ir);

                        /* Rename input — centered, width matches dropdown */
                        {
                            float input_w = 200.0f;
                            float label_w = ImGui::CalcTextSize("Name").x;
                            float row_w = label_w + 8.0f + input_w;
                            float off = (avail_w - row_w) * 0.5f;
                            if (off > 0.0f)
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextDisabled("Name");
                            ImGui::SameLine(0, 8);
                            char name_buf[64];
                            strncpy(name_buf, fx_cab_get_custom_name(engine, cab_chain),
                                    sizeof(name_buf) - 1);
                            name_buf[sizeof(name_buf) - 1] = '\0';
                            ImGui::SetNextItemWidth(input_w);
                            char name_id[32];
                            snprintf(name_id, sizeof(name_id), "##cabname_%d", sel.chain_id);
                            if (ImGui::InputText(name_id, name_buf, sizeof(name_buf))) {
                                fx_cab_set_custom_name(engine, cab_chain, name_buf);
                                if (lib_idx >= 0) {
                                    strncpy(s_custom_cabs[lib_idx].name, name_buf,
                                            sizeof(s_custom_cabs[lib_idx].name) - 1);
                                }
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit()) {
                                custom_cabs_save();
                            }
                        }

                        ImGui::Dummy(ImVec2(0.0f, 4.0f));

                        /* Row: [Load Image...] [Remove from library] */
                        {
                            float btn_a_w = 120.0f, btn_b_w = 160.0f, gap = 8.0f;
                            float row_w = btn_a_w + gap + btn_b_w;
                            float off = (avail_w - row_w) * 0.5f;
                            if (off > 0.0f)
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
                            char img_id[32], rem_id[32];
                            snprintf(img_id, sizeof(img_id), "Load Image...##img_%d", sel.chain_id);
                            snprintf(rem_id, sizeof(rem_id), "Remove from library##rm_%d", sel.chain_id);
                            if (ImGui::Button(img_id, ImVec2(btn_a_w, 24))) {
                                char picked[1024];
                                nfdu8filteritem_t filt[1] = { { "Image", "png,jpg,jpeg" } };
                                if (open_file_picker(filt, 1, picked, sizeof(picked))) {
                                    fx_cab_set_custom_image_path(engine, cab_chain, picked);
                                    if (lib_idx >= 0) {
                                        strncpy(s_custom_cabs[lib_idx].image_path, picked,
                                                sizeof(s_custom_cabs[lib_idx].image_path) - 1);
                                        custom_cabs_save();
                                    }
                                }
                            }
                            ImGui::SameLine(0, gap);
                            if (ImGui::Button(rem_id, ImVec2(btn_b_w, 24))) {
                                if (lib_idx >= 0) {
                                    custom_cab_remove(lib_idx);
                                    custom_cabs_save();
                                }
                                load_cab_for_type(engine, cab_chain,
                                                  (fx_cab_type_t)cab_type_ref);
                            }
                        }
                    }

                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    /* Cab visual: user image if present, else procedural for custom,
                     * else stock cab texture. */
                    {
                        const char *img_path = is_custom_active
                            ? fx_cab_get_custom_image_path(engine, cab_chain) : "";
                        uintptr_t cab_tex = 0;
                        if (img_path && *img_path) {
                            cab_tex = fx_texture_load(img_path);
                        } else if (!is_custom_active) {
                            cab_tex = load_cab_texture(cab_type_ref);
                        }

                        float img_h = 220.0f;
                        float img_w = img_h;
                        if (cab_tex) {
                            int cw = 0, ch = 0;
                            if (fx_texture_get_size(cab_tex, &cw, &ch) && ch > 0) {
                                float aspect = (float)cw / (float)ch;
                                img_w = img_h * aspect;
                            }
                            float img_x = (avail_w - img_w) * 0.5f;
                            if (img_x > 0.0f)
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + img_x);
                            ImGui::Image((ImTextureID)cab_tex, ImVec2(img_w, img_h));
                        } else if (is_custom_active) {
                            /* Procedural cab — deterministic from IR path */
                            img_w = img_h * 1.1f;
                            float img_x = (avail_w - img_w) * 0.5f;
                            if (img_x > 0.0f)
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + img_x);
                            ImVec2 p0 = ImGui::GetCursorScreenPos();
                            ImVec2 p1 = ImVec2(p0.x + img_w, p0.y + img_h);
                            draw_procedural_cab(ImGui::GetWindowDrawList(), p0, p1,
                                                fnv1a(active_ir));
                            ImGui::Dummy(ImVec2(img_w, img_h));
                        }
                    }

                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    /* Bypass toggle — centered */
                    bool cab_bypassed = fx_cab_get_bypass(engine, cab_chain);
                    {
                        float btn_w = 120.0f;
                        float btn_off = (avail_w - btn_w) * 0.5f;
                        if (btn_off > 0.0f)
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btn_off);
                    }
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        cab_bypassed ? ImVec4(0.30f, 0.10f, 0.08f, 0.9f)
                                     : ImVec4(0.10f, 0.28f, 0.10f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        cab_bypassed ? ImVec4(0.42f, 0.14f, 0.10f, 1.0f)
                                     : ImVec4(0.14f, 0.40f, 0.14f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        cab_bypassed ? ImVec4(0.55f, 0.18f, 0.12f, 1.0f)
                                     : ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.78f, 0.65f, 1.0f));
                    if (ImGui::Button(cab_bypassed ? "BYPASSED##cab" : "ON (Active)##cab",
                                      ImVec2(120, 28))) {
                        fx_cab_set_bypass(engine, cab_chain, !cab_bypassed);
                    }
                    ImGui::PopStyleColor(4);
                    ImGui::PopStyleVar();
                }

                /* ── PEDAL detail view ────────────────────────── */
                else if (sel.kind == NODE_PEDAL_PRE || sel.kind == NODE_PEDAL_POST) {
                    fx_pedal_id pid = sel.pedal_id;
                    fx_pedal_type_t pt = fx_pedal_get_type(engine, pid);
                    if (pt < FX_PEDAL_TYPE_COUNT) {
                        const char *pname = fx_pedal_get_type_name(pt);
                        int nparam = fx_pedal_get_param_count(pt);
                        bool bypassed = fx_pedal_get_bypass(engine, pid);
                        float avail_w = ImGui::GetContentRegionAvail().x;

                        /* Look up category for subtitle ("Jade Drive — Overdrive") */
                        const char *pedal_category = nullptr;
                        for (int ci = 0; ci < s_pedal_category_count && !pedal_category; ci++) {
                            for (int pi = 0; pi < s_pedal_categories[ci].count; pi++) {
                                if (s_pedal_categories[ci].pedals[pi].type == pt) {
                                    pedal_category = s_pedal_categories[ci].label;
                                    break;
                                }
                            }
                        }

                        /* Title — large name + subtitle */
                        {
                            ImGui::SetWindowFontScale(1.35f);
                            ImVec2 ts = ImGui::CalcTextSize(pname);
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                            if (bypassed)
                                ImGui::TextDisabled("%s", pname);
                            else
                                ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", pname);
                            ImGui::SetWindowFontScale(1.0f);
                            if (pedal_category) {
                                char sub[64];
                                snprintf(sub, sizeof(sub), "%s \xe2\x80\x94 %s",
                                         pname, pedal_category);
                                /* Title-case the category */
                                ImVec2 sub_sz = ImGui::CalcTextSize(sub);
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                                ImGui::TextDisabled("%s", sub);
                            }
                        }
                        ImGui::Dummy(ImVec2(0.0f, 4.0f));
                        {
                            ImVec2 sep_p0 = ImGui::GetCursorScreenPos();
                            ImGui::GetWindowDrawList()->AddLine(
                                sep_p0, ImVec2(sep_p0.x + avail_w, sep_p0.y),
                                IM_COL32(180, 130, 40, 100), 1.0f);
                            ImGui::Dummy(ImVec2(0.0f, 3.0f));
                        }
                        /* Pedal body image with overlay knobs + LED */
                        {
                            /* Convert display name to filename for lookups */
                            char pedal_fname[128];
                            type_to_filename(pname, pedal_fname, sizeof(pedal_fname));
                            if (strcmp(pedal_fname, "orange_distortion") == 0)
                                strcpy(pedal_fname, "orange_dist");

                            uintptr_t pedal_tex = load_pedal_texture(pname);
                            float img_h = 220.0f;
                            float img_w = img_h;
                            if (pedal_tex) {
                                int pw = 0, ph = 0;
                                if (fx_texture_get_size(pedal_tex, &pw, &ph) && ph > 0) {
                                    float aspect = (float)pw / (float)ph;
                                    img_w = img_h * aspect;
                                }
                            }
                            float img_x = (avail_w - img_w) * 0.5f;
                            if (img_x < 0.0f) img_x = 0.0f;
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + img_x);

                            ImVec2 img_pos = ImGui::GetCursorScreenPos();
                            float cursor_after_img_y = 0; /* track where cursor should be after image */

                            if (pedal_tex) {
                                ImVec4 tint = bypassed
                                    ? ImVec4(0.6f, 0.6f, 0.6f, 0.8f)
                                    : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                                ImGui::Image((ImTextureID)pedal_tex,
                                    ImVec2(img_w, img_h),
                                    ImVec2(0, 0), ImVec2(1, 1), tint);
                            } else {
                                ImGui::Dummy(ImVec2(img_w, img_h));
                            }
                            cursor_after_img_y = ImGui::GetCursorPosY();

                            /* LED indicator on pedal image (detected from green/purple dots) */
                            {
                                static const struct { const char *name; float x, y; } s_led_pos[] = {
                                    {"amp_box",0.502f,0.451f}, {"blues_grit",0.496f,0.605f},
                                    {"carbon_delay",0.496f,0.641f}, {"chaos_fuzz",0.369f,0.729f},
                                    {"cloud_verb",0.502f,0.213f}, {"drift_vibrato",0.500f,0.547f},
                                    {"drip_verb",0.506f,0.615f}, {"echo_delay",0.496f,0.582f},
                                    {"glass_comp",0.494f,0.588f}, {"gold_drive",0.502f,0.373f},
                                    {"grit_crush",0.498f,0.565f}, {"hall_verb",0.498f,0.600f},
                                    {"howl_wah",0.635f,0.576f}, {"jade_drive",0.490f,0.576f},
                                    {"jet_flanger",0.322f,0.693f}, {"liquid_chorus",0.502f,0.330f},
                                    {"mammoth_fuzz",0.336f,0.711f}, {"memory_echo",0.348f,0.703f},
                                    {"metal_zone",0.498f,0.599f}, {"noise_gate",0.494f,0.525f},
                                    {"orange_dist",0.498f,0.537f}, {"phase_sweep",0.508f,0.445f},
                                    {"plate_verb",0.496f,0.488f}, {"pulse_trem",0.320f,0.816f},
                                    {"punch_comp",0.502f,0.545f}, {"quack_filter",0.494f,0.451f},
                                    {"ring_tone",0.497f,0.578f}, {"rodent",0.500f,0.562f},
                                    {"round_fuzz",0.645f,0.398f}, {"shimmer_verb",0.498f,0.674f},
                                    {"squeeze_box",0.359f,0.578f}, {"tape_machine",0.498f,0.600f},
                                    {"tone_sculptor",0.500f,0.621f}, {"warm_tape",0.342f,0.721f},
                                    {"wraith_fuzz",0.498f,0.525f},
                                    {"grain_cloud",0.496f,0.559f},
                                    {"infinite_hold",0.506f,0.506f},
                                    {"precision_eq",0.498f,0.541f},
                                    {"pitch_warp",0.500f,0.516f},
                                    {"octave_engine",0.498f,0.521f},
                                    {"loop_station",0.496f,0.523f},
                                };
                                /* Look up LED position for this pedal */
                                for (int li = 0; li < 41; li++) {
                                    if (strcmp(s_led_pos[li].name, pedal_fname) == 0) {
                                        float led_cx = img_pos.x + s_led_pos[li].x * img_w;
                                        float led_cy = img_pos.y + s_led_pos[li].y * img_h;
                                        ImDrawList *ldl = ImGui::GetWindowDrawList();
                                        if (!bypassed) {
                                            /* Green glow when active */
                                            ldl->AddCircleFilled(ImVec2(led_cx, led_cy), 6.0f,
                                                IM_COL32(40, 220, 40, 200), 12);
                                            ldl->AddCircleFilled(ImVec2(led_cx, led_cy), 10.0f,
                                                IM_COL32(40, 200, 40, 60), 12);
                                            ldl->AddCircleFilled(ImVec2(led_cx, led_cy), 16.0f,
                                                IM_COL32(40, 180, 40, 25), 12);
                                        } else {
                                            /* Dim red when bypassed */
                                            ldl->AddCircleFilled(ImVec2(led_cx, led_cy), 4.0f,
                                                IM_COL32(180, 40, 30, 150), 12);
                                        }
                                        break;
                                    }
                                }
                            }

                            /* Stomp switch click area (detected from purple dots) */
                            {
                                static const struct { const char *name; float x0,y0,x1,y1; } s_stomp[] = {
                                    {"amp_box",0.410f,0.652f,0.602f,0.809f},{"blues_grit",0.383f,0.656f,0.602f,0.812f},
                                    {"carbon_delay",0.395f,0.680f,0.621f,0.848f},{"chaos_fuzz",0.516f,0.570f,0.695f,0.738f},
                                    {"cloud_verb",0.387f,0.676f,0.602f,0.828f},{"drift_vibrato",0.387f,0.609f,0.609f,0.777f},
                                    {"drip_verb",0.395f,0.660f,0.617f,0.832f},{"echo_delay",0.375f,0.648f,0.613f,0.812f},
                                    {"glass_comp",0.375f,0.633f,0.613f,0.832f},{"gold_drive",0.375f,0.621f,0.621f,0.809f},
                                    {"grit_crush",0.379f,0.551f,0.629f,0.797f},{"hall_verb",0.387f,0.645f,0.617f,0.816f},
                                    {"howl_wah",0.551f,0.629f,0.719f,0.773f},{"jade_drive",0.391f,0.652f,0.598f,0.820f},
                                    {"jet_flanger",0.387f,0.625f,0.617f,0.805f},{"liquid_chorus",0.395f,0.590f,0.602f,0.777f},
                                    {"mammoth_fuzz",0.398f,0.621f,0.602f,0.777f},{"memory_echo",0.391f,0.598f,0.605f,0.777f},
                                    {"metal_zone",0.387f,0.633f,0.621f,0.816f},{"noise_gate",0.395f,0.582f,0.602f,0.754f},
                                    {"orange_dist",0.383f,0.582f,0.609f,0.781f},{"phase_sweep",0.383f,0.613f,0.582f,0.793f},
                                    {"plate_verb",0.391f,0.590f,0.609f,0.773f},{"pulse_trem",0.391f,0.660f,0.605f,0.844f},
                                    {"punch_comp",0.406f,0.613f,0.598f,0.773f},{"quack_filter",0.328f,0.551f,0.566f,0.750f},
                                    {"ring_tone",0.395f,0.621f,0.605f,0.770f},{"rodent",0.395f,0.617f,0.602f,0.785f},
                                    {"round_fuzz",0.379f,0.578f,0.621f,0.781f},{"shimmer_verb",0.395f,0.707f,0.609f,0.855f},
                                    {"squeeze_box",0.391f,0.617f,0.617f,0.789f},{"tape_machine",0.398f,0.648f,0.605f,0.812f},
                                    {"tone_sculptor",0.406f,0.684f,0.605f,0.844f},{"warm_tape",0.398f,0.629f,0.598f,0.805f},
                                    {"wraith_fuzz",0.414f,0.602f,0.594f,0.742f},
                                    {"grain_cloud",0.387f,0.609f,0.609f,0.773f},
                                    {"infinite_hold",0.414f,0.594f,0.598f,0.738f},
                                    {"precision_eq",0.414f,0.613f,0.586f,0.762f},
                                    {"pitch_warp",0.402f,0.609f,0.594f,0.766f},
                                    {"octave_engine",0.410f,0.578f,0.590f,0.727f},
                                    {"loop_station",0.398f,0.594f,0.598f,0.773f},
                                };
                                for (int si = 0; si < 41; si++) {
                                    if (strcmp(s_stomp[si].name, pedal_fname) == 0) {
                                        float sx0 = img_pos.x + s_stomp[si].x0 * img_w;
                                        float sy0 = img_pos.y + s_stomp[si].y0 * img_h;
                                        float sx1 = img_pos.x + s_stomp[si].x1 * img_w;
                                        float sy1 = img_pos.y + s_stomp[si].y1 * img_h;

                                        /* Invisible stomp button */
                                        ImGui::SetCursorScreenPos(ImVec2(sx0, sy0));
                                        char stomp_id[32];
                                        snprintf(stomp_id, sizeof(stomp_id), "##stomp_%d", (int)pid);
                                        if (ImGui::InvisibleButton(stomp_id, ImVec2(sx1-sx0, sy1-sy0))) {
                                            fx_pedal_set_bypass(engine, pid, !bypassed);
                                        }
                                        /* Hover highlight on stomp area */
                                        if (ImGui::IsItemHovered()) {
                                            ImDrawList *sdl = ImGui::GetWindowDrawList();
                                            sdl->AddRectFilled(ImVec2(sx0, sy0), ImVec2(sx1, sy1),
                                                IM_COL32(255, 255, 255, 20), 4.0f);
                                            ImGui::SetTooltip("Click to %s", bypassed ? "activate" : "bypass");
                                        }
                                        break;
                                    }
                                }
                            }

                            /* Per-pedal knob position maps (detected from red dots) */
                            struct PedalKnobMap { const char *name; int count; float pos[8][2]; };
                            static const PedalKnobMap s_pedal_knob_maps[] = {
                                { "amp_box", 6, { {0.324f,0.162f},{0.330f,0.328f},{0.504f,0.336f},{0.506f,0.158f},{0.678f,0.162f},{0.686f,0.332f} } },
                                { "blues_grit", 3, { {0.334f,0.212f},{0.502f,0.216f},{0.666f,0.218f} } },
                                { "carbon_delay", 4, { {0.363f,0.163f},{0.363f,0.339f},{0.647f,0.335f},{0.649f,0.161f} } },
                                { "chaos_fuzz", 4, { {0.357f,0.197f},{0.625f,0.195f},{0.355f,0.381f},{0.633f,0.379f} } },
                                { "cloud_verb", 5, { {0.352f,0.214f},{0.352f,0.360f},{0.498f,0.364f},{0.648f,0.212f},{0.648f,0.360f} } },
                                { "drift_vibrato", 2, { {0.381f,0.209f},{0.615f,0.207f} } },
                                { "drip_verb", 3, { {0.350f,0.202f},{0.504f,0.206f},{0.664f,0.206f} } },
                                { "echo_delay", 4, { {0.360f,0.346f},{0.362f,0.164f},{0.638f,0.158f},{0.638f,0.342f} } },
                                { "glass_comp", 4, { {0.344f,0.145f},{0.350f,0.346f},{0.650f,0.145f},{0.652f,0.346f} } },
                                { "gold_drive", 3, { {0.354f,0.182f},{0.500f,0.186f},{0.648f,0.182f} } },
                                { "grit_crush", 2, { {0.371f,0.321f},{0.623f,0.325f} } },
                                { "hall_verb", 3, { {0.327f,0.229f},{0.497f,0.225f},{0.669f,0.231f} } },
                                { "howl_wah", 2, { {0.599f,0.307f},{0.705f,0.305f} } },
                                { "jade_drive", 6, { {0.337f,0.161f},{0.347f,0.329f},{0.489f,0.167f},{0.497f,0.325f},{0.643f,0.323f},{0.659f,0.163f} } },
                                { "jet_flanger", 4, { {0.359f,0.181f},{0.365f,0.362f},{0.635f,0.179f},{0.635f,0.363f} } },
                                { "liquid_chorus", 3, { {0.345f,0.203f},{0.503f,0.203f},{0.661f,0.201f} } },
                                { "mammoth_fuzz", 3, { {0.335f,0.213f},{0.495f,0.217f},{0.657f,0.213f} } },
                                { "memory_echo", 6, { {0.323f,0.333f},{0.325f,0.163f},{0.503f,0.165f},{0.503f,0.331f},{0.677f,0.329f},{0.679f,0.163f} } },
                                { "metal_zone", 6, { {0.327f,0.171f},{0.333f,0.333f},{0.499f,0.333f},{0.501f,0.171f},{0.663f,0.173f},{0.671f,0.331f} } },
                                { "noise_gate", 4, { {0.381f,0.263f},{0.621f,0.263f},{0.377f,0.409f},{0.621f,0.407f} } },
                                { "orange_dist", 3, { {0.321f,0.201f},{0.507f,0.201f},{0.681f,0.203f} } },
                                { "phase_sweep", 4, { {0.377f,0.373f},{0.389f,0.203f},{0.679f,0.229f},{0.655f,0.393f} } },
                                { "plate_verb", 3, { {0.343f,0.229f},{0.499f,0.227f},{0.656f,0.229f} } },
                                { "pulse_trem", 6, { {0.326f,0.193f},{0.510f,0.193f},{0.693f,0.193f},{0.334f,0.391f},{0.505f,0.391f},{0.677f,0.393f} } },
                                { "punch_comp", 4, { {0.392f,0.290f},{0.609f,0.290f},{0.393f,0.442f},{0.609f,0.441f} } },
                                { "quack_filter", 4, { {0.378f,0.301f},{0.672f,0.344f},{0.352f,0.445f},{0.641f,0.483f} } },
                                { "ring_tone", 3, { {0.341f,0.228f},{0.501f,0.228f},{0.657f,0.226f} } },
                                { "rodent", 4, { {0.355f,0.247f},{0.500f,0.333f},{0.503f,0.162f},{0.648f,0.249f} } },
                                { "round_fuzz", 2, { {0.331f,0.250f},{0.670f,0.253f} } },
                                { "shimmer_verb", 4, { {0.357f,0.168f},{0.357f,0.359f},{0.647f,0.167f},{0.649f,0.356f} } },
                                { "squeeze_box", 2, { {0.378f,0.237f},{0.623f,0.236f} } },
                                { "tape_machine", 6, { {0.320f,0.180f},{0.322f,0.344f},{0.496f,0.179f},{0.498f,0.346f},{0.679f,0.182f},{0.679f,0.346f} } },
                                { "tone_sculptor", 3, { {0.318f,0.208f},{0.498f,0.209f},{0.680f,0.209f} } },
                                { "warm_tape", 3, { {0.344f,0.252f},{0.502f,0.252f},{0.659f,0.251f} } },
                                { "wraith_fuzz", 6, { {0.348f,0.214f},{0.351f,0.349f},{0.501f,0.214f},{0.501f,0.350f},{0.657f,0.345f},{0.664f,0.211f} } },
                                { "grain_cloud", 4, { {0.379f,0.242f},{0.625f,0.242f},{0.379f,0.422f},{0.621f,0.422f} } },
                                { "infinite_hold", 3, { {0.363f,0.230f},{0.508f,0.230f},{0.656f,0.230f} } },
                                { "precision_eq", 5, { {0.350f,0.193f},{0.350f,0.354f},{0.502f,0.193f},{0.502f,0.350f},{0.646f,0.197f} } },
                                { "pitch_warp", 3, { {0.361f,0.225f},{0.506f,0.225f},{0.650f,0.225f} } },
                                { "octave_engine", 4, { {0.377f,0.178f},{0.377f,0.330f},{0.623f,0.178f},{0.623f,0.330f} } },
                                { "loop_station", 2, { {0.387f,0.238f},{0.609f,0.238f} } },
                            };
                            static const int s_pedal_knob_map_count = 41;

                            const float PEDAL_KNOB_SZ = 32.0f;
                            const char *knob_tex = "resources/knobs/knob_pointer_black_nobg.png";

                            /* Look up per-pedal knob positions (pedal_fname already set above) */

                            const PedalKnobMap *pmap = nullptr;
                            for (int mi = 0; mi < s_pedal_knob_map_count; mi++) {
                                if (strcmp(s_pedal_knob_maps[mi].name, pedal_fname) == 0) {
                                    pmap = &s_pedal_knob_maps[mi];
                                    break;
                                }
                            }

                            /* Render knobs: interactive for params, static dummies for extra holes */
                            int total_holes = pmap ? pmap->count : nparam;
                            for (int k = 0; k < total_holes; k++) {
                                float kx, ky;
                                if (pmap && k < pmap->count) {
                                    kx = img_pos.x + pmap->pos[k][0] * img_w - PEDAL_KNOB_SZ * 0.5f;
                                    ky = img_pos.y + pmap->pos[k][1] * img_h - PEDAL_KNOB_SZ * 0.5f;
                                } else {
                                    float margin = img_w * 0.15f;
                                    float usable = img_w - 2.0f * margin;
                                    float sp = (nparam > 1) ? usable / (float)(nparam - 1) : 0.0f;
                                    kx = img_pos.x + margin + k * sp - PEDAL_KNOB_SZ * 0.5f;
                                    ky = img_pos.y + img_h * 0.35f - PEDAL_KNOB_SZ * 0.5f;
                                }

                                if (k < nparam) {
                                    /* Interactive knob — mapped to a parameter */
                                    const char *kname = fx_pedal_get_param_name(pt, k);
                                    float kval = fx_pedal_get_param(engine, pid, k);
                                    char kid[48];
                                    snprintf(kid, sizeof(kid), "%s##ped_ov_%d_%d",
                                             kname, (int)pid, k);
                                    if (knob_overlay(kid, &kval, 0.0f, 1.0f, 0.5f, 0.01f,
                                                     kx, ky, PEDAL_KNOB_SZ, knob_tex)) {
                                        fx_pedal_set_param(engine, pid, k, kval);
                                    }
                                } else {
                                    /* Static dummy knob — fills an extra knob hole */
                                    float dummy = 0.5f;
                                    char did[32];
                                    snprintf(did, sizeof(did), "##dummy_%d_%d", (int)pid, k);
                                    knob_overlay(did, &dummy, 0.0f, 1.0f, 0.5f, 0.0f,
                                                 kx, ky, PEDAL_KNOB_SZ, knob_tex);
                                }
                            }

                            /* Remove X overlay — top-right corner of pedal image */
                            {
                                const float XBTN_SZ = 22.0f;
                                float xbtn_x = img_pos.x + img_w - XBTN_SZ * 0.5f;
                                float xbtn_y = img_pos.y + XBTN_SZ * 0.5f;
                                /* Invisible hit area centered on corner */
                                ImGui::SetCursorScreenPos(
                                    ImVec2(xbtn_x - XBTN_SZ * 0.5f, xbtn_y - XBTN_SZ * 0.5f));
                                char xbtn_id[32];
                                snprintf(xbtn_id, sizeof(xbtn_id), "##xcorner_%d", (int)pid);
                                bool x_clicked = ImGui::InvisibleButton(xbtn_id, ImVec2(XBTN_SZ, XBTN_SZ));
                                bool x_hovered = ImGui::IsItemHovered();
                                ImDrawList *xdl = ImGui::GetWindowDrawList();
                                float xr = XBTN_SZ * 0.42f;
                                /* Background — always visible, brighter on hover */
                                xdl->AddCircleFilled(ImVec2(xbtn_x, xbtn_y), xr,
                                    x_hovered ? IM_COL32(180, 40, 30, 220) : IM_COL32(80, 20, 15, 170), 16);
                                xdl->AddCircle(ImVec2(xbtn_x, xbtn_y), xr,
                                    IM_COL32(220, 60, 40, x_hovered ? 220 : 120), 16, 1.5f);
                                /* X arms */
                                float xa = xr * 0.48f;
                                ImU32 xcol = IM_COL32(255, 210, 195, x_hovered ? 255 : 200);
                                xdl->AddLine(ImVec2(xbtn_x - xa, xbtn_y - xa),
                                             ImVec2(xbtn_x + xa, xbtn_y + xa), xcol, 2.0f);
                                xdl->AddLine(ImVec2(xbtn_x + xa, xbtn_y - xa),
                                             ImVec2(xbtn_x - xa, xbtn_y + xa), xcol, 2.0f);
                                if (x_hovered)
                                    ImGui::SetTooltip("Remove pedal");
                                if (x_clicked) {
                                    fx_chain_pos_t xpos = (sel.kind == NODE_PEDAL_PRE)
                                                           ? FX_CHAIN_POS_PRE : FX_CHAIN_POS_POST;
                                    fx_pedal_id *xids = (xpos == FX_CHAIN_POS_PRE) ? s_pre_ids : s_post_ids;
                                    int *xid_count = (xpos == FX_CHAIN_POS_PRE) ? &s_pre_id_count : &s_post_id_count;
                                    int xpi = sel.slot;
                                    fx_chain_remove_pedal(engine, pid);
                                    for (int j = xpi; j < *xid_count - 1; j++)
                                        xids[j] = xids[j + 1];
                                    (*xid_count)--;
                                    s_selected_node = -1;
                                }
                            }

                            /* Restore cursor to below the image (overlay knobs + X button moved it) */
                            ImGui::SetCursorPosY(cursor_after_img_y);
                        }

                        ImGui::Dummy(ImVec2(0.0f, 12.0f));

                        /* Bottom row: reorder arrows (bypass is on the stomp switch, remove is X on image corner) */
                        /* TODO(future): replace arrows with drag-and-drop reorder on the chain view */
                        {
                            const float ARR_SZ = 30.0f;
                            float row_w = ARR_SZ + 4 + ARR_SZ;
                            float row_off = (avail_w - row_w) * 0.5f;
                            if (row_off > 0.0f)
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + row_off);

                            /* Reorder arrows */
                            fx_chain_pos_t pos = (sel.kind == NODE_PEDAL_PRE)
                                                  ? FX_CHAIN_POS_PRE : FX_CHAIN_POS_POST;
                            fx_pedal_id *ids = (pos == FX_CHAIN_POS_PRE) ? s_pre_ids : s_post_ids;
                            int *id_count = (pos == FX_CHAIN_POS_PRE) ? &s_pre_id_count : &s_post_id_count;
                            int pi = sel.slot;

                            /* Left arrow — programmatic */
                            {
                                bool can_left = (pi > 0);
                                ImGui::PushID("##arr_l");
                                ImVec2 ap = ImGui::GetCursorScreenPos();
                                bool l_clicked = ImGui::InvisibleButton("##arr_l_click", ImVec2(ARR_SZ, ARR_SZ));
                                bool l_hovered = ImGui::IsItemHovered();
                                ImDrawList *adl = ImGui::GetWindowDrawList();
                                float acx = ap.x + ARR_SZ * 0.5f;
                                float acy = ap.y + ARR_SZ * 0.5f;
                                float ar = ARR_SZ * 0.42f;
                                ImU32 abg = (l_hovered && can_left) ? IM_COL32(60, 50, 35, 240) : IM_COL32(40, 35, 25, 200);
                                ImU32 aedge = can_left ? IM_COL32(180, 150, 80, 180) : IM_COL32(80, 70, 50, 100);
                                ImU32 afg = can_left ? IM_COL32(230, 200, 140, 240) : IM_COL32(80, 70, 50, 120);
                                adl->AddCircleFilled(ImVec2(acx, acy), ar, abg, 16);
                                adl->AddCircle(ImVec2(acx, acy), ar, aedge, 16, 1.5f);
                                /* Left arrow triangle */
                                float ta = ARR_SZ * 0.22f;
                                adl->AddTriangleFilled(
                                    ImVec2(acx - ta, acy),
                                    ImVec2(acx + ta * 0.6f, acy - ta),
                                    ImVec2(acx + ta * 0.6f, acy + ta), afg);
                                if (l_hovered && can_left) ImGui::SetTooltip("Move left");
                                if (l_clicked && can_left) {
                                    fx_pedal_id tmp = ids[pi - 1];
                                    ids[pi - 1] = ids[pi]; ids[pi] = tmp;
                                    fx_chain_move_pedal(engine, pid, pos, pi - 1);
                                    s_selected_node--;
                                }
                                ImGui::PopID();
                            }
                            ImGui::SameLine(0, 4);

                            /* Right arrow — programmatic */
                            {
                                bool can_right = (pi < *id_count - 1);
                                ImGui::PushID("##arr_r");
                                ImVec2 ap = ImGui::GetCursorScreenPos();
                                bool r_clicked = ImGui::InvisibleButton("##arr_r_click", ImVec2(ARR_SZ, ARR_SZ));
                                bool r_hovered = ImGui::IsItemHovered();
                                ImDrawList *adl = ImGui::GetWindowDrawList();
                                float acx = ap.x + ARR_SZ * 0.5f;
                                float acy = ap.y + ARR_SZ * 0.5f;
                                float ar = ARR_SZ * 0.42f;
                                ImU32 abg = (r_hovered && can_right) ? IM_COL32(60, 50, 35, 240) : IM_COL32(40, 35, 25, 200);
                                ImU32 aedge = can_right ? IM_COL32(180, 150, 80, 180) : IM_COL32(80, 70, 50, 100);
                                ImU32 afg = can_right ? IM_COL32(230, 200, 140, 240) : IM_COL32(80, 70, 50, 120);
                                adl->AddCircleFilled(ImVec2(acx, acy), ar, abg, 16);
                                adl->AddCircle(ImVec2(acx, acy), ar, aedge, 16, 1.5f);
                                /* Right arrow triangle */
                                float ta = ARR_SZ * 0.22f;
                                adl->AddTriangleFilled(
                                    ImVec2(acx + ta, acy),
                                    ImVec2(acx - ta * 0.6f, acy - ta),
                                    ImVec2(acx - ta * 0.6f, acy + ta), afg);
                                if (r_hovered && can_right) ImGui::SetTooltip("Move right");
                                if (r_clicked && can_right) {
                                    fx_pedal_id tmp = ids[pi + 1];
                                    ids[pi + 1] = ids[pi]; ids[pi] = tmp;
                                    fx_chain_move_pedal(engine, pid, pos, pi + 1);
                                    s_selected_node++;
                                }
                                ImGui::PopID();
                            }

                        }
                    }
                }

                /* ── STUDIO processor detail view ────────────── */
                else if (sel.kind == NODE_STUDIO) {
                    fx_studio_id sid = sel.pedal_id;
                    fx_studio_type_t st = fx_studio_get_type(engine, sid);
                    if (st < FX_STUDIO_COUNT) {
                        const char *sname = fx_studio_get_type_name(st);
                        int nparam = fx_studio_get_param_count(st);
                        bool bypassed = fx_studio_get_bypass(engine, sid);
                        float avail_w = ImGui::GetContentRegionAvail().x;

                        /* Title */
                        {
                            ImGui::SetWindowFontScale(1.35f);
                            ImVec2 ts = ImGui::CalcTextSize(sname);
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                            if (bypassed)
                                ImGui::TextDisabled("%s", sname);
                            else
                                ImGui::TextColored(ImVec4(0.45f, 0.65f, 0.90f, 1.0f), "%s", sname);
                            ImGui::SetWindowFontScale(1.0f);
                            ImVec2 sub_sz = ImGui::CalcTextSize("Rack Effect");
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                            ImGui::TextDisabled("Rack Effect");
                        }
                        ImGui::Dummy(ImVec2(0.0f, 4.0f));
                        {
                            ImVec2 sep_p0 = ImGui::GetCursorScreenPos();
                            ImGui::GetWindowDrawList()->AddLine(
                                sep_p0, ImVec2(sep_p0.x + avail_w, sep_p0.y),
                                IM_COL32(60, 100, 160, 100), 1.0f);
                            ImGui::Dummy(ImVec2(0.0f, 2.0f));
                        }

                        /* Rack unit image + overlay knobs */
                        {
                            static const char *rack_fnames[] = {
                                "iron_squeeze", "glass_eq", "reel_warmth", "brick_wall",
                                "velvet_press", "glue_bus", "valve_color", "precision_eq", "room_engine"
                            };

                            /* Knob position maps from red dot detection */
                            struct RackKnobMap { int count; float pos[8][2]; };
                            static const RackKnobMap rack_knob_maps[] = {
                                /* iron_squeeze: 6 (5 params + 1 dummy) */
                                { 6, { {0.150f,0.500f},{0.290f,0.500f},{0.430f,0.500f},{0.570f,0.500f},{0.710f,0.500f},{0.850f,0.500f} } },
                                /* glass_eq: 7 (6 params + 1 dummy) */
                                { 7, { {0.150f,0.500f},{0.267f,0.500f},{0.383f,0.500f},{0.500f,0.500f},{0.617f,0.500f},{0.733f,0.500f},{0.850f,0.500f} } },
                                /* reel_warmth: 5 (4 params + 1 dummy) */
                                { 5, { {0.150f,0.500f},{0.325f,0.500f},{0.500f,0.500f},{0.675f,0.500f},{0.850f,0.500f} } },
                                /* brick_wall: 3 (3 params) */
                                { 3, { {0.150f,0.500f},{0.500f,0.500f},{0.850f,0.500f} } },
                                /* velvet_press: 3 (3 params) */
                                { 3, { {0.150f,0.500f},{0.500f,0.500f},{0.850f,0.500f} } },
                                /* glue_bus: 5 (5 params) */
                                { 5, { {0.150f,0.500f},{0.325f,0.500f},{0.500f,0.500f},{0.675f,0.500f},{0.850f,0.500f} } },
                                /* valve_color: 4 (4 params) */
                                { 4, { {0.150f,0.500f},{0.383f,0.500f},{0.617f,0.500f},{0.850f,0.500f} } },
                                /* precision_eq: 6 (5 params + 1 dummy) */
                                { 6, { {0.150f,0.500f},{0.290f,0.500f},{0.430f,0.500f},{0.570f,0.500f},{0.710f,0.500f},{0.850f,0.500f} } },
                                /* room_engine: 5 (4 params + 1 dummy) */
                                { 5, { {0.150f,0.500f},{0.325f,0.500f},{0.500f,0.500f},{0.675f,0.500f},{0.850f,0.500f} } },
                            };

                            char rpath[256];
                            if (st >= 0 && st < FX_STUDIO_COUNT)
                                snprintf(rpath, sizeof(rpath), "resources/studio/%s_nobg.png", rack_fnames[st]);
                            else
                                rpath[0] = '\0';
                            uintptr_t rack_tex = fx_texture_load(rpath);

                            float img_w = 500.0f;
                            float img_h = img_w * 0.3f;
                            if (rack_tex) {
                                int rw = 0, rh = 0;
                                if (fx_texture_get_size(rack_tex, &rw, &rh) && rh > 0) {
                                    float aspect = (float)rw / (float)rh;
                                    img_h = img_w / aspect;
                                }
                            }
                            float img_x = (avail_w - img_w) * 0.5f;
                            if (img_x > 0.0f)
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + img_x);

                            ImVec2 img_pos = ImGui::GetCursorScreenPos();

                            if (rack_tex) {
                                ImVec4 tint = bypassed ? ImVec4(0.5f,0.5f,0.5f,0.7f) : ImVec4(1,1,1,1);
                                ImGui::Image((ImTextureID)rack_tex, ImVec2(img_w, img_h),
                                    ImVec2(0,0), ImVec2(1,1), tint);
                            } else {
                                ImGui::Dummy(ImVec2(img_w, img_h));
                            }
                            float cursor_after_rack_y = ImGui::GetCursorPosY();

                            /* Overlay knobs on the rack image */
                            if (st >= 0 && st < FX_STUDIO_COUNT) {
                                const RackKnobMap &rmap = rack_knob_maps[st];
                                const float RACK_KNOB_SZ = 30.0f;
                                const char *knob_tex = "resources/knobs/knob_dome_silver_nobg.png";

                                for (int k = 0; k < rmap.count; k++) {
                                    float kx = img_pos.x + rmap.pos[k][0] * img_w - RACK_KNOB_SZ * 0.5f;
                                    float ky = img_pos.y + rmap.pos[k][1] * img_h - RACK_KNOB_SZ * 0.5f;

                                    if (k < nparam) {
                                        const char *kname = fx_studio_get_param_name(st, k);
                                        float kval = fx_studio_get_param(engine, sid, k);
                                        char kid[48];
                                        snprintf(kid, sizeof(kid), "%s##rack_ov_%d_%d", kname, (int)sid, k);
                                        if (knob_overlay(kid, &kval, 0.0f, 1.0f, 0.5f, 0.01f,
                                                         kx, ky, RACK_KNOB_SZ, knob_tex)) {
                                            fx_studio_set_param(engine, sid, k, kval);
                                        }
                                    } else {
                                        float dummy = 0.5f;
                                        char did[32];
                                        snprintf(did, sizeof(did), "##rack_d_%d_%d", (int)sid, k);
                                        knob_overlay(did, &dummy, 0.0f, 1.0f, 0.5f, 0.0f,
                                                     kx, ky, RACK_KNOB_SZ, knob_tex);
                                    }
                                }
                            }
                            /* Restore cursor below image (overlay knobs moved it) */
                            ImGui::SetCursorPosY(cursor_after_rack_y);
                        }

                        ImGui::Dummy(ImVec2(0.0f, 20.0f));

                        /* Bypass + remove */
                        {
                            const float BTN_H = 28.0f;
                            float row_w = 120 + 16 + 80;
                            float row_off = (avail_w - row_w) * 0.5f;
                            if (row_off > 0.0f)
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + row_off);

                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                bypassed ? ImVec4(0.30f, 0.10f, 0.08f, 0.9f)
                                         : ImVec4(0.10f, 0.20f, 0.35f, 0.9f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                bypassed ? ImVec4(0.42f, 0.14f, 0.10f, 1.0f)
                                         : ImVec4(0.14f, 0.30f, 0.50f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                bypassed ? ImVec4(0.55f, 0.18f, 0.12f, 1.0f)
                                         : ImVec4(0.18f, 0.40f, 0.65f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.80f, 0.72f, 1.0f));
                            char bp_id[48];
                            snprintf(bp_id, sizeof(bp_id), "%s##studio_bp",
                                     bypassed ? "BYPASSED" : "ON (Active)");
                            if (ImGui::Button(bp_id, ImVec2(120, BTN_H)))
                                fx_studio_set_bypass(engine, sid, !bypassed);
                            ImGui::PopStyleColor(4);
                            ImGui::PopStyleVar();

                            ImGui::SameLine(0, 16);

                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.10f, 0.08f, 0.8f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.14f, 0.10f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.18f, 0.12f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.70f, 0.60f, 1.0f));
                            if (ImGui::Button("Remove##rm_studio", ImVec2(80, BTN_H))) {
                                fx_studio_remove(engine, sid);
                                int pi = sel.slot;
                                for (int j = pi; j < s_studio_id_count - 1; j++)
                                    s_studio_ids[j] = s_studio_ids[j + 1];
                                s_studio_id_count--;
                                s_selected_node = -1;
                            }
                            ImGui::PopStyleColor(4);
                            ImGui::PopStyleVar();
                        }
                    }
                }

                /* ── INPUT / OUTPUT — nothing to show ─────────── */
                else {
                    ImGui::TextDisabled("No editable parameters.");
                }
            }

            ImGui::End();
        }

        /* ── Status bar — level meters ────────────────────────── */
        {
            ImGui::SetNextWindowPos(ImVec2(0, win_h - STATUS_H));
            ImGui::SetNextWindowSize(ImVec2(win_w, STATUS_H));
            ImGui::Begin("##status", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

            /* ── Status bar: theme-driven dark base + inner shadow at top ── */
            {
                const fx_theme_t *th = fx_theme_get(s_theme);
                ImDrawList *dl_sb = ImGui::GetWindowDrawList();
                ImVec2 sb_min = ImGui::GetWindowPos();
                ImVec2 sb_max = ImVec2(sb_min.x + win_w, sb_min.y + STATUS_H);
                dl_sb->AddRectFilled(sb_min, sb_max,
                                     theme_col32(scale_rgb(th->bg, 0.8f)));
                /* Inner shadow at top edge — always pure black at low alpha. */
                dl_sb->AddRectFilledMultiColor(
                    sb_min, ImVec2(sb_max.x, sb_min.y + 8.0f),
                    IM_COL32(0, 0, 0, 110), IM_COL32(0, 0, 0, 110),
                    IM_COL32(0, 0, 0,   0), IM_COL32(0, 0, 0,   0));
                ImVec4 sep = th->border; sep.w = 0.8f;
                dl_sb->AddLine(
                    ImVec2(sb_min.x, sb_min.y),
                    ImVec2(sb_max.x, sb_min.y),
                    theme_col32(sep), 1.0f);
            }

            float in_level  = fx_engine_get_input_level(engine);
            float out_level = fx_engine_get_output_level(engine);

            static int s_no_signal_frames = 0;
            const float NO_SIGNAL_THRESHOLD = 0.001f;
            const int   NO_SIGNAL_FRAME_COUNT = 120;
            if (in_level < NO_SIGNAL_THRESHOLD) {
                s_no_signal_frames++;
            } else {
                s_no_signal_frames = 0;
            }
            bool no_signal = (s_no_signal_frames >= NO_SIGNAL_FRAME_COUNT);

            static float s_in_clip_timer  = 0.0f;
            static float s_out_clip_timer = 0.0f;
            float dt = io.DeltaTime;
            if (in_level  > 0.95f) s_in_clip_timer  = 0.5f;
            if (out_level > 0.95f) s_out_clip_timer = 0.5f;
            s_in_clip_timer  = s_in_clip_timer  > 0.0f ? s_in_clip_timer  - dt : 0.0f;
            s_out_clip_timer = s_out_clip_timer > 0.0f ? s_out_clip_timer - dt : 0.0f;

            ImDrawList *dl  = ImGui::GetWindowDrawList();
            ImVec2      win = ImGui::GetWindowPos();
            float bar_h     = 20.0f;
            float bar_w     = 300.0f;
            float bar_y     = win.y + (STATUS_H - bar_h) * 0.5f;

            int num_segs = 20;

            auto draw_meter = [&](float x0, float level, bool clip_flash) {
                /* Dark background panel for contrast */
                float panel_pad = 4.0f;
                dl->AddRectFilled(
                    ImVec2(x0 - panel_pad, bar_y - panel_pad),
                    ImVec2(x0 + bar_w + 11.0f + panel_pad, bar_y + bar_h + panel_pad),
                    IM_COL32(18, 16, 14, 255), 4.0f);

                /* Meter background */
                dl->AddRectFilled(ImVec2(x0, bar_y), ImVec2(x0 + bar_w, bar_y + bar_h),
                                  IM_COL32(30, 28, 24, 255), 3.0f);

                float t = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
                float seg_w     = (bar_w - (num_segs - 1) * 1.5f) / (float)num_segs;
                int   lit_segs  = (int)(t * num_segs + 0.5f);
                for (int s = 0; s < num_segs; s++) {
                    float sx0 = x0 + s * (seg_w + 1.5f);
                    float sx1 = sx0 + seg_w;
                    if (s < lit_segs) {
                        ImU32 col;
                        if (s < 14) {
                            col = IM_COL32(40, 200, 60, 255);
                        } else if (s < 18) {
                            int r = 180 + (s - 14) * 15;
                            col = IM_COL32(r, 200, 20, 255);
                        } else {
                            col = IM_COL32(230, 40, 30, 255);
                        }
                        dl->AddRectFilled(ImVec2(sx0, bar_y), ImVec2(sx1, bar_y + bar_h),
                                          col, 1.5f);
                    } else {
                        dl->AddRectFilled(ImVec2(sx0, bar_y), ImVec2(sx1, bar_y + bar_h),
                                          IM_COL32(40, 40, 36, 255), 1.5f);
                    }
                }

                /* 3dB tick marks every 3 segments */
                for (int s = 3; s < num_segs; s += 3) {
                    float tick_x = x0 + s * (seg_w + 1.5f) - 0.75f;
                    dl->AddLine(ImVec2(tick_x, bar_y),
                                ImVec2(tick_x, bar_y + 5.0f),
                                IM_COL32(120, 110, 90, 160), 1.0f);
                    dl->AddLine(ImVec2(tick_x, bar_y + bar_h - 5.0f),
                                ImVec2(tick_x, bar_y + bar_h),
                                IM_COL32(120, 110, 90, 160), 1.0f);
                }

                /* Clip indicator */
                float cx0 = x0 + bar_w + 3.0f;
                float cx1 = cx0 + 8.0f;
                ImU32 clip_col = clip_flash
                    ? IM_COL32(255, 30, 20, 255)
                    : IM_COL32(60, 20, 18, 255);
                dl->AddRectFilled(ImVec2(cx0, bar_y), ImVec2(cx1, bar_y + bar_h),
                                  clip_col, 2.0f);
            };

            /* Label font scale for larger IN/OUT text */
            float label_scale = 1.3f;

            /* Left: input meter */
            float left_x = 10.0f;
            ImGui::SetCursorPosX(left_x);
            ImGui::SetCursorPosY((STATUS_H - ImGui::GetTextLineHeight() * label_scale) * 0.5f);
            ImGui::SetWindowFontScale(label_scale);
            ImGui::TextDisabled("IN");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::SameLine(0, 6);

            ImVec2 in_meter_pos = ImGui::GetCursorScreenPos();
            in_meter_pos.y = bar_y;
            draw_meter(in_meter_pos.x, in_level, s_in_clip_timer > 0.0f);
            ImGui::Dummy(ImVec2(bar_w + 14.0f, bar_h));

            /* Input gain trim — live control next to IN meter */
            ImGui::SameLine(0, 10);
            ImGui::SetCursorPosY((STATUS_H - bar_h) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab,        ImVec4(0.80f, 0.58f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,  ImVec4(0.95f, 0.70f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg,           ImVec4(0.10f, 0.09f, 0.07f, 1.0f));
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::SliderFloat("##in_gain_sb", &s_input_gain_db, -24.0f, 12.0f, "%+.1f dB")) {
                fx_audio_set_input_gain_db(s_input_gain_db);
                s_session_cfg.input_gain_db = s_input_gain_db;
            }
            if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
                s_input_gain_db += ImGui::GetIO().MouseWheel * 0.5f;
                if (s_input_gain_db < -24.0f) s_input_gain_db = -24.0f;
                if (s_input_gain_db >  12.0f) s_input_gain_db =  12.0f;
                fx_audio_set_input_gain_db(s_input_gain_db);
                s_session_cfg.input_gain_db = s_input_gain_db;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                s_input_gain_db = 0.0f;
                fx_audio_set_input_gain_db(s_input_gain_db);
                s_session_cfg.input_gain_db = s_input_gain_db;
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Input gain trim: -24 to +12 dB\nPre-chain, stacks with PAD.\nScroll to adjust (0.5 dB/notch). Right-click to reset to 0 dB.");

            ImGui::SameLine(0, 4);
            ImGui::SetCursorPosY((STATUS_H - bar_h) * 0.5f);
            {
                const fx_theme_t *th = fx_theme_get(s_theme);
                if (s_input_pad) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        th->accent);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, th->accent_hover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  th->accent_active);
                    ImGui::PushStyleColor(ImGuiCol_Text,          th->text);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,        th->frame);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, th->frame_hover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  th->frame_active);
                    ImGui::PushStyleColor(ImGuiCol_Text,          th->text_dim);
                }
            }
            if (ImGui::Button("PAD", ImVec2(38.0f, bar_h))) {
                s_input_pad = !s_input_pad;
                fx_audio_set_input_pad(s_input_pad);
                s_session_cfg.input_pad = s_input_pad;
            }
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("-20 dB pad (stacks with slider)");

            /* Center: NO SIGNAL text */
            if (no_signal) {
                const char *ns_text = "NO SIGNAL";
                float text_w = ImGui::CalcTextSize(ns_text).x;
                float cx = (win_w - text_w) * 0.5f;
                float cy = (STATUS_H - ImGui::GetTextLineHeight()) * 0.5f;
                ImGui::SetCursorPos(ImVec2(cx, cy));
                ImGui::TextColored(ImVec4(0.55f, 0.50f, 0.40f, 0.7f), "%s", ns_text);
            }

            /* Master volume knob — between meters */
            {
                static float s_master_vol = 1.0f;
                s_master_vol = fx_engine_get_master_volume(engine);
                float mv_x = win_w * 0.5f - 25.0f;
                if (!no_signal) mv_x = win_w * 0.5f - 25.0f;
                ImGui::SetCursorPos(ImVec2(mv_x, 2.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.58f, 0.45f, 1.0f));
                ImGui::SetWindowFontScale(0.75f);
                ImGui::Text("MASTER");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                ImGui::SetCursorPos(ImVec2(mv_x - 10.0f, 16.0f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.80f, 0.58f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.95f, 0.70f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.09f, 0.07f, 1.0f));
                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::SliderFloat("##master_vol", &s_master_vol, 0.0f, 1.0f, "")) {
                    fx_engine_set_master_volume(engine, s_master_vol);
                }
                if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
                    s_master_vol += ImGui::GetIO().MouseWheel * 0.02f;
                    if (s_master_vol < 0.0f) s_master_vol = 0.0f;
                    if (s_master_vol > 1.0f) s_master_vol = 1.0f;
                    fx_engine_set_master_volume(engine, s_master_vol);
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    s_master_vol = 1.0f;
                    fx_engine_set_master_volume(engine, s_master_vol);
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Master Volume: %.0f%%\nScroll to adjust (2%%/notch). Right-click to reset to 100%%.", s_master_vol * 100.0f);
                }
            }

            /* Right: output meter */
            {
                float right_margin  = 10.0f;
                float clip_w        = 8.0f + 3.0f;
                float label_txt_w   = ImGui::CalcTextSize("OUT").x * label_scale + 6.0f;
                float out_x         = win_w - right_margin - clip_w - bar_w - label_txt_w;

                ImGui::SetCursorPos(ImVec2(out_x, (STATUS_H - ImGui::GetTextLineHeight() * label_scale) * 0.5f));
                ImGui::SetWindowFontScale(label_scale);
                ImGui::TextDisabled("OUT");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::SameLine(0, 6);

                ImVec2 out_meter_pos = ImGui::GetCursorScreenPos();
                out_meter_pos.y = bar_y;
                draw_meter(out_meter_pos.x, out_level, s_out_clip_timer > 0.0f);
                ImGui::Dummy(ImVec2(bar_w + clip_w, bar_h));
            }

            ImGui::End();
        }

        /* ── Window border + resize grip (borderless) ──────── */
        {
            ImDrawList *fg = ImGui::GetForegroundDrawList();
            ImU32 border_col = IM_COL32(50, 45, 38, 200);
            fg->AddRect(ImVec2(0, 0),
                        ImVec2(win_w, win_h),
                        border_col, 0.0f, 0, 2.0f);
            float gs = 14.0f;
            float bx = win_w, by = win_h;
            ImU32 grip_col = IM_COL32(80, 72, 58, 180);
            fg->AddTriangleFilled(
                ImVec2(bx - gs, by), ImVec2(bx, by - gs), ImVec2(bx, by), grip_col);
        }

        /* Corner dirt vignettes — drawn over the "content region" only
         * (signal chain + detail view), not over the toolbar, looper
         * strip, or status bar. Uses the foreground drawlist so it sits
         * above widgets, but alpha-blends so they remain legible. */
        {
            float dirt_top = TOOLBAR_H;
            if (s_looper_panel_open) dirt_top += LOOPER_H;
            float dirt_bot = win_h - STATUS_H;
            if (dirt_bot > dirt_top) {
                fx_theme_draw_corner_dirt(ImGui::GetForegroundDrawList(),
                                          0.0f, dirt_top,
                                          win_w, dirt_bot - dirt_top,
                                          s_theme);
            }
        }

        /* Re-publish toolbar widget-hover state for the SDL hit-test. The
         * toolbar End() already set this based on toolbar-local items, but
         * popups/child windows render AFTER the toolbar and may overlap it.
         * If any popup is open, disable OS window drag across the whole
         * toolbar strip — the click either targets the popup (if hovered)
         * or dismisses it (if outside). Either way, the system window-drag
         * gesture must not consume it. Coarser than tracking HoveredWindow
         * directly but uses only the public ImGui API. */
        if (ImGui::IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopupId |
                                     ImGuiPopupFlags_AnyPopupLevel)) {
            g_toolbar_pointer_on_widget = true;
        }

        /* Render */
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.06f, 0.05f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    /* Auto-save last session preset and config */
    {
        bool ok = fx_preset_save(engine, "presets/last_session.0xfx");
        if (!ok) fx_preset_save(engine, "../presets/last_session.0xfx");
        FX_INFO(ok ? "Session saved to last_session.0xfx" : "Could not auto-save session");
    }
    {
        /* Snapshot window size before destruction */
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        s_session_cfg.window_w          = ww;
        s_session_cfg.window_h          = wh;
        s_session_cfg.input_device_idx  = s_selected_input;
        s_session_cfg.output_device_idx = s_selected_output;
        s_session_cfg.buf_size_idx      = s_selected_buf_idx;
        s_session_cfg.sr_idx            = s_selected_sr_idx;
        s_session_cfg.input_gain_db     = s_input_gain_db;
        s_session_cfg.input_pad         = s_input_pad;
        s_session_cfg.theme_id          = (int)s_theme;
        session_config_save(&s_session_cfg);
        FX_INFO("Config saved to %s", get_config_path());
    }

    /* Cleanup */
    fx_texture_shutdown();
    fx_midi_shutdown();
    fx_audio_shutdown();
    fx_engine_destroy(engine);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    NFD_Quit();
    SDL_Quit();

    fx_log_shutdown();
    return 0;
}
