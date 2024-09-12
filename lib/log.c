#include <errno.h>
#include <execinfo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#include "include/log.h"
#include "macro.h"
#include "color.h"

#ifndef SC_WEAK
  #define SC_WEAK __attribute__((weak))
#endif

#define SC_LOG_DOMAIN "sc"


SC_WEAK const char *const sc_log_level_names[SC_LOGLVL_COUNT] = {
  "EMERG", "ALERT", "CRIT", "ERR",
  "WARNING", "NOTICE", "INFO", "DEBUG", "VERBOSE",
};


static const char *const sc_log_level_prefixs[SC_LOGLVL_COUNT] = {
  "5;1;" COLOR_CODE(COLOR_BG_RED) ";"
    COLOR_CODE(COLOR_FG_BRIGHT_WHITE),
  "1;" COLOR_CODE(COLOR_BG_RED) ";"
    COLOR_CODE(COLOR_FG_BRIGHT_WHITE),
  "1;" COLOR_CODE(COLOR_FG_RED),
  "1;" COLOR_CODE(COLOR_FG_RED),
  "1;" COLOR_CODE(COLOR_FG_YELLOW),
  "1;" COLOR_CODE(COLOR_FG_GREEN),
  COLOR_CODE(COLOR_FG_BLUE),
  COLOR_CODE(COLOR_FG_MAGENTA),
  COLOR_CODE(COLOR_FG_CYAN),
};


static int sc_log_begin_file (
    void *data, const char *log_domain, unsigned int log_level) {
  if_fail (log_level <= SC_LOG_VERBOSE) {
    log_level = SC_LOG_VERBOSE;
  }

  time_t curtime = time(NULL);
  struct tm timeinfo;
  localtime_r(&curtime, &timeinfo);
  char timebuf[64];
  strftime(timebuf, sizeof(timebuf), "%b %e %T", &timeinfo);

  return fprintf(
    (FILE *) data, COLOR_SEQ(COLOR_FG_GREEN) "[%s]" RESET_SEQ " "
      SGR_FORMAT_SEQ_START "%-8s" RESET_SEQ " %s: ",
    timebuf, sc_log_level_prefixs[log_level], sc_log_level_names[log_level],
    log_domain == NULL || log_domain[0] == '\0' ? "**" : log_domain);
}


static int sc_vlog_default (
    void *data, const char *log_domain, unsigned int log_level,
    const char *format, va_list arg) {
  int ret = 0;
  if (sc_log_begin_fn != NULL) {
    ret += sc_log_begin_fn(data, log_domain, log_level);
  }
  ret += sc_log_vprint_fn(data, format, arg);
  if (sc_log_end_fn != NULL) {
    ret += sc_log_end_fn(data, log_domain, log_level);
  }
  return ret;
}


SC_WEAK void *sc_log_fn_data;
SC_WEAK sc_logevent_fn_t sc_log_begin_fn = sc_log_begin_file;
SC_WEAK sc_vprint_fn_t sc_log_vprint_fn = (sc_vprint_fn_t) vfprintf;
SC_WEAK sc_logevent_fn_t sc_log_end_fn = (sc_logevent_fn_t) fflush;
SC_WEAK int (*sc_vlog_fn) (
  void *data, const char *log_domain, unsigned int log_level,
  const char *format, va_list arg) = sc_vlog_default;


__attribute__((constructor))
static void sc_log_fn_data_init (void) {
  sc_log_fn_data = stdout;
}


SC_WEAK int sc_log_print_fn (void *data, const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  int ret = sc_log_vprint_fn(data, format, arg);
  va_end(arg);
  return ret;
}


SC_WEAK unsigned int sc_log_level = SC_LOG_NOTICE;


SC_WEAK int sc_vlog (
    const char *log_domain, unsigned int log_level,
    const char *format, va_list arg) {
  return_if_fail (log_level <= sc_log_level) 0;
  return_if_fail (format[0] != '\0') 0;
  return sc_vlog_fn(sc_log_fn_data, log_domain, log_level, format, arg);
}


SC_WEAK int sc_log (
    const char *log_domain, unsigned int log_level, const char *format, ...) {
  return_if_fail (log_level <= sc_log_level) 0;
  return_if_fail (format[0] != '\0') 0;

  va_list arg;
  va_start(arg, format);
  int ret = sc_vlog_fn(sc_log_fn_data, log_domain, log_level, format, arg);
  va_end(arg);
  return ret;
}


SC_WEAK int sc_vlogger (void *data, const char *format, va_list arg) {
  const struct ScLogger *logger = data;
  return sc_vlog(logger->log_domain, logger->log_level, format, arg);
}


SC_WEAK int sc_logger (void *data, const char *format, ...) {
  const struct ScLogger *logger = data;
  va_list arg;
  va_start(arg, format);
  int ret = sc_vlog(logger->log_domain, logger->log_level, format, arg);
  va_end(arg);
  return ret;
}


#if !defined(NO_BACKTRACK)


SC_WEAK int sc_save_backtrace (void **frames, int size, int skip) {
  skip++;
  void *frames_[size + skip];
  int frames_len = backtrace(frames_, size) - skip;
  if (frames_len <= 0) {
    sc_error("(no available backtrace)\n");
  } else {
    memcpy(frames, frames_ + skip, sizeof(frames[0]) * frames_len);
  }
  return frames_len;
}


SC_WEAK int sc_print_backtrace_f (
    void *const *frames, int size, sc_print_fn_t printer, void *data) {
  int ret = printer(data, "Traceback:\n");
  if_fail (size > 0) {
    ret += printer(data, "  (no available backtrace)\n");
    return ret;
  }

  char **symbols = backtrace_symbols(frames, size);
  if_fail (symbols != NULL) {
    ret += printer(data, "  (backtrace_symbols() error)\n");
    return ret;
  }

  for (int i = 0; i < size; i++) {
    ret += printer(data, "  %s\n", symbols[i]);
    if (strstr(symbols[i], "(main+") != NULL) {
      break;
    }
  }
  free(symbols);

  return ret;
}


SC_WEAK int sc_print_backtrace (void *const *frames, int size, FILE *out) {
  return sc_print_backtrace_f(frames, size, (sc_print_fn_t) fprintf, out);
}

#else

SC_WEAK int sc_save_backtrace (void **frames, int size) {
  (void) frames;
  (void) size;
  return 0;
}


SC_WEAK int sc_print_backtrace_f (
    void *const *frames, int size, sc_print_fn_t printer, void *data) {
  (void) frames;
  (void) size;
  (void) printer;
  (void) data;
  return 0;
}


SC_WEAK int sc_print_backtrace (void *const *frames, int size, FILE *out) {
  (void) frames;
  (void) size;
  (void) out;
  return 0;
}

#endif


SC_WEAK thread_local struct ScException sc_exc = {};


struct ScStrerrorHandler {
  unsigned long domain;
  sc_strerror_fn_t strfn;
};


static struct ScStrerrorHandler sc_strerror_handlers[32] = {};
static unsigned int sc_strerror_handlers_len = 0;


SC_WEAK int sc_register_strerror (
    unsigned long domain, sc_strerror_fn_t strfn) {
  return_if_fail (domain != 0) -1;

  unsigned int i;
  for (i = 0; i < sc_strerror_handlers_len; i++) {
    struct ScStrerrorHandler *handler = sc_strerror_handlers + i;
    if (handler->domain == domain) {
      handler->strfn = strfn;
      return 0;
    }
  }
  return_if_fail (i < arraysize(sc_strerror_handlers)) -1;

  struct ScStrerrorHandler *handler =
    sc_strerror_handlers + sc_strerror_handlers_len;
  handler->domain = domain;
  handler->strfn = strfn;
  sc_strerror_handlers_len++;
  return 0;
}


__always_inline
static inline int _save_err (
    unsigned long domain, int code, const char *fn, const char *what) {
  sc_exc.domain = domain;
  sc_exc.code = code;
  sc_exc.fn = fn;
  sc_exc.what = what;
  sc_exc.frames_len = sc_save_backtrace(
    sc_exc.frames, arraysize(sc_exc.frames), 1);
  return code;
}


SC_WEAK int sc_set_err (
    unsigned long domain, int code, const char *fn, const char *what) {
  return _save_err(domain, code, fn, what);
}


SC_WEAK int sc_set_errno (const char *fn, const char *what) {
  return _save_err(0, errno, fn, what);
}


SC_WEAK int sc_print_err_f (sc_print_fn_t printer, void *data) {
  int ret = 0;
  ret += printer(data, "at %s(): ", sc_exc.fn);

  if (sc_exc.domain == 0) {
    ret += printer(data, "%s\n", strerror(sc_exc.code));
  } else {
    for (unsigned int i = 0; i < sc_strerror_handlers_len; i++) {
      struct ScStrerrorHandler *handler = sc_strerror_handlers + i;
      if (handler->domain == sc_exc.domain) {
        ret += printer(data, "%s\n", handler->strfn(sc_exc.code));
        goto end;
      }
    }
    ret += printer(data, "%08lu, %x\n", sc_exc.domain, sc_exc.code);
  }

  if (sc_exc.what != NULL) {
    ret += printer(data, "  %s\n", sc_exc.what);
  }

end:
  if (sc_log_level >= SC_LOG_DEBUG) {
    ret += sc_print_backtrace_f(
      sc_exc.frames, sc_exc.frames_len, printer, data);
  }

  return ret;
}


SC_WEAK int sc_print_err (FILE *out) {
  return sc_print_err_f((sc_print_fn_t) fprintf, out);
}
