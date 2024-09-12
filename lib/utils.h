#ifndef UTILS_H
#define UTILS_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>
#include <zlib.h>

#include "include/defs.h"
#include "macro.h"
#include "log.h"


__nonnull()
typedef int (*stream_fn_t) (FILE *, FILE *, size_t);
__nonnull()
typedef int (*init_fn_t) (void *, void *);


__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline uint32_t bitstream_get (
    const void *buf, size_t i, unsigned int len) {
  const unsigned char *start = (const unsigned char *) buf + i / 8;
  const unsigned char *end = (const unsigned char *) buf + (i + len + 7) / 8;
  ptrdiff_t run = end - start;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-shift-count-overflow"
  return
    run <= 1 ? BIT_FIELD(*start, 8 - i % 8 - len, len) :
    run <= 2 ? BIT_FIELD(
      be16toh(*(const uint16_t *) start), 16 - i % 8 - len, len) :
    BIT_FIELD(
      be32toh(*(const uint32_t *) start), 32 - i % 8 - len, len);
#pragma GCC diagnostic pop
}


__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1, 2))
static inline unsigned long plzj_crc32 (const void *buf, size_t len) {
  return crc32_z(crc32_z(0, Z_NULL, 0), buf, len);
}


__nonnull((1, 2))
void *ptrarray_new (void *arrp, size_t *lenp, size_t elmsize);
__nonnull((1, 2))
int array_new (
  void *arrp, size_t *lenp, size_t elmsize, init_fn_t init_fn, void *userdata);


__attribute_artificial__ __nonnull() __attr_access((__write_only__, 4, 5))
static inline int read_at (
    FILE *__restrict__ stream, off_t off, int whence,
    void *__restrict__ ptr, size_t size) {
  return_if_fail (fseeko(stream, off, whence) == 0) ERR_STD(fseeko);
  return_if_fail (fread(ptr, size, 1, stream) == 1) ERR_STD(fread);
  return 0;
}


__THROW __nonnull()
int copy (FILE *dst, FILE *src, size_t len, unsigned int bsize);
__THROW __nonnull()
int dump (const char *path, FILE *src, size_t len, unsigned int bsize);
__THROW __nonnull((1, 2)) __attr_access((__write_only__, 5))
int copy_uncompress (
  FILE *dst, FILE *src, size_t len, unsigned int bsize, size_t *outlenp);

__THROW __nonnull((1, 2)) __attr_access((__write_only__, 3))
int write_lpe (FILE *dst, FILE *src, size_t *lenp, unsigned int bsize);
__THROW __nonnull((1, 2)) __attr_access((__read_only__, 1))
__attr_access((__write_only__, 3))
int dump_lpe (const char *path, FILE *src, size_t *lenp, unsigned int bsize);

__THROW __nonnull((1, 2)) __attr_access((__write_only__, 3))
__attr_access((__write_only__, 5))
int write_lpe_uncompress (
  FILE *dst, FILE *src, size_t *lenp, unsigned int bsize, size_t *outlenp);


#ifdef __cplusplus
}
#endif

#endif /* UTILS_H */
