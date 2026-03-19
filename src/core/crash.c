/*
 * 0xFX — Signal-based crash handler
 *
 * On POSIX: catches SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS,
 *           logs the signal, prints a backtrace (Linux), then re-raises.
 * On Windows: uses SetUnhandledExceptionFilter.
 */

#include "crash.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

/* ── POSIX implementation ───────────────────────────────────────── */
#ifndef _WIN32

#include <signal.h>
#include <stdio.h>

/* backtrace() is a GNU/glibc extension available on Linux */
#if defined(__linux__)
#  include <execinfo.h>
#  define FX_HAS_BACKTRACE 1
#else
#  define FX_HAS_BACKTRACE 0
#endif

typedef struct {
    int         signum;
    const char *name;
} sig_entry_t;

static const sig_entry_t k_signals[] = {
    { SIGSEGV, "SIGSEGV (Segmentation fault)"    },
    { SIGABRT, "SIGABRT (Abort)"                 },
    { SIGFPE,  "SIGFPE  (Floating-point exception)" },
    { SIGILL,  "SIGILL  (Illegal instruction)"   },
    { SIGBUS,  "SIGBUS  (Bus error)"             },
};
#define K_SIGNALS_COUNT ((int)(sizeof(k_signals) / sizeof(k_signals[0])))

static const char *signal_name(int signum) {
    for (int i = 0; i < K_SIGNALS_COUNT; i++) {
        if (k_signals[i].signum == signum) return k_signals[i].name;
    }
    return "Unknown signal";
}

static void crash_handler(int signum) {
    /* Log signal */
    FX_FATAL("*** CRASH: signal %d — %s ***", signum, signal_name(signum));

#if FX_HAS_BACKTRACE
    /* Print backtrace to stderr (async-signal-safe: write() only) */
    void *frames[64];
    int   nframes = backtrace(frames, 64);
    /* backtrace_symbols_fd is async-signal-safe */
    backtrace_symbols_fd(frames, nframes, 2 /* stderr fd */);
#endif

    /* Flush the log file if any */
    fx_log_shutdown();

    /* Restore default action and re-raise so the OS can generate a core dump */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(signum, &sa, NULL);
    raise(signum);
}

void fx_crash_init(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_handler;
    sigemptyset(&sa.sa_mask);
    /* SA_RESETHAND: restore default after first delivery (belt-and-suspenders) */
    sa.sa_flags = SA_RESETHAND;

    for (int i = 0; i < K_SIGNALS_COUNT; i++) {
        sigaction(k_signals[i].signum, &sa, NULL);
    }

    FX_DEBUG("Crash handler installed (SIGSEGV SIGABRT SIGFPE SIGILL SIGBUS)");
}

/* ── Windows implementation ─────────────────────────────────────── */
#else  /* _WIN32 */

#include <windows.h>

static const char *exception_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
        default:                                 return "Unknown exception";
    }
}

static LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    FX_FATAL("*** CRASH: Windows exception 0x%08lX — %s ***",
             (unsigned long)code, exception_name(code));
    fx_log_shutdown();
    return EXCEPTION_CONTINUE_SEARCH; /* let the OS handle it (default behavior) */
}

void fx_crash_init(void) {
    SetUnhandledExceptionFilter(unhandled_exception_filter);
    FX_DEBUG("Crash handler installed (SetUnhandledExceptionFilter)");
}

#endif /* _WIN32 */
