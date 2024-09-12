#if defined _WIN32 && !defined ACP_IS_UTF8

#define _GNU_SOURCE

#include <direct.h>
#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <windows.h>
#include <wchar.h>

#include <shellapi.h>

#include "../macro.h"
#include "../log.h"
#include "nowide.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"


int mwiden (wchar_t *restrict ws, int size, const char *restrict s, int len) {
  int ret = MultiByteToWideChar(CP_UTF8, 0, s, len, ws, size);
  if_fail (ret > 0) {
    errno = EINVAL;
    (void) ERR_WIN32(MultiByteToWideChar);
  }
  return ret;
}


wchar_t *maswiden (const char *restrict s, int len, int *sizep) {
  int size = mwiden(NULL, 0, s, len);
  return_if_fail (size > 0) NULL;

  wchar_t *ws = malloc(sizeof(*ws) * size);
  if_fail (ws != NULL) {
    (void) ERR_STD(malloc);
    return NULL;
  }

  int ret = mwiden(ws, size, s, len);
  if_fail (ret > 0) {
    free(ws);
    return NULL;
  }

  if (sizep != NULL) {
    *sizep = ret;
  }
  return ws;
}


int mnarrow (char *restrict s, int size, const wchar_t *restrict ws, int len) {
  int ret = WideCharToMultiByte(CP_UTF8, 0, ws, len, s, size, NULL, NULL);
  if_fail (ret > 0) {
    errno = EINVAL;
    (void) ERR_WIN32(WideCharToMultiByte);
  }
  return ret;
}


char *masnarrow (const wchar_t *restrict ws, int len, int *sizep) {
  int size = mnarrow(NULL, 0, ws, len);
  return_if_fail (size > 0) NULL;

  char *s = malloc(size);
  if_fail (s != NULL) {
    (void) ERR_STD(malloc);
    return NULL;
  }

  int ret = mnarrow(s, size, ws, len);
  if_fail (ret > 0) {
    free(s);
    return NULL;
  }

  if (sizep != NULL) {
    *sizep = ret;
  }
  return s;
}


void set_margv (int argc, char **argv) {
  setlocale(LC_ALL, "");

  int p_argc = 0;
  wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &p_argc);
  if_fail (p_argc == argc) {
    _exit(255);
  }

  for (int i = 0; i < argc; i++) {
    argv[i] = m_narrow(wargv[i]);
    if_fail (argv[i] != NULL) {
      _exit(255);
    }
  }
}


FILE *mfopen (
    const char *restrict filename, const char *restrict modes) {
  FILE *stream = NULL;

  wchar_t *wfilename = NULL;
  if (filename != NULL) {
    wfilename = m_widen(filename);
    return_if_fail (wfilename != NULL) NULL;
  }

  wchar_t *wmodes = NULL;
  if (modes != NULL) {
    wmodes = m_widen(modes);
    goto_if_fail (wmodes != NULL) fail_wmodes;
  }

  stream = _wfopen(wfilename, wmodes);

  free(wmodes);
fail_wmodes:
  free(wfilename);
  return stream;
}


int fmprintf (FILE *restrict stream, const char *restrict fmt, ...) {
  return_if_fail (fmt != NULL && fmt[0] != '\0') fprintf(stream, fmt);

  va_list arg;
  va_start(arg, fmt);
  int ret = vfmprintf(stream, fmt, arg);
  va_end(arg);
  return ret;
}


int fputms (const char *restrict s, FILE *restrict stream) {
  return_if_fail (s != NULL && s[0] != '\0') fputs(s, stream);

  wchar_t *ws = m_widen(s);
  return_if_fail (ws != NULL) EOF;

  int ret = fputws(ws, stream);
  free(ws);
  return ret;
}


int mprintf (const char *restrict fmt, ...) {
  return_if_fail (fmt != NULL && fmt[0] != '\0') printf(fmt);

  va_list arg;
  va_start(arg, fmt);
  int ret = vfmprintf(stdout, fmt, arg);
  va_end(arg);
  return ret;
}


int vfmprintf (FILE *restrict s, const char *restrict fmt, va_list arg) {
  return_if_fail (fmt != NULL && fmt[0] != '\0') vfprintf(s, fmt, arg);

  char *str;
  int len = vasprintf(&str, fmt, arg);
  return_if_fail (len > 0) len;

  int ret = fputms(str, s);
  free(str);
  return ret;
}


int mmkdir (const char *path, mode_t mode) {
  (void) mode;
  return_if_fail (path != NULL && path[0] != '\0') _mkdir(path);

  wchar_t *wpath = m_widen(path);
  return_if_fail (wpath != NULL) -1;

  int ret = _wmkdir(wpath);
  free(wpath);
  return ret;
}


int mstat (const char *restrict file, struct stat *restrict buf) {
  return_if_fail (file != NULL && file[0] != '\0') stat(file, buf);

  wchar_t *wfile = m_widen(file);
  return_if_fail (wfile != NULL) -1;

  int ret = _wstat(wfile, (struct _stat *) buf);
  free(wfile);
  return ret;
}


int mcsftime (
    char *restrict s, size_t maxsize, const char *restrict format,
    const struct tm *restrict tp) {
  wchar_t *wformat = m_widen(format);
  return_if_fail (wformat != NULL) 0;

  wchar_t *ws = malloc(sizeof(*ws) * maxsize);
  return_if_fail (ws != NULL) 0;

  int ret;

  int len = wcsftime(ws, maxsize, wformat, tp);
  free(wformat);
  if_fail (len != 0) {
    ret = 0;
    goto fail;
  }

  ret = mnarrow(s, maxsize, ws, -1) - 1;
  if_fail (ret >= 0) {
    ret = 0;
    goto fail;
  }

fail:
  free(ws);
  return ret;
}


int munlink (const char *name) {
  wchar_t *wname = m_widen(name);
  return_if_fail (wname != NULL) -1;

  int ret = _wunlink(wname);
  free(wname);
  return ret;
}

#else

#include <locale.h>

#include "../macro.h"
#include "nowide.h"


void set_margv (int argc, char **argv) {
  (void) argc;
  (void) argv;
  setlocale(LC_ALL, "");
}


#endif
