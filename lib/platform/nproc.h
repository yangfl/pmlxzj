#ifndef PLATFORM_NPROC_H
#define PLATFORM_NPROC_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "../include/defs.h"


__attribute_warn_unused_result__ __attribute_const__ __THROW
int get_nproc (void);


#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_NPROC_H */
