#ifndef LOG_H
#define LOG_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "include/err.h"
#include "include/log.h"


#define SC_LOG_DOMAIN "plzj"

#define __STR_ERR_FN(fn) # fn

#define ERR_WHAT(ret, what) (sc_set_err(SC_ED_PLZJ, ret, __func__, what), -ret)
#define ERR(ret) ERR_WHAT(ret, NULL)
#define ERR_FMT(ret, ...) \
  (sc_set_err_fmt(SC_ED_PLZJ, ret, __func__, __VA_ARGS__), -ret)
#define ERR_STD(fn) (sc_set_errno(__STR_ERR_FN(fn), NULL), -PL_ESTD)
#define ERR_WIN32(fn) (sc_set_err_win32(__STR_ERR_FN(fn), NULL), -PL_EWIN32)
#define ERR_ZLIB(fn, res) \
  (sc_set_err(SC_ED_ZLIB, res, __STR_ERR_FN(fn), NULL), -PL_EZLIB)
#define ERR_PNG(fn) \
  (sc_set_err(SC_ED_LIBPNG, -1, __STR_ERR_FN(fn), NULL), -PL_EPNG)


#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
