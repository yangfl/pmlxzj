#define _BSD_SOURCE

#include <endian.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/alg.h"
#include "include/parser.h"
#include "macro.h"
#include "log.h"
#include "utils.h"


size_t Pmlxzj_get_title (const struct Pmlxzj *pl, char *dst, size_t size) {
  return pmlxzj_iconv_dec(dst, size, pl->player.title, strnlen(
    pl->player.title, sizeof(pl->player.title)));
}


size_t Pmlxzj_get_infotext (const struct Pmlxzj *pl, char *dst, size_t size) {
  return pmlxzj_infotext_dec_iconv(dst, size, pl->video.infotext);
}


bool Pmlxzj_is_registered (const struct Pmlxzj *pl) {
  return pmlxzj_regcode_check(pl->video.regcode1, pl->video.regcode2);
}


int Pmlxzj_set_password (struct Pmlxzj *pl, const char *password, bool force) {
  return_if_fail (pl->encrypted < 0) ERR(PL_EINVAL);

  bool ok = pmlxzj_password_cksum(password) ==
    le32toh(pl->footer.playlock_cksum);
  if (ok || force) {
    char buf[22];
    strncpy(buf, password, sizeof(buf) - 1);
    pmlxzj_key_enc(pl->key, buf);
    pl->encrypted = 1;
  }
  return ok ? 0 : ERR(PL_EINVAL);
}


int Pmlxzj_set_password_iconv (
    struct Pmlxzj *pl, const char *password, bool force) {
  return_if_fail (pl->encrypted < 0) ERR(PL_EINVAL);

  char *buf = pmlxzj_iconv_enc_new(password, 0);
  return_if_fail (buf != NULL) ERR_SYS(malloc);

  int ret = Pmlxzj_set_password(pl, buf, force);
  free(buf);
  return ret;
}


int Pmlxzj_print_info (const struct Pmlxzj *pl, FILE *out) {
  int ret = 0;

  char title[128] = "";
  Pmlxzj_get_title(pl, title, sizeof(title));
  ret += fprintf(out, "Player title: %s\n",
                 title[0] == '\0' ? "(null)" : title);

  char infotext[128] = "";
  Pmlxzj_get_infotext(pl, infotext, sizeof(infotext));
  if (infotext[0] == '\0') {
    ret += fputs("Info watermark: (null)\n", out);
  } else {
    ret += fprintf(out, "Info watermark: %s\n", infotext);

    char fontname[64] = "";
    pmlxzj_iconv_dec(
      fontname, sizeof(fontname), pl->video.infotextfontname,
      strnlen(pl->video.infotextfontname, sizeof(pl->video.infotextfontname)));
    ret += fprintf(
      out,
      "Info watermark style: pos %" PRIu32 " x %" PRIu32 ", size %" PRIu32
      ", font %s, color #%06" PRIx32 "",
      le32toh(pl->video.infox), le32toh(pl->video.infoy),
      le32toh(pl->video.infotextfontsize),
      fontname[0] == '\0' ? "(unspecified)" : fontname,
      le32toh(pl->video.infotextfontcolor));
    if (pl->video.infotextfontstyle.fsBold != 0) {
      ret += fputs(", bold", out);
    }
    if (pl->video.infotextfontstyle.fsItalic != 0) {
      ret += fputs(", italic", out);
    }
    if (pl->video.infotextfontstyle.fsUnderline != 0) {
      ret += fputs(", underline", out);
    }
    if (pl->video.infotextfontstyle.fsStrikeOut != 0) {
      ret += fputs(", strike out", out);
    }
    ret += fputs("\n", out);
  }

  ret += fprintf(out, "Resolution: %" PRIu32 "x%" PRIu32 "\n",
                 le32toh(pl->video.width), le32toh(pl->video.height));

  uint_fast32_t frames_cnt = le32toh(pl->video.frames_cnt);
  uint_fast32_t frame_ms = le32toh(pl->video.frame_ms);
  float elapsed = frames_cnt * frame_ms / 1000.;
  uint_fast32_t minutes = elapsed / 60;
  float seconds = elapsed - 60 * minutes;
  ret += fprintf(
    out,
    "Elapsed: %" PRIuFAST32 ":%02.1f (%.1f s, %" PRIuFAST32
    " frames, FPS = %.2f)\n\n",
    minutes, seconds, elapsed, frames_cnt, 1000. / frame_ms);

  if (pl->footer.editlock_key != 0) {
    ret += fprintf(out, "Lock state: Edit locked, key = %" PRIu32 "\n",
                   le32toh(pl->footer.editlock_key));
  } else if (pl->footer.playlock_cksum != 0) {
    ret += fprintf(
      out, "Lock state: Play locked, password checksum = %" PRIu32 "\n",
      le32toh(pl->footer.playlock_cksum));
  } else {
    ret += fputs("Lock state: Unlocked\n", out);
  }
  ret += fprintf(out, "Registered: %s\n",
                 Pmlxzj_is_registered(pl) ? "Yes" : "No");
  ret += fprintf(out, "Video offset: 0x%08" PRIx32 "\n",
                 le32toh(pl->video_offset));
  ret += fprintf(out, "Video type: %" PRIu32 "\n",
                 le32toh(pl->player.video_type));
  if (pl->audio_offset == 0) {
    ret += fputs("Audio offset: (no audio)\n", out);
  } else {
    ret += fprintf(out, "Audio offset: 0x%08" PRIx32 "\n",
                   le32toh(pl->audio_offset));
    ret += fprintf(out, "Audio type: %" PRIu32 "\n",
                   le32toh(pl->player.audio_type));
  }

  return ret;
}


void Pmlxzj_destroy (struct Pmlxzj *pl) {
  fclose(pl->file);
}


int Pmlxzj_init (struct Pmlxzj *pl, FILE *file) {
  off_t offset = ftello(file);
  return_if_fail (offset >= 0) ERR_SYS(ftello);

  int ret;

  if_fail (fseeko(file, 0, SEEK_END) == 0) {
    ret = ERR_SYS(fseeko);
    goto fail;
  }
  pl->file_size = ftello(file);

  // footer
  if_fail (fseeko(file, -sizeof(pl->footer), SEEK_END) == 0) {
    ret = ERR_SYS(fseeko);
    goto fail;
  }
  if_fail (fread(&pl->footer, sizeof(pl->footer), 1, file) == 1) {
    ret = ERR_SYS(fread);
    goto fail;
  }
  if_fail (strcmp(pl->footer.magic, PMLXZJ_MAGIC) == 0) {
    ret = ERR(PL_EFORMAT);
    goto fail;
  }

  // player
  if_fail (fseeko(
      file, -sizeof(pl->footer) - sizeof(pl->player), SEEK_END) == 0) {
    ret = ERR_SYS(fseeko);
    goto fail;
  }
  if_fail (fread(&pl->player, sizeof(pl->player), 1, file) == 1) {
    ret = ERR_SYS(fread);
    goto fail;
  }

  // audio/video
  uint_fast32_t data_offset = le32toh(pl->footer.data_offset);
  if_fail (fseeko(file, data_offset, SEEK_SET) == 0) {
    ret = ERR_SYS(fseeko);
    goto fail;
  }

  int32_t variant_offset_i32;
  if_fail (fread(
      &variant_offset_i32, sizeof(variant_offset_i32), 1, file) == 1) {
    ret = ERR_SYS(fread);
    goto fail;
  }
  int_fast32_t variant_offset = le32toh(variant_offset_i32);
  pl->audio_new_format = variant_offset <= 0;
  if (variant_offset <= 0) {
    // New format
    pl->video_offset = data_offset + sizeof(variant_offset_i32);
    pl->audio_offset = -variant_offset;
  } else {
    // Legacy format
    pl->video_offset = variant_offset;
    pl->audio_offset = data_offset + sizeof(variant_offset_i32);
  }

  if_fail (fseeko(file, pl->video_offset, SEEK_SET) == 0) {
    ret = ERR_SYS(fseeko);
    goto fail;
  }
  if_fail (fread(&pl->video, sizeof(pl->video), 1, file) == 1) {
    ret = ERR_SYS(fread);
    goto fail;
  }

  // edit lock key
  if (pl->footer.editlock_key != 0) {
    if_fail (pl->footer.playlock_cksum == 0) {
      ret = ERR(PL_EFORMAT);
      goto fail;
    }
    char buf[22] = {};
    snprintf(buf, sizeof(buf), "%" PRIu32, le32toh(pl->footer.editlock_key));
    pmlxzj_key_enc(pl->key, buf);
    pl->encrypted = 1;
  } else if (pl->footer.playlock_cksum != 0) {
    pl->encrypted = -1;
  } else {
    pl->encrypted = 0;
  }

  pl->file = file;
  return 0;

fail:
  fseeko(file, offset, SEEK_SET);
  return ret;
}


int Pmlxzj_init_file (struct Pmlxzj *pl, const char *path, const char *mode) {
  FILE *file = fopen(path, mode);
  return_if_fail (file != NULL) ERR_SYS(fopen);

  int ret = Pmlxzj_init(pl, file);
  if_fail (ret == 0) {
    fclose(file);
  }
  return ret;
}
