#include <string.h>
#include <zlib.h>

#include "macro.h"
#include "log.h"


static const char *const errstrs[] = {
  "Invalid argument",
  "Stopped",
  "File format error or damaged",
  "Format not supported yet",
  "Password required",
};


const char *plzj_strerror (int ret) {
  if_fail (ret >= PL_EINVAL && ret <= PL_EKEY) {
    static thread_local char buf[128];
    snprintf(buf, sizeof(buf), "Unknown error number %d", ret);
    return buf;
  }

  return errstrs[ret - PL_EINVAL];
}


__attribute_constructor__
static void err_init (void) {
  sc_register_exc_handler(SC_ED_PLZJ, plzj_strerror, NULL);
  sc_register_exc_handler(SC_ED_ZLIB, zError, NULL);
}
