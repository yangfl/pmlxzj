#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <zlib.h>

#include "utils.h"
#include "macro.h"
#include "log.h"


#ifdef __GLIBC__

int copy (FILE *dst, FILE *src, size_t len) {
  off_t inoff = ftello(src);
  return_if_fail (inoff >= 0) ERR_SYS(ftello);
  off_t outoff = ftello(dst);
  return_if_fail (outoff >= 0) ERR_SYS(ftello);

  return_if_fail (fflush(src) == 0) ERR_SYS(fflush);
  return_if_fail (fflush(dst) == 0) ERR_SYS(fflush);

  ssize_t res = copy_file_range(
    fileno(src), &inoff, fileno(dst), &outoff, len, 0);
  return_if_fail (res >= 0 && (size_t) res == len) ERR_SYS(copy_file_range);

  fseeko(src, inoff, SEEK_SET);
  fseeko(dst, outoff, SEEK_SET);
  return 0;
}

#else

int copy (FILE *dst, FILE *src, size_t len) {
  register const size_t bufsize = min(1024 * 1024, SIZE_MAX);
  void *buf = malloc(bufsize);
  return_if_fail (buf != NULL) ERR_SYS(malloc);

  int ret;

  for (size_t remain = len; remain > 0; ) {
    size_t chunksize = min(bufsize, remain);
    if_fail (fread(buf, chunksize, 1, src) == 1) {
      ret = ERR_SYS(fread);
      goto fail;
    }
    if_fail (fwrite(buf, chunksize, 1, dst) == 1) {
      ret = ERR_SYS(fwrite);
      goto fail;
    }
    remain -= chunksize;
  }

  ret = 0;
fail:
  free(buf);
  return ret;
}

#endif


int copy_uncompress (FILE *dst, FILE *src, size_t len) {
  z_stream strm = {};
  int res = inflateInit(&strm);
  return_if_fail (res == Z_OK) ERR_ZLIB(inflateInit, res);

  int ret;

  register const size_t bufsize = min(1024 * 1024, INT_MAX);
  unsigned char *buf = malloc(2 * bufsize);
  if_fail (buf != NULL) {
    ret = ERR_SYS(malloc);
    goto fail_buf;
  }
  unsigned char *bufout = buf + bufsize;

  for (size_t remain = len; remain > 0; ) {
    size_t chunksize = min(bufsize, remain);
    if_fail (fread(buf, chunksize, 1, src) == 1) {
      ret = ERR_SYS(fread);
      goto fail;
    }

    strm.next_in = buf;
    strm.avail_in = chunksize;

    int res;
    do {
      strm.next_out = bufout;
      strm.avail_out = bufsize;

      res = inflate(&strm, Z_NO_FLUSH);
      if_fail (res == Z_OK || res == Z_STREAM_END) {
        ret = ERR_ZLIB(inflate, res);
        goto fail;
      }

      if_fail (fwrite(bufout, bufsize - strm.avail_out, 1, dst) == 1) {
        ret = ERR_SYS(fwrite);
        goto fail;
      }
    } while (strm.avail_out == 0 && res != Z_STREAM_END);

    remain -= chunksize;
  }

  ret = 0;
fail:
  free(buf);
fail_buf:
  inflateEnd(&strm);
  return ret;
}


// Length-Prefixed Encoding
static int write_lpe_func (FILE *dst, FILE *src, stream_fn func) {
  int ret;

  off_t pos = ftello(src);
  return_if_fail (pos >= 0) ERR_SYS(ftello);

  uint32_t size_h;
  return_if_fail (fread(&size_h, sizeof(size_h), 1, src) == 1) ERR_SYS(fread);
  size_t size = le32toh(size_h);

  ret = func(dst, src, size);
  goto_if_fail (ret == 0) fail;

  ret = 0;
  if (0) {
fail:
    fseeko(src, pos, SEEK_SET);
  }
  return ret;
}


int write_lpe (FILE *dst, FILE *src) {
  return write_lpe_func(dst, src, copy);
}


int dump_lpe (const char *path, FILE *src) {
  FILE *out = fopen(path, "wb");
  return_if_fail (out != NULL) ERR_SYS(fopen);
  int ret = write_lpe(out, src);
  fclose(out);
  return ret;
}


int write_lpe_uncompress (FILE *dst, FILE *src) {
  return write_lpe_func(dst, src, copy_uncompress);
}


// Length-Suffixed Encoding
int write_lse (FILE *dst, FILE *src) {
  off_t pos = ftello(src);
  return_if_fail (pos >= 0) ERR_SYS(ftello);

  return_if_fail (fseeko(src, -4, SEEK_CUR) == 0) ERR_SYS(fseeko);

  int ret;

  uint32_t size_h;
  if_fail (fread(&size_h, sizeof(size_h), 1, src) == 1) {
    ret = ERR_SYS(fread);
    goto fail;
  }
  size_t size = le32toh(size_h);

  if_fail (fseeko(src, -4 - size, SEEK_CUR) == 0) {
    ret = ERR_SYS(fseeko);
    goto fail;
  }
  ret = copy(dst, src, size);
  goto_if_fail (ret == 0) fail;

  if_fail (fseeko(src, -size, SEEK_CUR) == 0) {
    ret = ERR_SYS(fseeko);
    goto fail;
  }

  ret = 0;
  if (0) {
fail:
    fseeko(src, pos, SEEK_SET);
  }
  return ret;
}


int dump_lse (const char *path, FILE *src) {
  FILE *out = fopen(path, "wb");
  return_if_fail (out != NULL) ERR_SYS(fopen);
  int ret = write_lse(out, src);
  fclose(out);
  return ret;
}
