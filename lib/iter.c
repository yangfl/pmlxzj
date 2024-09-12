#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "include/platform/endian.h"

#include "include/iter.h"
#include "include/video.h"
#include "macro.h"
#include "log.h"
#include "utils.h"


int PlzjLxePacketIter_next (struct PlzjLxePacketIter *iter) {
  return_if_fail (iter->frame_no < iter->frames_cnt) ERR(PL_ESTOP);

  return_if_fail (fseeko(iter->file, iter->end_offset, SEEK_SET) == 0)
    ERR_STD(fseeko);
  iter->begin_offset = iter->end_offset;

  if (iter->frame_no < 0) {
    iter->frame_no = 0;
    return PlzjLxePacketIter_NEXT_FRAME;
  }

  bool is_cursor;
  size_t header_size;
  size_t stream_size;

  if (iter->frame_no == 0 && iter->frame_packet_no < 2) {
    if (iter->frame_packet_no == 0) {
      header_size =
        sizeof(iter->packet.cursor) - offsetof(struct PlzjLxeCursor, left);
      return_if_fail (
        fread(&iter->packet.cursor.left, header_size, 1, iter->file) == 1
      ) ERR_STD(fread);

      iter->packet.cursor.frame_no_neg = htole32(INT32_MIN);

      is_cursor = true;
      stream_size = le32toh(iter->packet.cursor.size);
    } else {
      header_size =
        sizeof(iter->packet.image) - offsetof(struct PlzjLxeImage, size);
      return_if_fail (
        fread(&iter->packet.image.size, header_size, 1, iter->file) == 1
      ) ERR_STD(fread);

      iter->packet.image.frame_no = htole32(INT32_MAX);
      iter->packet.image.left = 0;
      iter->packet.image.top = 0;
      iter->packet.image.right = htole32(iter->width);
      iter->packet.image.bottom = htole32(iter->height);

      is_cursor = false;
      stream_size = le32toh(iter->packet.image.size);
    }
  } else {
    return_if_fail (fread(
      &iter->packet.varient_no, sizeof(iter->packet.varient_no), 1, iter->file
    ) == 1) ERR_STD(fread);
    int32_t frame_no = PlzjLxePacket_frame_no(&iter->packet);
    if (iter->frame_no != frame_no) {
      iter->frame_no = frame_no;
      iter->frame_packet_no = 0;
      return PlzjLxePacketIter_NEXT_FRAME;
    }

    is_cursor = PlzjLxePacket_is_cursor(&iter->packet);
    header_size = is_cursor ?
      sizeof(iter->packet.cursor) : sizeof(iter->packet.image);
    return_if_fail (fread(
      (unsigned char *) &iter->packet + 4, header_size - 4, 1, iter->file
    ) == 1) ERR_STD(fread);

    stream_size = PlzjLxePacket_data_size(&iter->packet);
  }

  iter->frame_packet_no++;
  iter->offset = iter->begin_offset + header_size;
  iter->end_offset = iter->offset + stream_size;
  return is_cursor ?
    PlzjLxePacketIter_NEXT_CURSOR : PlzjLxePacketIter_NEXT_IMAGE;
}


int PlzjLxePacketIter_init (
    struct PlzjLxePacketIter *iter, const struct Plzj *pl,
    int32_t frames_limit) {
  iter->packet = (union PlzjLxePacket) {0};
  iter->frame_no = -1;
  iter->frame_packet_no = pl->video.has_cursor ? 0 : 1;
  iter->frames_cnt = le32toh(pl->video.frames_cnt);
  if (frames_limit >= 0 && frames_limit < iter->frames_cnt) {
    iter->frames_cnt = frames_limit;
  }

  iter->file = pl->file;

  iter->width = le32toh(pl->video.width);
  iter->height = le32toh(pl->video.height);

  iter->end_offset = pl->video_offset + sizeof(pl->video);
  return 0;
}


int Plzj_print_video (const struct Plzj *pl, FILE *out, int32_t frames_limit) {
  struct PlzjLxePacketIter iter;
  PlzjLxePacketIter_init(&iter, pl, frames_limit);

  int ret = fputs("Stream header (Frame 0):\n", out);

  while (true) {
    int state = PlzjLxePacketIter_next(&iter);
    break_if_fail (state >= 0);

    if (state == PlzjLxePacketIter_NEXT_FRAME) {
      break_if_fail (iter.frame_no < iter.frames_cnt);
      if (iter.frame_no > 0) {
        ret += fprintf(out, "Frame: %" PRId32 "\n", iter.frame_no);
      }
    } else if (state == PlzjLxePacketIter_NEXT_CURSOR) {
      struct PlzjCursor cursor;
      PlzjCursor_init(&cursor, &iter.packet.cursor);
      ret += PlzjCursor_print(&cursor, out, iter.begin_offset);
    } else {
      struct PlzjImage image;
      PlzjImage_init(&image, &iter.packet.image);
      ret += PlzjImage_print(&image, out, iter.begin_offset);
    }
  }

  return ret;
}


int Plzj_set_playlock (struct Plzj *pl, const char *password) {
  return_if_fail (pl->key_set >= 0) ERR(PL_EKEY);

  bool setkey = password != NULL && password[0] != '\0';
  return_if_fail (pl->key_set > 0 || setkey) 0;

  struct PlzjLxePacketIter iter;
  PlzjLxePacketIter_init(&iter, pl, -1);

  char key[20] = {0};
  if (setkey) {
    char buf[22];
    strncpy(buf, password, sizeof(buf) - 1);
    plzj_key_enc(key, buf);
  }
  if (pl->key_set > 0) {
    for (unsigned int i = 0; i < 20; i++) {
      key[i] ^= pl->key[i];
    }
  }

  bool recrypt = (pl->key_set > 0) == setkey;

  int ret;
  while (true) {
    int state = PlzjLxePacketIter_next(&iter);
    if_fail (state >= 0) {
      ret = state;
      goto fail;
    }

    if (state == PlzjLxePacketIter_NEXT_FRAME) {
      break_if_fail (iter.frame_no < iter.frames_cnt);
      continue;
    }

    if (state == PlzjLxePacketIter_NEXT_IMAGE) {
      size_t size = le32toh(iter.packet.image.size);
      if (size <= 10240) {
        continue;
      }

      unsigned char head[20];
      if (!recrypt) {
        ret = read_at(pl->file, iter.offset + 4, SEEK_SET, head, sizeof(head));
        goto_if_fail (ret == 0) fail;
      }

      unsigned char encrypted[20];
      ret = read_at(
        pl->file, iter.offset + size / 2, SEEK_SET, encrypted,
        sizeof(encrypted));
      goto_if_fail (ret == 0) fail;

      if_fail (fseeko(pl->file, iter.offset + size / 2, SEEK_SET) == 0) {
        ret = ERR_STD(fseeko);
        goto fail;
      }
      if (!recrypt) {
        for (unsigned int i = 0; i < 20; i++) {
          encrypted[i] ^= head[i];
        }
      }
      for (unsigned int i = 0; i < 20; i++) {
        encrypted[i] ^= key[i];
      }
      if_fail (fwrite(encrypted, sizeof(encrypted), 1, pl->file) == 1) {
        ret = ERR_STD(fwrite);
        goto fail;
      }
    }
  }

  if_fail (fseeko(
      pl->file, pl->end_offset + PLZJ_OFFSET_FOOTER + offsetof(
        struct PlzjLxeFooter, editlock_key), SEEK_SET) == 0) {
    ret = ERR_STD(fseeko);
    goto fail;
  }
  uint32_t cksum = 0;
  if_fail (fwrite(&cksum, sizeof(cksum), 1, pl->file) == 1) {
    ret = ERR_STD(fwrite);
    goto fail;
  }
  if (setkey) {
    cksum = htole32(plzj_password_cksum(password));
  }
  if_fail (fwrite(&cksum, sizeof(cksum), 1, pl->file) == 1) {
    ret = ERR_STD(fwrite);
    goto fail;
  }

  if (!setkey) {
    pl->key_set = 0;
  } else {
    char buf[22];
    strncpy(buf, password, sizeof(buf) - 1);
    plzj_key_enc(pl->key, buf);
    pl->key_set = 1;
  }
  pl->footer.editlock_key = le32toh(0);
  pl->footer.playlock_cksum = le32toh(cksum);

  ret = 0;
fail:
  return ret;
}


int Plzj_set_playlock_iconv (struct Plzj *pl, const char *password) {
  return_if_fail (pl->key_set >= 0) ERR(PL_EKEY);

  bool setkey = password != NULL && password[0] != '\0';
  return_if_fail (pl->key_set > 0 || setkey) 0;

  if (!setkey) {
    return Plzj_set_playlock(pl, NULL);
  }

  char *buf = plzj_iconv_enc_new(password, 0);
  return_if_fail (buf != NULL) -sc_exc.code;

  int ret = Plzj_set_playlock(pl, buf);
  free(buf);
  return ret;
}


int PlzjFile_set_playlock (struct PlzjFile *pf, const char *password) {
  return_if_fail (PlzjFile_valid(pf)) ERR(PL_EFORMAT);

  int ret;
  for (uint32_t i = 0; i < pf->sections_cnt; i++) {
    ret = Plzj_set_playlock(pf->sections + i, password);
    return_if_fail (ret >= 0) ret;
  }

  if (pf->extended) {
    const struct PlzjLxeFooter *footer = &pf->sections[0].footer;

    return_if_fail (fseeko(
      pf->file, PLZJ_OFFSET_FOOTER_SECTIONED + offsetof(
        struct PlzjLxeFooterSectioned, editlock_key), SEEK_END
    ) == 0) ERR_STD(fseeko);
    return_if_fail (fwrite(
      &footer->editlock_key, sizeof(footer->editlock_key), 1, pf->file
    ) == 1) ERR_STD(fwrite);
    return_if_fail (fwrite(
      &footer->playlock_cksum, sizeof(footer->playlock_cksum), 1, pf->file
    ) == 1) ERR_STD(fwrite);
  }
  return 0;
}


int PlzjFile_set_playlock_iconv (struct PlzjFile *pf, const char *password) {
  return_if_fail (PlzjFile_valid(pf)) ERR(PL_EFORMAT);

  bool setkey = password != NULL && password[0] != '\0';
  if (!setkey) {
    return PlzjFile_set_playlock(pf, NULL);
  }

  char *buf = plzj_iconv_enc_new(password, 0);
  return_if_fail (buf != NULL) -sc_exc.code;

  int ret = PlzjFile_set_playlock(pf, buf);
  free(buf);
  return ret;
}
