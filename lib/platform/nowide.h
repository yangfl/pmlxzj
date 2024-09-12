#ifndef PLATFORM_NOWIDE_H
#define PLATFORM_NOWIDE_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#if defined _WIN32 && !defined ACP_IS_UTF8

#include <stdarg.h>
#include <sys/types.h>
#include <wchar.h>

#include "../include/defs.h"


#ifndef DIR_SEP
#  define DIR_SEP '\\'
#endif
#ifndef DIR_SEP_S
#  define DIR_SEP_S "\\"
#endif


typedef wchar_t mchar_t;
typedef wint_t mint_t;

#define SCNsM "ls"


/* stdio.h */

FILE *mfdopen (int, const char *);
#define fgetmc fgetwc
#define fgetmc_unlocked _fgetwc_nolock
char *fgetms (char *restrict, int, FILE *restrict);
char *fgetms_unlocked (char *restrict, int, FILE *restrict);
__attribute_malloc__ __attr_dealloc_fclose __attribute_warn_unused_result__
FILE *mfopen (
  const char *__restrict __filename, const char *__restrict __modes);
__nonnull((1)) __attr_access((__read_only__, 2))
__attr_format((gnu_printf, 2, 3))
int fmprintf (FILE *__restrict __stream, const char *__restrict __fmt, ...);
#define fputmc fputwc
#define fputmc_unlocked _fputwc_nolock
__nonnull((2))
int fputms (const char *__restrict __s, FILE *__restrict __stream);
int fputms_unlocked (const char *restrict, FILE *restrict);
FILE *mfreopen (const char *restrict, const char *restrict, FILE *restrict);
int fmscanf (FILE *restrict, const char *restrict, ...);
#define getmc getwc
#define getmc_unlocked _getwc_nolock
#define getmchar getwchar
#define getmchar_unlocked _getwchar_nolock
char *getms (char *);
void mperror (const char *);
FILE *mpopen (const char *, const char *);
__attr_access((__read_only__, 1)) __attr_format((gnu_printf, 1, 2))
int mprintf (const char *__restrict __fmt, ...);
#define putmc putwc
#define putmc_unlocked _putwc_nolock
#define putmchar putwchar
#define putmchar_unlocked _putwchar_nolock
int putms (const char *);
int mremove (const char *);
int mrename (const char *, const char *);
int mscanf (const char *restrict, ...);
int snmprintf (char *restrict, size_t, const char *restrict, ...);
int smprintf (char *restrict, const char *restrict, ...);
int smscanf (const char *restrict, const char *restrict, ...);
char *mtempnam (const char *, const char *);
char *mtmpnam (char *);
#define ungetmc ungetwc
__nonnull((1)) __attr_access((__read_only__, 2))
__attr_format((gnu_printf, 2, 0))
int vfmprintf (
  FILE *__restrict __s, const char *__restrict __fmt, va_list __arg);
int vfmscanf (FILE *restrict, const char *restrict, va_list);
int vmprintf (const char *restrict, va_list);
int vsmcanf (const char *restrict, va_list);
int vsmnprintf (char *restrict, size_t, const char *restrict, va_list);
int vsmprintf (char *restrict, const char *restrict, va_list);
int vsmscanf (const char *restrict, const char *restrict, va_list);


/* sys/stat.h */

__attr_access((__read_only__, 1))
int mmkdir (const char *__path, mode_t __mode);
__attr_access((__read_only__, 1)) __attr_access((__write_only__, 2))
int mstat (const char *__restrict __file, struct stat *__restrict __buf);


/* time.h */

__THROW __nonnull((1, 3, 4)) __attr_access((__write_only__, 1, 2))
__attr_access((__read_only__, 3)) __attr_access((__read_only__, 4))
int mcsftime(
  char *__restrict __s, size_t __maxsize, const char *__restrict __format,
  const struct tm *__restrict __tp);


/* unistd.h */
__THROW __nonnull((1))
int munlink (const char *__name);

#else

#include "../include/defs.h"


#ifndef DIR_SEP
#  define DIR_SEP '/'
#endif
#ifndef DIR_SEP_S
#  define DIR_SEP_S "/"
#endif


typedef char mchar_t;
typedef int mint_t;

#define SCNsM "s"


#define mfdopen fdopen
#define fgetmc fgetc
#define fgetmc_unlocked fgetc_unlocked
#define fgetms fgets
#define fgetms_unlocked fgets_unlocked
#define mfopen fopen
#define fmprintf fprintf
#define fputmc fputc
#define fputmc_unlocked fputc_unlocked
#define fputms fputs
#define fputms_unlocked fputs_unlocked
#define mfreopen freopen
#define fmscanf fscanf
#define getmc getc
#define getmc_unlocked getc_unlocked
#define getmchar getchar
#define getmchar_unlocked getchar_unlocked
#define getms gets
#define mperror perror
#define mpopen popen
#define mprintf printf
#define putmc putc
#define putmc_unlocked putc_unlocked
#define putmchar putchar
#define putmchar_unlocked putchar_unlocked
#define putms puts
#define mremove remove
#define mrename rename
#define mscanf scanf
#define snmprintf snprintf
#define smprintf sprintf
#define smscanf sscanf
#define mtempnam tempnam
#define mtmpnam tmpnam
#define ungetmc ungetc
#define vfmprintf vfprintf
#define vfmscanf vfscanf
#define vmprintf vprintf
#define vsmcanf vscanf
#define vsmnprintf vsnprintf
#define vsmprintf vsprintf
#define vsmscanf vsscanf

#ifdef _WIN32
#define mmkdir(__path, __mode) _mkdir(__path)
#define mstat _stat
#else
#define mmkdir mkdir
#define mstat stat
#endif

#define mcsftime strftime

#ifdef _WIN32
#define munlink _unlink
#else
#define munlink unlink
#endif

#endif

__THROW __nonnull((3)) __attr_access((__write_only__, 1, 2))
__attr_access((__read_only__, 3))
int mwiden (
  mchar_t *__restrict __ws, int __size, const char *__restrict __s, int __len);
__THROW __nonnull((1)) __attribute_malloc__ __attr_access((__read_only__, 1))
__attr_access((__write_only__, 3))
mchar_t *maswiden (const char *__restrict __s, int __len, int *__sizep);
__THROW __nonnull((3)) __attr_access((__write_only__, 1, 2))
__attr_access((__read_only__, 3))
int mnarrow (
  char *__restrict __s, int __size, const mchar_t *__restrict __ws, int __len);
__THROW __nonnull((1)) __attribute_malloc__ __attr_access((__read_only__, 1))
__attr_access((__write_only__, 3))
char *masnarrow (const mchar_t *__restrict __ws, int __len, int *__sizep);

#define m_widen(s) maswiden(s, -1, NULL)
#define m_narrow(s) masnarrow(s, -1, NULL)


__nonnull() __attr_access((__write_only__, 2, 1))
void set_margv (int argc, char **argv);


#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_NOWIDE_H */
