#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/nowide.h"

#include "include/parser.h"
#include "macro.h"
#include "log.h"
#include "utils.h"


int Plzj_extract_txts (const struct Plzj *pl, const char *dir) {
  size_t dir_len = strlen(dir);
  char *path = malloc(dir_len + 65);
  return_if_fail (path != NULL) ERR_STD(malloc);
  memcpy(path, dir, dir_len);
  char *filename = path + dir_len;
  filename[0] = DIR_SEP;
  filename++;

  int ret;

  if_fail (fseeko(pl->file, pl->keyframes_offset, SEEK_SET) == 0) {
    ret = ERR_STD(fseeko);
    goto fail;
  }
  snprintf(filename, 64, "keyframes.txt");
  ret = dump(path, pl->file, pl->keyframes_size, 0);
  goto_if_fail (ret >= 0) fail;

  if (pl->clicks_offset != -1) {
    if_fail (fseeko(pl->file, pl->clicks_offset, SEEK_SET) == 0) {
      ret = ERR_STD(fseeko);
      goto fail;
    }
    snprintf(filename, 64, "clicks.txt");
    ret = dump(path, pl->file, pl->clicks_size, 0);
    goto_if_fail (ret >= 0) fail;
  }

fail:
  free(path);
  return ret;
}
