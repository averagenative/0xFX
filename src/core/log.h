#ifndef FX_LOG_H
#define FX_LOG_H

typedef enum {
    FX_LOG_DEBUG = 0,
    FX_LOG_INFO,
    FX_LOG_WARN,
    FX_LOG_ERROR,
    FX_LOG_FATAL
} fx_log_level_t;

void fx_log_init(const char *log_file_path);  /* NULL = stderr only */
void fx_log_shutdown(void);
void fx_log_set_level(fx_log_level_t level);
void fx_log(fx_log_level_t level, const char *file, int line, const char *fmt, ...);

#define FX_DEBUG(...) fx_log(FX_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define FX_INFO(...)  fx_log(FX_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define FX_WARN(...)  fx_log(FX_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define FX_ERROR(...) fx_log(FX_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define FX_FATAL(...) fx_log(FX_LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#endif /* FX_LOG_H */
