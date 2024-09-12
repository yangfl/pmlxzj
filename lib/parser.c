#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/platform/endian.h"
#include "platform/nowide.h"

#include "include/alg.h"
#include "include/parser.h"
#include "macro.h"
#include "log.h"
#include "utils.h"


size_t Plzj_get_caption (const struct Plzj *pl, char *dst, size_t size) {
  return plzj_iconv_dec(dst, size, pl->section.caption, strnlen(
    pl->section.caption, sizeof(pl->section.caption)));
}


size_t Plzj_get_title (const struct Plzj *pl, char *dst, size_t size) {
  return plzj_iconv_dec(dst, size, pl->player.title, strnlen(
    pl->player.title, sizeof(pl->player.title)));
}


size_t Plzj_get_infotext (const struct Plzj *pl, char *dst, size_t size) {
  return plzj_infotext_dec_iconv(dst, size, pl->video.infotext);
}


bool Plzj_is_registered (const struct Plzj *pl) {
  return plzj_regcode_check(pl->video.regcode1, pl->video.regcode2);
}


int Plzj_set_password (struct Plzj *pl, const char *password, bool force) {
  return_if_fail (pl->key_set < 0) 2;

  bool ok = plzj_password_cksum(password) == le32toh(pl->footer.playlock_cksum);
  return_if_fail (ok || force) ERR(PL_EINVAL);

  char buf[22];
  strncpy(buf, password, sizeof(buf) - 1);
  plzj_key_enc(pl->key, buf);
  pl->key_set = 1;
  return ok ? 0 : 1;
}


int Plzj_set_password_iconv (
    struct Plzj *pl, const char *password, bool force) {
  return_if_fail (pl->key_set < 0) 2;

  char *buf = plzj_iconv_enc_new(password, 0);
  return_if_fail (buf != NULL) ERR_STD(malloc);

  int ret = Plzj_set_password(pl, buf, force);
  free(buf);
  return ret;
}


int Plzj_print_info (const struct Plzj *pl, FILE *out, bool in_section) {
  int ret = 0;

  if (in_section) {
    char caption[128] = "";
    Plzj_get_caption(pl, caption, sizeof(caption));
    ret += fmprintf(out, "Section caption: %s\n",
                    caption[0] == '\0' ? "(null)" : caption);
  }

  char title[128] = "";
  Plzj_get_title(pl, title, sizeof(title));
  ret += fmprintf(out, "Player title: %s\n",
                  title[0] == '\0' ? "(null)" : title);

  char infotext[128] = "";
  Plzj_get_infotext(pl, infotext, sizeof(infotext));
  ret += fmprintf(out, "Info watermark: %s\n",
                  infotext[0] == '\0' ? "(null)" : infotext);
  if (infotext[0] != '\0') {
    char fontname[64] = "";
    plzj_iconv_dec(
      fontname, sizeof(fontname), pl->video.infotextfontname,
      strnlen(pl->video.infotextfontname, sizeof(pl->video.infotextfontname)));
    ret += fmprintf(
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

  unsigned long frames_cnt = le32toh(pl->video.frames_cnt);
  unsigned long frame_ms = le32toh(pl->video.frame_ms);
  float elapsed = frames_cnt * frame_ms / 1000.;
  unsigned long minutes = elapsed / 60;
  float seconds = elapsed - 60 * minutes;
  ret += fprintf(
    out,
    "Elapsed: %lu:%02.1f (%.1f s, %lu frames, FPS = %.2f)\n\n",
    minutes, seconds, elapsed, frames_cnt, 1000. / frame_ms);

  uint32_t editlock_key = le32toh(pl->footer.editlock_key);
  uint32_t playlock_cksum = le32toh(pl->footer.playlock_cksum);
  if (editlock_key != 0) {
    ret += fprintf(out, "Lock state: Edit locked, key = %" PRIu32 "\n",
                   editlock_key);
  } else if (playlock_cksum != 0) {
    ret += fprintf(
      out, "Lock state: Play locked, password checksum = %" PRIu32 "\n",
      playlock_cksum);
  } else {
    ret += fputs("Lock state: Unlocked\n", out);
  }
  ret += fprintf(out, "Registered: %s\n",
                 Plzj_is_registered(pl) ? "Yes" : "No");
  ret += fprintf(
    out, "%s offset: 0x%08jx, type %" PRIu32 "\n", "Video",
    (uintmax_t) pl->video_offset, le32toh(pl->player.video_type));
  if (pl->audio_offset == -1) {
    ret += fputs("Audio offset: (no audio)\n", out);
  } else {
    ret += fprintf(
      out, "%s offset: 0x%08jx, type %" PRIu32 "\n", "Audio",
      (uintmax_t) pl->audio_offset, le32toh(pl->player.audio_type));
  }
  ret += fprintf(
    out, "%s txt offset: 0x%08jx, size %" PRIu32 "\n", "Keyframes",
    (uintmax_t) pl->keyframes_offset, pl->keyframes_size);
  if (pl->clicks_offset == -1) {
    ret += fputs("Clicks txt offset: (no clicks)\n", out);
  } else {
    ret += fprintf(
      out, "%s txt offset: 0x%08jx, size %" PRIu32 "\n", "Clicks",
      (uintmax_t) pl->clicks_offset, pl->clicks_size);
  }

  return ret;
}


int Plzj_init (
    struct Plzj *pl, FILE *file, const struct PlzjLxeSection *section) {
  return_if_fail (
    (section->begin_offset_hi == 0 && section->end_offset_hi == 0) ||
    sizeof(off_t) >= 8) ERR_WHAT(PL_EINVAL, "_FILE_OFFSET_BITS=64 required");

  off_t reset_offset = ftello(file);
  return_if_fail (reset_offset != -1) ERR_STD(ftello);

  pl->begin_offset =
    ((off_t) le32toh(section->begin_offset_hi) << 32) |
    le32toh(section->begin_offset);
  pl->end_offset =
    ((off_t) le32toh(section->end_offset_hi) << 32) |
    le32toh(section->end_offset);

  int ret;

  // footer
  ret = read_at(
    file, pl->end_offset + PLZJ_OFFSET_FOOTER, SEEK_SET, &pl->footer,
    sizeof(pl->footer));
  goto_if_fail (ret == 0) fail;

  // player
  ret = read_at(
    file, pl->end_offset + PLZJ_OFFSET_FOOTER - sizeof(pl->player), SEEK_SET,
    &pl->player, sizeof(pl->player));
  goto_if_fail (ret == 0) fail;

  // txts
  uint32_t size_h;
  ret = read_at(
    file,
    pl->end_offset + PLZJ_OFFSET_FOOTER - sizeof(pl->player) - sizeof(size_h),
    SEEK_SET, &size_h, sizeof(size_h));
  goto_if_fail (ret == 0) fail;

  pl->keyframes_size = le32toh(size_h);
  pl->keyframes_offset =
    pl->end_offset + PLZJ_OFFSET_FOOTER - sizeof(pl->player) -
    sizeof(size_h) - pl->keyframes_size;

  if (pl->player.has_clicks == 0) {
    pl->clicks_size = 0;
    pl->clicks_offset = -1;
  } else {
    ret = read_at(
      file, pl->keyframes_offset - sizeof(size_h), SEEK_SET,
      &size_h, sizeof(size_h));
    goto_if_fail (ret == 0) fail;

    pl->clicks_size = le32toh(size_h);
    pl->clicks_offset = pl->keyframes_offset - sizeof(size_h) - pl->clicks_size;
  }

  // audio / video
  int32_t variant_offset_h;
  ret = read_at(
    file, pl->begin_offset, SEEK_SET, &variant_offset_h,
    sizeof(variant_offset_h));
  goto_if_fail (ret == 0) fail;

  int32_t variant_offset = le32toh(variant_offset_h);
  pl->audio_new_format = variant_offset <= 0;
  if (variant_offset <= 0) {
    // New format
    pl->video_offset = pl->begin_offset + sizeof(variant_offset_h);
    if_fail (variant_offset != -1 || sizeof(off_t) >= 8) {
      ret = ERR_WHAT(PL_EINVAL, "_FILE_OFFSET_BITS=64 required");
      goto fail;
    }
    pl->audio_offset =
      variant_offset == 0 ? -1 :
      variant_offset == -1 ? (off_t) le64toh(pl->footer.audio_offset64) :
      -variant_offset;
  } else {
    // Legacy format
    pl->video_offset = variant_offset;
    pl->audio_offset = pl->begin_offset + sizeof(variant_offset_h);
  }

  ret = read_at(
    file, pl->video_offset, SEEK_SET, &pl->video, sizeof(pl->video));
  goto_if_fail (ret == 0) fail;

  // lock state
  uint32_t editlock_key = le32toh(pl->footer.editlock_key);
  uint32_t playlock_cksum = le32toh(pl->footer.playlock_cksum);
  if (editlock_key != 0) {
    if_fail (playlock_cksum == 0) {
      ret = ERR(PL_EFORMAT);
      goto fail;
    }
    char buf[22] = {0};
    snprintf(buf, sizeof(buf), "%" PRIu32, editlock_key);
    plzj_key_enc(pl->key, buf);
    pl->key_set = 1;
  } else if (playlock_cksum != 0) {
    pl->key_set = -1;
  } else {
    pl->key_set = 0;
  }

  pl->file = file;
  pl->section = *section;

  ret = 0;
fail:
  fseeko(file, reset_offset, SEEK_SET);
  return ret;
}


static bool PlzjFile_lockstate_consistent (const struct PlzjFile *pf) {
  if (pf->sections_cnt <= 1) {
    return true;
  }
  return_if_fail (pf->extended) false;

  for (uint32_t i = 0; i < pf->sections_cnt; i++) {
    const struct Plzj *pl = pf->sections + i;
    return_if_fail (
      pl->footer.editlock_key == pf->extfooter.editlock_key &&
      pl->footer.playlock_cksum == pf->extfooter.playlock_cksum) false;
  }

  return true;
}


static int _PlzjFile_set_password (
    const struct PlzjFile *pf, const char *password, bool force) {
  bool ok = true;
  for (uint32_t i = 0; i < pf->sections_cnt; i++) {
    int ret = Plzj_set_password(pf->sections + i, password, force);
    return_if_fail (ret >= 0) ret;
    if (ret == 1) {
      ok = false;
    }
  }
  return ok ? 0 : 1;
}


int PlzjFile_set_password (
    const struct PlzjFile *pf, const char *password, bool force) {
  return_if_fail (PlzjFile_valid(pf)) ERR(PL_EFORMAT);
  return_if_fail (PlzjFile_lockstate_consistent(pf)) ERR(PL_EFORMAT);

  return _PlzjFile_set_password(pf, password, force);
}


int PlzjFile_set_password_iconv (
    const struct PlzjFile *pf, const char *password, bool force) {
  return_if_fail (PlzjFile_valid(pf)) ERR(PL_EFORMAT);
  return_if_fail (PlzjFile_lockstate_consistent(pf)) ERR(PL_EFORMAT);

  char *buf = plzj_iconv_enc_new(password, 0);
  return_if_fail (buf != NULL) ERR_STD(malloc);

  int ret = _PlzjFile_set_password(pf, buf, force);
  free(buf);
  return ret;
}


void PlzjFile_destroy (const struct PlzjFile *pf) {
  for (uint32_t i = 0; i < pf->sections_cnt; i++) {
    Plzj_destroy(pf->sections + i);
  }
  free(pf->sections);

  if (pf->file != NULL) {
    fclose(pf->file);
  }
}


int PlzjFile_init (struct PlzjFile *pf, FILE *file) {
  off_t reset_offset = ftello(file);
  return_if_fail (reset_offset != -1) ERR_STD(ftello);

  pf->sections = NULL;

  int ret;

  if_fail (fseeko(file, 0, SEEK_END) == 0) {
    ret = ERR_STD(fseeko);
    goto fail;
  }
  pf->file_size = ftello(file);

  // footer
  char magic[PLZJ_MAGIC_LEN];
  ret = read_at(file, -(off_t) sizeof(magic), SEEK_END, &magic, sizeof(magic));
  goto_if_fail (ret == 0) fail;

  pf->extended = strcmp(magic, PLZJ_MAGIC_SECTIONED) == 0;
  if (!pf->extended) {
    if_fail (strcmp(magic, PLZJ_MAGIC) == 0) {
      ret = ERR(PL_EFORMAT);
      goto fail;
    }
    pf->sections_cnt = 1;
  } else {
    ret = read_at(
      file, PLZJ_OFFSET_FOOTER_SECTIONED, SEEK_END,
      &pf->extfooter, sizeof(pf->extfooter));
    goto_if_fail (ret == 0) fail;
    pf->sections_cnt = le32toh(pf->extfooter.sections_cnt);
  }

  size_t sections_len = sizeof(pf->sections[0]) * pf->sections_cnt;
  pf->sections = malloc(sections_len);
  if_fail (pf->sections != NULL) {
    ret = ERR_STD(malloc);
    goto fail;
  }

  struct PlzjLxeSection section;
  if (!pf->extended) {
    struct PlzjLxeFooter footer;
    ret = read_at(file, PLZJ_OFFSET_FOOTER, SEEK_END, &footer, sizeof(footer));
    goto_if_fail (ret == 0) fail;

    section = (struct PlzjLxeSection) {0};
    section.begin_offset = footer.data_offset;
    section.end_offset = pf->file_size;
    if (sizeof(pf->file_size) >= 8) {
      section.end_offset_hi = pf->file_size >> 32;
    }
    ret = Plzj_init(pf->sections, file, &section);
    goto_if_fail (ret == 0) fail;
  } else {
    for (uint32_t i = 0; i < pf->sections_cnt; i++) {
      ret = read_at(
        file,
        PLZJ_OFFSET_FOOTER_SECTIONED -
        (off_t) sizeof(struct PlzjLxeSection) * (pf->sections_cnt - i),
        SEEK_END, &section, sizeof(section));
      goto_if_fail (ret == 0) fail_section;

      ret = Plzj_init(pf->sections + i, file, &section);
      goto_if_fail (ret == 0) fail_section;

      if (0) {
fail_section:
        for (; i > 0; ) {
          i--;
          Plzj_destroy(pf->sections + i);
        }
        goto fail;
      }
    }
  }

  // lock state
  if_fail (PlzjFile_lockstate_consistent(pf)) {
    sc_warning("Lock state inconsistent across sections\n");
  }

  pf->file = file;

  ret = 0;
  if (0) {
fail:
    free(pf->sections);
  }
  fseeko(file, reset_offset, SEEK_SET);
  return ret;
}


int PlzjFile_init_file (
    struct PlzjFile *pf, const char *path, const char *mode) {
  FILE *file = mfopen(path, mode);
  return_if_fail (file != NULL) ERR_STD(mfopen);

  int ret = PlzjFile_init(pf, file);
  if_fail (ret == 0) {
    fclose(file);
  }
  return ret;
}
