#define _GNU_SOURCE

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>
#ifdef __linux__
#include <sys/sendfile.h>
#endif

#include "include/platform/endian.h"
#include "platform/nowide.h"
#include "platform/stdbit.h"

#include "utils.h"
#include "macro.h"
#include "log.h"


#define DEFAULT_BSIZE (1024 * 1024)


__attribute_artificial__
static inline size_t round_size (size_t x) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-shift-count-overflow"
  size_t part = 1ull << (sizeof(x) * 8 - stdc_leading_zeros(x) - 1);
#pragma GCC diagnostic pop
  part = min(part, 4096 * 16);
  return (x + part - 1) & ~(part - 1);
}


void *ptrarray_new (void *arrp, size_t *lenp, size_t elmsize) {
  void **newarr = realloc(
    *(void ***) arrp, sizeof(void *) * round_size(*lenp + 1));
  if_fail (newarr != NULL) {
    (void) ERR_STD(realloc);
    return NULL;
  }
  *(void ***) arrp = newarr;

  void *elm = malloc(elmsize);
  if_fail (elm != NULL) {
    (void) ERR_STD(malloc);
    return NULL;
  }

  newarr[*lenp] = elm;
  (*lenp)++;

  return elm;
}


int array_new (
    void *arrp, size_t *lenp, size_t elmsize, init_fn_t init_fn,
    void *userdata) {
  void *newarr = realloc(*(void **) arrp, elmsize * round_size(*lenp + 1));
  return_if_fail (newarr != NULL) ERR_STD(realloc);
  *(void **) arrp = newarr;

  int ret = init_fn((unsigned char *) newarr + elmsize * *lenp, userdata);
  return_if_fail (ret == 0) ret;

  (*lenp)++;
  return 0;
}


static int copy_io (FILE *dst, FILE *src, size_t len, unsigned int bsize) {
  if (bsize <= 0) {
    bsize = min(DEFAULT_BSIZE, UINT_MAX & ~1023);
  }

  void *buf = malloc(bsize);
  return_if_fail (buf != NULL) ERR_STD(malloc);

  int ret;

  for (size_t remain = len; remain > 0; ) {
    size_t chunksize = min(bsize, remain);
    if_fail (fread(buf, chunksize, 1, src) == 1) {
      ret = ERR_STD(fread);
      goto fail;
    }
    if_fail (fwrite(buf, chunksize, 1, dst) == 1) {
      ret = ERR_STD(fwrite);
      goto fail;
    }
    remain -= chunksize;
  }

  ret = 0;
fail:
  free(buf);
  return ret;
}

#if defined HAVE_COPY_FILE_RANGE || defined __linux__ || defined __freebsd__

int copy (FILE *dst, FILE *src, size_t len, unsigned int bsize) {
  off_t inoff = ftello(src);
  return_if_fail (inoff != -1) ERR_STD(ftello);
  off_t outoff = ftello(dst);
  return_if_fail (outoff != -1) ERR_STD(ftello);

  return_if_fail (fflush(src) == 0) ERR_STD(fflush);
  return_if_fail (fflush(dst) == 0) ERR_STD(fflush);

  ssize_t res;

  res = copy_file_range(fileno(src), &inoff, fileno(dst), &outoff, len, 0);
  if_fail (res >= 0 && (size_t) res == len) {
    return_if_fail (res < 0 && errno == EXDEV) ERR_STD(copy_file_range);
#ifndef __linux__
    goto fallback;
#else
    fseeko(dst, 0, SEEK_END);
    off_t dstsize = ftello(dst);
    fseeko(dst, outoff, SEEK_SET);
    return_if_fail (dstsize != -1) ERR_STD(ftello);

    if (dstsize != outoff) {
      goto fallback;
    }

    res = sendfile(fileno(dst), fileno(src), &inoff, len);
    if_fail (res >= 0 && (size_t) res == len) {
      return_if_fail (res < 0 && errno == EINVAL) ERR_STD(sendfile);
      goto fallback;
    }
#endif
  }

  fseeko(src, inoff, SEEK_SET);
  fseeko(dst, outoff, SEEK_SET);
  return 0;

fallback:
  return copy_io(dst, src, len, bsize);
}

#else

int copy (FILE *dst, FILE *src, size_t len, unsigned int bsize) {
  return copy_io(dst, src, len, bsize);
}

#endif

int dump (const char *path, FILE *src, size_t len, unsigned int bsize) {
  FILE *out = mfopen(path, "wb");
  return_if_fail (out != NULL) ERR_STD(mfopen);
  int ret = copy(out, src, len, bsize);
  fclose(out);
  return ret;
}


int copy_uncompress (
    FILE *dst, FILE *src, size_t len, unsigned int bsize, size_t *outlenp) {
  if (bsize <= 0) {
    bsize = min(DEFAULT_BSIZE, UINT_MAX & ~1023);
  }

  unsigned char *buf = malloc(2 * bsize);
  return_if_fail (buf != NULL) ERR_STD(malloc);
  unsigned char *bufout = buf + bsize;

  int res;
  int ret;

  z_stream strm = {0};
  res = inflateInit(&strm);
  if_fail (res == Z_OK) {
    ret = ERR_ZLIB(inflateInit, res);
    goto fail_strm;
  }

  for (size_t remain = len; remain > 0; ) {
    size_t chunksize = min(bsize, remain);
    if_fail (fread(buf, chunksize, 1, src) == 1) {
      ret = ERR_STD(fread);
      goto fail;
    }

    strm.next_in = buf;
    strm.avail_in = chunksize;

    do {
      strm.next_out = bufout;
      strm.avail_out = bsize;

      res = inflate(&strm, Z_NO_FLUSH);
      if_fail (res == Z_OK || res == Z_STREAM_END) {
        ret = ERR_ZLIB(inflate, res);
        goto fail;
      }

      if_fail (fwrite(bufout, bsize - strm.avail_out, 1, dst) == 1) {
        ret = ERR_STD(fwrite);
        goto fail;
      }
    } while (strm.avail_out == 0 && res != Z_STREAM_END);

    remain -= chunksize;
  }

  if (outlenp != NULL) {
    *outlenp = strm.total_out;
  }
  ret = 0;
fail:
  inflateEnd(&strm);
fail_strm:
  free(buf);
  return ret;
}


// Length-Prefixed Encoding
int write_lpe (FILE *dst, FILE *src, size_t *lenp, unsigned int bsize) {
  int ret;

  off_t pos = ftello(src);
  return_if_fail (pos != -1) ERR_STD(ftello);

  uint32_t size_h;
  return_if_fail (fread(&size_h, sizeof(size_h), 1, src) == 1) ERR_STD(fread);
  size_t size = le32toh(size_h);

  ret = copy(dst, src, size, bsize);
  goto_if_fail (ret == 0) fail;

  if (lenp != NULL) {
    *lenp = size;
  }
  return 0;

fail:
  fseeko(src, pos, SEEK_SET);
  return ret;
}


int dump_lpe (const char *path, FILE *src, size_t *lenp, unsigned int bsize) {
  FILE *out = mfopen(path, "wb");
  return_if_fail (out != NULL) ERR_STD(mfopen);
  int ret = write_lpe(out, src, lenp, bsize);
  fclose(out);
  return ret;
}


int write_lpe_uncompress (
    FILE *dst, FILE *src, size_t *lenp, unsigned int bsize, size_t *outlenp) {
  int ret;

  off_t pos = ftello(src);
  return_if_fail (pos != -1) ERR_STD(ftello);

  uint32_t size_h;
  return_if_fail (fread(&size_h, sizeof(size_h), 1, src) == 1) ERR_STD(fread);
  size_t size = le32toh(size_h);

  ret = copy_uncompress(dst, src, size, bsize, outlenp);
  goto_if_fail (ret == 0) fail;

  if (lenp != NULL) {
    *lenp = size;
  }
  return 0;

fail:
  fseeko(src, pos, SEEK_SET);
  return ret;
}
