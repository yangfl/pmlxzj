#ifndef IMAGE_H
#define IMAGE_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "include/defs.h"


__attribute_artificial__ __attribute_warn_unused_result__ __attribute_const__
static inline unsigned char to_depth8 (
    unsigned char depth, unsigned char color) {
  // https://stackoverflow.com/questions/2442576
  return (depth == 5 ? color * 527 + 23 : color * 259 + 33) >> 6;
}


#ifdef __cplusplus
}
#endif

#endif /* IMAGE_H */
