#ifndef SC_LOG_H
#define SC_LOG_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdio.h>

#include "platform/tls.h"

#include "defs.h"

/**
 * @file
 * Log utility functions.
 */


/// system is unusable
#define SC_LOG_EMERG    0
/// action must be taken immediately
#define SC_LOG_ALERT    1
/// fatal conditions
#define SC_LOG_CRIT     2
/// error conditions
#define SC_LOG_ERROR    3
/// warning conditions
#define SC_LOG_WARNING  4
/// normal but significant condition
#define SC_LOG_NOTICE   5
/// informational messages
#define SC_LOG_INFO     6
/// debug-level messages
#define SC_LOG_DEBUG    7
/// more debug messages
#define SC_LOG_VERBOSE  8
/// number of log levels
#define SC_LOGLVL_COUNT 9

PLZJ_API
/// name of log levels
extern const char *const sc_log_level_names[SC_LOGLVL_COUNT];


__nonnull((2)) __attr_access((__read_only__, 2)) __attr_format((printf, 2, 3))
typedef int (*sc_print_fn_t) (void *data, const char *format, ...);
__nonnull((2)) __attr_access((__read_only__, 2)) __attr_format((printf, 2, 0))
typedef int (*sc_vprint_fn_t) (void *data, const char *format, va_list arg);
__nonnull((2)) __attr_access((__read_only__, 2))
typedef int (*sc_logevent_fn_t) (
  void *data, const char *log_domain, unsigned int log_level);


PLZJ_API
/// log function user data
extern void *sc_log_fn_data;

PLZJ_API
/**
 * @brief Initialize a log event and print heading of a log event.
 *
 * @note This function does not check @p log_level against @c sc_log_level .
 *
 * @param data Function data @ref sc_log_fn_data .
 * @param log_domain Log domain. Can be @c NULL .
 * @param log_level Log level.
 * @return Number of bytes written, or negative error code.
 */
extern sc_logevent_fn_t sc_log_begin_fn;
PLZJ_API
/**
 * @brief Append message to log buffer.
 *
 * @param data Function data @ref sc_log_fn_data .
 * @param format Format string.
 * @param arg Variable argument list.
 * @return Number of bytes written, or negative error code.
 */
extern sc_vprint_fn_t sc_log_vprint_fn;
PLZJ_API __nonnull((2)) __attr_access((__read_only__, 2))
__attr_format((printf, 2, 3))
/**
 * @brief Append message to log buffer.
 *
 * @param data Function data @ref sc_log_fn_data .
 * @param format Format string.
 * @param ... Format arguments.
 * @return Number of bytes written, or negative error code.
 */
int sc_log_print_fn (void *data, const char *format, ...);
PLZJ_API
/**
 * @brief End a log event and print log buffer.
 *
 * @note This function does not check @p log_level against @c sc_log_level .
 *
 * @param data Function data @ref sc_log_fn_data .
 * @param log_domain Log domain. Can be @c NULL .
 * @param log_level Log level.
 * @return Number of bytes written, or negative error code.
 */
extern sc_logevent_fn_t sc_log_end_fn;
PLZJ_API __attr_access((__read_only__, 2)) __attr_access((__read_only__, 4))
__attr_format((printf, 4, 0))
/**
 * @brief Write a log message.
 *
 * @note This function does not check @p log_level against @c sc_log_level .
 *
 * @param data Function data @ref sc_log_fn_data .
 * @param log_domain Log domain. Can be @c NULL .
 * @param log_level Log level.
 * @param format Format string.
 * @param arg Variable argument list.
 * @return Number of bytes written, or negative error code.
 */
extern int (*sc_vlog_fn) (
  void *data, const char *log_domain, unsigned int log_level,
  const char *format, va_list arg);


PLZJ_API
/// current log level, level above this (exclusive) will be ignored
extern unsigned int sc_log_level;

#define sc_log_begin(log_level) \
  ((log_level) > sc_log_level ? 0 : ((sc_log_begin_fn == NULL ? 0 : \
    sc_log_begin_fn(sc_log_fn_data, SC_LOG_DOMAIN, (log_level))), 1))
#define sc_log_print(...) sc_log_print_fn(sc_log_fn_data, __VA_ARGS__)
#define sc_log_end(log_level) (sc_log_end_fn == NULL ? 0 : \
    sc_log_end_fn(sc_log_fn_data, SC_LOG_DOMAIN, (log_level)))

PLZJ_API __nonnull((3)) __attr_access((__read_only__, 1))
__attr_access((__read_only__, 3)) __attr_format((printf, 3, 0))
/**
 * @brief Write a log message.
 *
 * @param log_domain Log domain. Can be @c NULL .
 * @param log_level Log level.
 * @param format Format string.
 * @param arg Variable argument list.
 * @return Number of bytes written, or negative error code.
 */
int sc_vlog (
  const char *log_domain, unsigned int log_level,
  const char *format, va_list arg);
PLZJ_API __nonnull((3)) __attr_access((__read_only__, 1))
__attr_access((__read_only__, 3)) __attr_format((printf, 3, 4))
/**
 * @brief Write a log message.
 *
 * @param log_domain Log domain. Can be @c NULL .
 * @param log_level Log level.
 * @param format Format string.
 * @param ... Format arguments.
 * @return Number of bytes written, or negative error code.
 */
int sc_log (
  const char *log_domain, unsigned int log_level, const char *format, ...);

#define SC_LOG(log_domain, log_level, ...) \
  ((log_level) > sc_log_level ? 0 : sc_log(log_domain, log_level, __VA_ARGS__))

#define sc_emerg(...) SC_LOG(SC_LOG_DOMAIN, SC_LOG_EMERG, __VA_ARGS__)
#define sc_alert(...) SC_LOG(SC_LOG_DOMAIN, SC_LOG_ALERT, __VA_ARGS__)
#define sc_crit(...) SC_LOG(SC_LOG_DOMAIN, SC_LOG_CRIT, __VA_ARGS__)
#define sc_error(...) SC_LOG(SC_LOG_DOMAIN, SC_LOG_ERROR, __VA_ARGS__)
#define sc_warning(...) SC_LOG(SC_LOG_DOMAIN, SC_LOG_WARNING, __VA_ARGS__)
#define sc_notice(...) SC_LOG(SC_LOG_DOMAIN, SC_LOG_NOTICE, __VA_ARGS__)
#define sc_info(...) SC_LOG(SC_LOG_DOMAIN, SC_LOG_INFO, __VA_ARGS__)
#define sc_debug(...) SC_LOG(SC_LOG_DOMAIN, SC_LOG_DEBUG, __VA_ARGS__)
#define sc_verbose(...) SC_LOG(SC_LOG_DOMAIN, SC_LOG_VERBOSE, __VA_ARGS__)


struct ScLogger {
  const char *log_domain;
  unsigned int log_level;
};

PLZJ_API __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2)) __attr_format((printf, 2, 0))
/**
 * @brief Write a log message.
 *
 * @note This function does not check @p log_level against @c sc_log_level .
 *
 * @param data Log arguments @ref ScLogger .
 * @param format Format string.
 * @param arg Variable argument list.
 * @return Number of bytes written, or negative error code.
 */
int sc_vlogger (void *data, const char *format, va_list arg);
PLZJ_API __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2)) __attr_format((printf, 2, 3))
/**
 * @brief Write a log message.
 *
 * @note This function does not check @p log_level against @c sc_log_level .
 *
 * @param data Log arguments @ref ScLogger .
 * @param format Format string.
 * @param ... Format arguments.
 * @return Number of bytes written, or negative error code.
 */
int sc_logger (void *data, const char *format, ...);


PLZJ_API __THROW __attribute_noinline__ __attr_access((__write_only__, 1, 2))
int sc_save_backtrace (void **frames, int size, int skip);
PLZJ_API __nonnull((1, 3)) __attr_access((__read_only__, 1, 2))
int sc_print_backtrace_f (
  void *const *frames, int size, sc_print_fn_t printer, void *data,
  const char *indent);
PLZJ_API __nonnull((1, 3)) __attr_access((__read_only__, 1, 2))
int sc_print_backtrace (
  void *const *frames, int size, FILE *out, const char *indent);


struct ScException {
  unsigned long domain;
  int code;
  const char *fn;

  const char *what;
  union {
    void *userdata;
    char str[128];
    unsigned char buf[128];
  };

  void *frames[32];
  int frames_len;
};

PLZJ_API
extern thread_local struct ScException sc_exc;

PLZJ_API __nonnull((1, 2)) __attr_access((__read_only__, 1))
__attr_access((__read_only__, 4)) __attr_access((__read_only__, 5))
int ScException_print (
  const struct ScException *exc, sc_print_fn_t printer, void *data,
  const char *indent, const char *bt_indent);
PLZJ_API __attribute_noinline__ __nonnull((1))
__attr_access((__read_only__, 1)) __attr_access((__read_only__, 2))
__attr_format((printf, 2, 3))
int ScException_stderr (
  const struct ScException *exc, const char *format, ...);
PLZJ_API __nonnull((1)) __attr_access((__read_only__, 2))
__attr_access((__read_only__, 3))
int sc_print_err (FILE *out, const char *indent, const char *bt_indent);


__attribute_warn_unused_result__
typedef const char *(*sc_strerror_fn_t) (int code);
__nonnull((1, 2, 4)) __attr_access((__read_only__, 1))
__attr_access((__read_only__, 4))
typedef int (*sc_print_err_fn_t) (
  const struct ScException *exc, sc_print_fn_t printer, void *data,
  const char *indent);

PLZJ_API __THROW
int sc_register_exc_handler (
  unsigned long domain, sc_strerror_fn_t strfn, sc_print_err_fn_t printfn);


PLZJ_API __THROW __attribute_noinline__ __attr_access((__read_only__, 3))
__attr_access((__read_only__, 4))
int sc_set_err (
  unsigned long domain, int code, const char *fn, const char *what);
PLZJ_API __THROW __attribute_noinline__ __attr_access((__read_only__, 3))
__attr_access((__read_only__, 4)) __attr_format((gnu_printf, 4, 5))
int sc_set_err_fmt (
  unsigned long domain, int code, const char *fn, const char *format, ...);

PLZJ_API __THROW __attribute_noinline__ __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int sc_set_errno (const char *fn, const char *what);

#ifdef _WIN32

PLZJ_API __THROW __attribute_noinline__ __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int sc_set_err_win32 (const char *fn, const char *what);

#endif

#ifdef __cplusplus
}
#endif

#endif /* SC_LOG_H */
