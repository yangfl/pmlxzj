#ifndef UTILS_H
#define UTILS_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>
#include <zlib.h>

#include "include/defs.h"


__nonnull()
typedef int (*stream_fn) (FILE *, FILE *, size_t);


__wur __attribute_pure__ __nonnull() __attr_access((__read_only__, 1, 2))
static inline unsigned long pmlxzj_crc32 (const void *buf, size_t len) {
  return crc32_z(crc32_z(0, Z_NULL, 0), buf, len);
}

__THROW __nonnull()
int copy (FILE *dst, FILE *src, size_t len);
__THROW __nonnull()
int copy_uncompress (FILE *dst, FILE *src, size_t len);

__THROW __nonnull()
int write_lpe (FILE *dst, FILE *src);
__THROW __nonnull() __attr_access((__read_only__, 1))
int dump_lpe (const char *path, FILE *src);

__THROW __nonnull()
int write_lpe_uncompress (FILE *dst, FILE *src);

__THROW __nonnull()
int write_lse (FILE *dst, FILE *src);
__THROW __nonnull() __attr_access((__read_only__, 1))
int dump_lse (const char *path, FILE *src);


#ifdef __cplusplus
}
#endif

#endif /* UTILS_H */
