#include <string.h>
#include <threads.h>
#include <zlib.h>

#include "macro.h"
#include "log.h"


const char *pmlxzj_strerror (int ret) {
  switch (ret) {
    case PL_EINVAL:
      return "Invalid argument";
    case PL_ESTOP:
      return "Stopped";
    case PL_EFORMAT:
      return "File format error or damaged";
    case PL_ENOTSUP:
      return "Format not supported yet";
    case PL_EKEY:
      return "Password required";
    default:
      static thread_local char buf[128];
      snprintf(buf, sizeof(buf), "Unknown error number %d", ret);
      return buf;
  }
}


__attribute__((constructor))
static void err_init () {
  sc_register_strerror(PL_E_PL, pmlxzj_strerror);
  sc_register_strerror(PL_E_ZLIB, zError);
}
