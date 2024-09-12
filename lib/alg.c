#ifndef HAVE_ICONV_H
#  ifdef __has_include
#    if __has_include(<iconv.h>)
#      define HAVE_ICONV_H 1
#    endif
#  endif
#endif

#ifdef HAVE_ICONV_H
#include <iconv.h>
#elif defined _WIN32
#include <windows.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "include/alg.h"
#include "macro.h"
#include "log.h"


#ifdef HAVE_ICONV_H

__nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2)) __attr_access((__write_only__, 3, 4))
__attr_access((__read_only__, 5, 6))
/**
 * @brief Convert character encoding, insuring null termination.
 *
 * @param tocode Destination codeset.
 * @param fromcode Source codeset.
 * @param[out] dst Destination buffer.
 * @param dstsize Size of @p dst , including space for terminating null.
 * @param src Source string.
 * @param srclen Length of @p src , not including terminating null (ideally
 *   @c strlen(src) ).
 *
 * @return Length of converted string, not including terminating null (ideally
 *   @c strlen(dst) ). A null character is always appended at the end of
 *   @p dst .
 */
static size_t plzj_iconv (
    const char *tocode, const char *fromcode, char *dst, size_t dstsize,
    const char *src, size_t srclen) {
  goto_if_fail (srclen > 0) end;
  goto_if_fail (dstsize > 1) end;

  iconv_t cd = iconv_open(tocode, fromcode);
  if_fail (cd != (iconv_t) -1) {
    (void) ERR_STD(iconv_open);
    goto end;
  }

  size_t ret = 0;
  char *out = dst;
  size_t avail = dstsize - 1;
  iconv(cd, (char **) &src, &srclen, &out, &avail);
  avail++;
  dst[dstsize - avail] = '\0';
  ret = dstsize - avail;

  iconv_close(cd);
  return ret;

end:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
  if (dstsize > 0) {
    dst[0] = '\0';
  }
  return 0;
#pragma GCC diagnostic pop
}


size_t plzj_iconv_dec (
    char *dst, size_t dstsize, const char *src, size_t srclen) {
  return plzj_iconv("", "GBK", dst, dstsize, src, srclen);
}


size_t plzj_iconv_enc (
    char *dst, size_t dstsize, const char *src, size_t srclen) {
  return plzj_iconv("GBK", "", dst, dstsize, src, srclen);
}

#elif defined _WIN32

static int plzj_iconv_widen (
    UINT CodePage, wchar_t *ws, int size, const char *s, int len) {
  int ret = MultiByteToWideChar(CodePage, 0, s, len, ws, size);
  if_fail (ret > 0) {
    (void) ERR_WIN32(MultiByteToWideChar);
  }
  return ret;
}


static wchar_t *plzj_iconv_aswiden (
    UINT CodePage, const char *s, int len, int *sizep) {
  int size = plzj_iconv_widen(CodePage, NULL, 0, s, len);
  return_if_fail (size > 0) NULL;

  wchar_t *ws = malloc(sizeof(*ws) * size);
  if_fail (ws != NULL) {
    (void) ERR_STD(malloc);
    return NULL;
  }

  int ret = plzj_iconv_widen(CodePage, ws, size, s, len);
  if_fail (ret > 0) {
    free(ws);
    return NULL;
  }

  if (sizep != NULL) {
    *sizep = ret;
  }
  return ws;
}


static int plzj_iconv_narrow (
    UINT CodePage, char *s, int size, const wchar_t *ws, int len) {
  int ret = WideCharToMultiByte(CodePage, 0, ws, len, s, size, NULL, NULL);
  if_fail (ret > 0) {
    (void) ERR_WIN32(WideCharToMultiByte);
  }
  return ret;
}


static size_t plzj_iconv (
    UINT tocode, UINT fromcode, char *dst, size_t dstsize,
    const char *src, size_t srclen) {
  goto_if_fail (srclen > 0) end;
  goto_if_fail (dstsize > 1) end;

  int wslen;
  wchar_t *ws = plzj_iconv_aswiden(fromcode, src, srclen, &wslen);
  goto_if_fail (ws != NULL) end;

  int ret = plzj_iconv_narrow(tocode, dst, dstsize, ws, wslen);
  if_fail (ret >= 0) {
    ret = 0;
  }
  dst[ret] = '\0';

  free(ws);
  return ret;

end:
  if (dstsize > 0) {
    dst[0] = '\0';
  }
  return 0;
}


static UINT plzj_get_gbk_cp (void) {
  static UINT CodePage = 0;
  if (CodePage == 0) {
    CodePage = IsValidCodePage(54936) ? 54936 : 936;
    sc_debug("Using GBK codepage %u\n", CodePage);
  }
  return CodePage;
}


size_t plzj_iconv_dec (
    char *dst, size_t dstsize, const char *src, size_t srclen) {
  return plzj_iconv(CP_UTF8, plzj_get_gbk_cp(), dst, dstsize, src, srclen);
}


size_t plzj_iconv_enc (
    char *dst, size_t dstsize, const char *src, size_t srclen) {
  return plzj_iconv(plzj_get_gbk_cp(), CP_UTF8, dst, dstsize, src, srclen);
}

#else

static size_t plzj_iconv_dummy (
    char *dst, size_t dstsize, const char *src, size_t srclen) {
  size_t size = min(dstsize - 1, srclen);
  memcpy(dst, src, size);
  dst[size] = '\0';
  return size;
}


size_t plzj_iconv_dec (
    char *dst, size_t dstsize, const char *src, size_t srclen) {
  return plzj_iconv_dummy(dst, dstsize, src, srclen);
}


size_t plzj_iconv_enc (
    char *dst, size_t dstsize, const char *src, size_t srclen) {
  return plzj_iconv_dummy(dst, dstsize, src, srclen);
}

#endif

char *plzj_iconv_enc_new (const char *src, size_t srclen) {
  if (srclen <= 0) {
    srclen = strlen(src);
  }

  size_t dstsize = 2 * srclen + 1;
  char *dst = malloc(dstsize);
  if_fail (dst != NULL) {
    (void) ERR_STD(malloc);
    return NULL;
  }

  dstsize = plzj_iconv_enc(dst, dstsize, src, srclen) + 1;

  char *new = realloc(dst, dstsize);
  if (new != NULL) {
    dst = new;
  }
  return dst;
}


size_t plzj_infotext_dec_iconv (char *dst, size_t dstsize, const char *src) {
  char decoded[40];
  plzj_infotext_encdec(decoded, src);
  return plzj_iconv_dec(
    dst, dstsize, decoded, strnlen(decoded, sizeof(decoded)));
}


size_t plzj_infotext_enc_iconv (char *dst, const char *src, size_t srclen) {
  unsigned int len = plzj_iconv_enc(dst, 40, src, srclen);
  plzj_infotext_encdec(dst, dst);
  return len;
}


unsigned int plzj_regcode_enc (char *regcode2, long code2) {
  code2 *= 124;
  sprintf(regcode2, "%ld", code2);
  unsigned int i;
  for (i = 0; regcode2[i] != '\0'; i++) {
    regcode2[i] += 20 + 10 * (i % 2) - i / 3;
  }
  return i;
}
