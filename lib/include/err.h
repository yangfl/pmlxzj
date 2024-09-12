#ifndef PLZJ_ERR_H
#define PLZJ_ERR_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "defs.h"


#define PL_E_PLZJ         0x706c7a6alu
#define PL_E_ZLIB       0x7a6c6962lu
#define PL_E_PNG        0x00504e47lu

#define PL_ESYS         1
#define PL_EZLIB        2
#define PL_EPNG         3

#define PL_EINVAL       9
#define PL_ESTOP        10
#define PL_EFORMAT      11
#define PL_ENOTSUP      12
#define PL_EKEY         13

__attribute_warn_unused_result__ __attribute_const__
const char *plzj_strerror (int ret);


#ifdef __cplusplus
}
#endif

#endif /* PLZJ_ERR_H */
