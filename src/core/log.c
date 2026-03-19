/*
 * 0xFX — Logging implementation
 *
 * Thread-safe logger with millisecond timestamps, level filtering,
 * color output to stderr, and optional file output.
 */

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <pthread.h>
#endif

/* ── Internal state ─────────────────────────────────────────────── */

static fx_log_level_t  s_min_level  = FX_LOG_DEBUG;
static FILE           *s_log_file   = NULL;

#ifdef _WIN32
static CRITICAL_SECTION s_lock;
static int              s_lock_init = 0;
#else
static pthread_mutex_t  s_lock      = PTHREAD_MUTEX_INITIALIZER;
#endif

/* ── ANSI color codes (stderr only) ────────────────────────────── */

#define ANSI_RESET      "\033[0m"
#define ANSI_GRAY       "\033[90m"
#define ANSI_DEFAULT    ""
#define ANSI_YELLOW     "\033[33m"
#define ANSI_RED        "\033[31m"
#define ANSI_RED_BOLD   "\033[1;31m"

static const char *level_color(fx_log_level_t level) {
    switch (level) {
        case FX_LOG_DEBUG: return ANSI_GRAY;
        case FX_LOG_INFO:  return ANSI_DEFAULT;
        case FX_LOG_WARN:  return ANSI_YELLOW;
        case FX_LOG_ERROR: return ANSI_RED;
        case FX_LOG_FATAL: return ANSI_RED_BOLD;
        default:           return ANSI_DEFAULT;
    }
}

static const char *level_name(fx_log_level_t level) {
    switch (level) {
        case FX_LOG_DEBUG: return "DEBUG";
        case FX_LOG_INFO:  return "INFO ";
        case FX_LOG_WARN:  return "WARN ";
        case FX_LOG_ERROR: return "ERROR";
        case FX_LOG_FATAL: return "FATAL";
        default:           return "?????";
    }
}

/* ── Timestamp ──────────────────────────────────────────────────── */

/*
 * Writes "[YYYY-MM-DD HH:MM:SS.mmm]" into buf (must be >= 32 bytes).
 */
static void get_timestamp(char *buf, size_t buflen) {
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(buf, buflen, "[%04d-%02d-%02d %02d:%02d:%02d.%03d]",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);
    int ms = (int)(ts.tv_nsec / 1000000);
    snprintf(buf, buflen, "[%04d-%02d-%02d %02d:%02d:%02d.%03d]",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
             ms);
#endif
}

/* ── Locking helpers ────────────────────────────────────────────── */

static void lock(void) {
#ifdef _WIN32
    if (s_lock_init) EnterCriticalSection(&s_lock);
#else
    pthread_mutex_lock(&s_lock);
#endif
}

static void unlock(void) {
#ifdef _WIN32
    if (s_lock_init) LeaveCriticalSection(&s_lock);
#else
    pthread_mutex_unlock(&s_lock);
#endif
}

/* ── Public API ─────────────────────────────────────────────────── */

void fx_log_init(const char *log_file_path) {
#ifdef _WIN32
    if (!s_lock_init) {
        InitializeCriticalSection(&s_lock);
        s_lock_init = 1;
    }
#endif

    lock();
    if (log_file_path) {
        FILE *f = fopen(log_file_path, "a");
        if (f) {
            s_log_file = f;
        } else {
            /* Couldn't open log file — carry on with stderr only */
            fprintf(stderr, "[WARN ] fx_log_init: could not open log file: %s\n",
                    log_file_path);
        }
    }
    unlock();
}

void fx_log_shutdown(void) {
    lock();
    if (s_log_file) {
        fclose(s_log_file);
        s_log_file = NULL;
    }
    unlock();

#ifdef _WIN32
    if (s_lock_init) {
        DeleteCriticalSection(&s_lock);
        s_lock_init = 0;
    }
#endif
}

void fx_log_set_level(fx_log_level_t level) {
    lock();
    s_min_level = level;
    unlock();
}

void fx_log(fx_log_level_t level, const char *file, int line,
            const char *fmt, ...) {
    if (level < s_min_level) return;

    /* Extract just the basename from __FILE__ */
    const char *basename = file ? file : "?";
    const char *slash = strrchr(basename, '/');
    if (!slash) slash = strrchr(basename, '\\');
    if (slash) basename = slash + 1;

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    /* Format the user message into a local buffer */
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    lock();

    /* Stderr — with ANSI color */
    const char *color = level_color(level);
    if (color[0] != '\0') {
        fprintf(stderr, "%s%s [%s] [%s:%d] %s%s\n",
                color, timestamp, level_name(level), basename, line,
                msg, ANSI_RESET);
    } else {
        fprintf(stderr, "%s [%s] [%s:%d] %s\n",
                timestamp, level_name(level), basename, line, msg);
    }

    /* Log file — no color codes */
    if (s_log_file) {
        fprintf(s_log_file, "%s [%s] [%s:%d] %s\n",
                timestamp, level_name(level), basename, line, msg);
        fflush(s_log_file);
    }

    unlock();
}
