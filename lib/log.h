#ifndef LOG_H
#define LOG_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "include/err.h"
#include "include/log.h"


#define SC_LOG_DOMAIN "plzj"

#define ERR(ret) (sc_set_err(PL_E_PL, ret, __func__), -ret)
#define ERR_SYS(fn) (sc_set_errno(# fn), -PL_ESYS)
#define ERR_ZLIB(fn, res) (sc_set_err(PL_E_ZLIB, res, # fn), -PL_EZLIB)
#define ERR_PNG(fn) (sc_set_err(PL_E_PNG, -1, # fn), -PL_EPNG)


#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
