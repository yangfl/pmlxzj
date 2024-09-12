#ifndef IMAGE_H
#define IMAGE_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "include/defs.h"


extern const unsigned char depth5to8_table[];
extern const unsigned char depth6to8_table[];

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_const__
static inline unsigned char to_depth8 (
    unsigned char depth, unsigned char color) {
  return (depth == 5 ? depth5to8_table : depth6to8_table)[color];
}


#ifdef __cplusplus
}
#endif

#endif /* IMAGE_H */
