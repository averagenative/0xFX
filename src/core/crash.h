#ifndef FX_CRASH_H
#define FX_CRASH_H

/*
 * fx_crash_init — register signal/exception handlers for crash reporting.
 *
 * On POSIX: installs handlers for SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS.
 * On Windows: installs SetUnhandledExceptionFilter.
 *
 * Must be called after fx_log_init() so the logger is available.
 */
void fx_crash_init(void);

#endif /* FX_CRASH_H */
