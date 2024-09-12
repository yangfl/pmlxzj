#ifndef PLZJ_ERR_H
#define PLZJ_ERR_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "defs.h"


#ifndef SC_ED_PLZJ
#  define SC_ED_PLZJ      0x706c7a6alu
#endif
#ifndef SC_ED_ZLIB
#  define SC_ED_ZLIB      0x7a6c6962lu
#endif
#ifndef SC_ED_LIBPNG
#  define SC_ED_LIBPNG    0x00504e47lu
#endif

#define PL_ESTD         1
#define PL_EWIN32       2
#define PL_EZLIB        3
#define PL_EPNG         4

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
