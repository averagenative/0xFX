/*
 * custom_cabs.cpp — Shared user library of custom cab IRs.
 *
 * See custom_cabs.h for the public API contract.
 *
 * Internally owns: a single array of entries, a mutex that protects every
 * read and write, and a "loaded" flag so lazy-load is safe from multiple
 * plugin-GUI threads racing into the library at startup.
 */
#include "custom_cabs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>

extern "C" {
#include "cJSON.h"
#include "../core/log.h"
}

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define CC_PATH_SEP "\\"
static const char *cc_config_dir(void) {
    static char buf[512];
    const char *appdata = getenv("APPDATA");
    if (!appdata) appdata = ".";
    snprintf(buf, sizeof(buf), "%s\\0xFX", appdata);
    return buf;
}
static void cc_ensure_dir(const char *path) { CreateDirectoryA(path, NULL); }
#else
#include <sys/stat.h>
#include <sys/types.h>
#define CC_PATH_SEP "/"
static const char *cc_config_dir(void) {
    static char buf[512];
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(buf, sizeof(buf), "%s/.0xfx", home);
    return buf;
}
static void cc_ensure_dir(const char *path) { mkdir(path, 0755); }
#endif

static const char *cc_library_path(void) {
    static char buf[1024];
    snprintf(buf, sizeof(buf), "%s%scustom_cabs.json",
             cc_config_dir(), CC_PATH_SEP);
    return buf;
}

static std::mutex        s_cc_mutex;
static fx_custom_cab_t   s_cc_entries[FX_MAX_CUSTOM_CABS];
static int               s_cc_count   = 0;
static bool              s_cc_loaded  = false;

static void cc_basename_no_ext(const char *path, char *out, size_t out_sz) {
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

/* Caller holds s_cc_mutex. */
static void cc_load_locked(void) {
    s_cc_count = 0;
    FILE *fp = fopen(cc_library_path(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > (1 << 20)) { fclose(fp); return; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, (size_t)sz, fp);
    buf[sz] = '\0';
    fclose(fp);
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        return;
    }
    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n && s_cc_count < FX_MAX_CUSTOM_CABS; i++) {
        cJSON *it = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsObject(it)) continue;
        cJSON *p   = cJSON_GetObjectItem(it, "ir_path");
        cJSON *nm  = cJSON_GetObjectItem(it, "name");
        cJSON *img = cJSON_GetObjectItem(it, "image_path");
        if (!cJSON_IsString(p) || !p->valuestring || !*p->valuestring) continue;
        fx_custom_cab_t &e = s_cc_entries[s_cc_count++];
        memset(&e, 0, sizeof(e));
        strncpy(e.ir_path, p->valuestring, sizeof(e.ir_path) - 1);
        if (cJSON_IsString(nm) && nm->valuestring)
            strncpy(e.name, nm->valuestring, sizeof(e.name) - 1);
        else
            cc_basename_no_ext(e.ir_path, e.name, sizeof(e.name));
        if (cJSON_IsString(img) && img->valuestring)
            strncpy(e.image_path, img->valuestring, sizeof(e.image_path) - 1);
    }
    cJSON_Delete(root);
    FX_INFO("Custom cabs: loaded %d from %s", s_cc_count, cc_library_path());
}

extern "C" void fx_custom_cabs_load(void) {
    std::lock_guard<std::mutex> g(s_cc_mutex);
    cc_load_locked();
    s_cc_loaded = true;
}

static void cc_ensure_loaded_locked(void) {
    if (!s_cc_loaded) {
        cc_load_locked();
        s_cc_loaded = true;
    }
}

extern "C" void fx_custom_cabs_save(void) {
    std::lock_guard<std::mutex> g(s_cc_mutex);
    cc_ensure_loaded_locked();
    cc_ensure_dir(cc_config_dir());
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s_cc_count; i++) {
        cJSON *it = cJSON_CreateObject();
        cJSON_AddStringToObject(it, "ir_path",    s_cc_entries[i].ir_path);
        cJSON_AddStringToObject(it, "name",       s_cc_entries[i].name);
        cJSON_AddStringToObject(it, "image_path", s_cc_entries[i].image_path);
        cJSON_AddItemToArray(arr, it);
    }
    char *out = cJSON_Print(arr);
    cJSON_Delete(arr);
    if (!out) return;
    FILE *fp = fopen(cc_library_path(), "wb");
    if (fp) { fwrite(out, 1, strlen(out), fp); fclose(fp); }
    free(out);
}

extern "C" int fx_custom_cabs_snapshot(fx_custom_cab_t *out, int max_entries) {
    if (!out || max_entries <= 0) return 0;
    std::lock_guard<std::mutex> g(s_cc_mutex);
    cc_ensure_loaded_locked();
    int n = s_cc_count < max_entries ? s_cc_count : max_entries;
    memcpy(out, s_cc_entries, (size_t)n * sizeof(fx_custom_cab_t));
    return n;
}

extern "C" int fx_custom_cabs_find(const char *ir_path) {
    if (!ir_path) return -1;
    std::lock_guard<std::mutex> g(s_cc_mutex);
    cc_ensure_loaded_locked();
    for (int i = 0; i < s_cc_count; i++) {
        if (strcmp(s_cc_entries[i].ir_path, ir_path) == 0) return i;
    }
    return -1;
}

extern "C" int fx_custom_cabs_add(const char *ir_path) {
    if (!ir_path || !*ir_path) return -1;
    std::lock_guard<std::mutex> g(s_cc_mutex);
    cc_ensure_loaded_locked();
    for (int i = 0; i < s_cc_count; i++) {
        if (strcmp(s_cc_entries[i].ir_path, ir_path) == 0) return i;
    }
    if (s_cc_count >= FX_MAX_CUSTOM_CABS) {
        FX_WARN("Custom cab library full (%d) — cannot add %s",
                FX_MAX_CUSTOM_CABS, ir_path);
        return -1;
    }
    fx_custom_cab_t &e = s_cc_entries[s_cc_count];
    memset(&e, 0, sizeof(e));
    strncpy(e.ir_path, ir_path, sizeof(e.ir_path) - 1);
    cc_basename_no_ext(ir_path, e.name, sizeof(e.name));
    return s_cc_count++;
}

extern "C" void fx_custom_cabs_remove_by_index(int idx) {
    std::lock_guard<std::mutex> g(s_cc_mutex);
    cc_ensure_loaded_locked();
    if (idx < 0 || idx >= s_cc_count) return;
    for (int i = idx; i < s_cc_count - 1; i++)
        s_cc_entries[i] = s_cc_entries[i + 1];
    s_cc_count--;
    memset(&s_cc_entries[s_cc_count], 0, sizeof(fx_custom_cab_t));
}

extern "C" void fx_custom_cabs_remove_by_path(const char *ir_path) {
    if (!ir_path) return;
    std::lock_guard<std::mutex> g(s_cc_mutex);
    cc_ensure_loaded_locked();
    for (int i = 0; i < s_cc_count; i++) {
        if (strcmp(s_cc_entries[i].ir_path, ir_path) == 0) {
            for (int j = i; j < s_cc_count - 1; j++)
                s_cc_entries[j] = s_cc_entries[j + 1];
            s_cc_count--;
            memset(&s_cc_entries[s_cc_count], 0, sizeof(fx_custom_cab_t));
            return;
        }
    }
}

extern "C" bool fx_custom_cabs_set_name(const char *ir_path, const char *name) {
    if (!ir_path || !name) return false;
    std::lock_guard<std::mutex> g(s_cc_mutex);
    cc_ensure_loaded_locked();
    for (int i = 0; i < s_cc_count; i++) {
        if (strcmp(s_cc_entries[i].ir_path, ir_path) == 0) {
            strncpy(s_cc_entries[i].name, name, sizeof(s_cc_entries[i].name) - 1);
            s_cc_entries[i].name[sizeof(s_cc_entries[i].name) - 1] = '\0';
            return true;
        }
    }
    return false;
}

extern "C" bool fx_custom_cabs_set_image(const char *ir_path, const char *image_path) {
    if (!ir_path || !image_path) return false;
    std::lock_guard<std::mutex> g(s_cc_mutex);
    cc_ensure_loaded_locked();
    for (int i = 0; i < s_cc_count; i++) {
        if (strcmp(s_cc_entries[i].ir_path, ir_path) == 0) {
            strncpy(s_cc_entries[i].image_path, image_path, sizeof(s_cc_entries[i].image_path) - 1);
            s_cc_entries[i].image_path[sizeof(s_cc_entries[i].image_path) - 1] = '\0';
            return true;
        }
    }
    return false;
}
