#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/parser.h"
#include "macro.h"
#include "log.h"
#include "utils.h"


int Pmlxzj_extract_txts (const struct Pmlxzj *pl, const char *dir) {
  return_if_fail (fseeko(
    pl->file, -sizeof(struct PmlxzjLxeFooter) -
              sizeof(struct PmlxzjLxePlayer), SEEK_END) == 0) ERR_SYS(fseeko);

  size_t dir_len = strlen(dir);
  char path[dir_len + 65];
  memcpy(path, dir, dir_len);
  char *filename = path + dir_len;
  filename[0] = '/';
  filename++;

  int ret;

  snprintf(filename, 64, "keyframes.txt");
  ret = dump_lse(path, pl->file);
  return_if_fail (ret >= 0) ret;

  if (pl->player.has_clicks) {
    snprintf(filename, 64, "clicks.txt");
    ret = dump_lse(path, pl->file);
    return_if_fail (ret >= 0) ret;
  }

  return 0;
}
