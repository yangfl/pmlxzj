#include <iconv.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "include/alg.h"
#include "log.h"
#include "macro.h"


size_t plzj_iconv (
    const char *tocode, const char *fromcode, char *dst, size_t dstsize,
    const char *src, size_t srclen) {
  goto_if_fail (srclen > 0) end;
  goto_if_fail (dstsize > 1) end;

  iconv_t cd = iconv_open(tocode, fromcode);
  if_fail (cd != (iconv_t) -1) {
    (void) ERR_SYS(iconv_open);
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


char *plzj_iconv_enc_new (const char *src, size_t srclen) {
  if (srclen <= 0) {
    srclen = strlen(src);
  }
  size_t len = 2 * srclen;
  char *buf = malloc(len);
  if_fail (buf != NULL) {
    (void) ERR_SYS(malloc);
    return NULL;
  }

  len = plzj_iconv_enc(buf, len, src, srclen) + 1;
  char *new = realloc(buf, len);
  if (new != NULL) {
    buf = new;
  }
  return buf;
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
