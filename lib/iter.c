#define _BSD_SOURCE

#include <endian.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "include/iter.h"
#include "include/video.h"
#include "macro.h"
#include "log.h"


int PmlxzjLxePacketIter_next (struct PmlxzjLxePacketIter *iter) {
  return_if_fail (iter->frame_no < iter->frames_cnt) ERR(PL_ESTOP);

  return_if_fail (fseeko(iter->file, iter->end_offset, SEEK_SET) == 0)
    ERR_SYS(fseeko);
  iter->begin_offset = iter->end_offset;

  if (iter->frame_no < 0) {
    iter->frame_no = 0;
    return PmlxzjLxePacketIter_NEXT_FRAME;
  }

  bool is_cursor;
  size_t header_size;
  size_t stream_size;

  if (iter->frame_no == 0 && iter->frame_packet_no < 2) {
    if (iter->frame_packet_no == 0) {
      header_size =
        sizeof(iter->packet.cursor) - offsetof(struct PmlxzjLxeCursor, left);
      return_if_fail (
        fread(&iter->packet.cursor.left, header_size, 1, iter->file) == 1
      ) ERR_SYS(fread);

      iter->packet.cursor.frame_no_neg = htole32(INT32_MIN);

      is_cursor = true;
      stream_size = le32toh(iter->packet.cursor.size);
    } else {
      header_size =
        sizeof(iter->packet.image) - offsetof(struct PmlxzjLxeImage, size);
      return_if_fail (
        fread(&iter->packet.image.size, header_size, 1, iter->file) == 1
      ) ERR_SYS(fread);

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
    ) == 1) ERR_SYS(fread);
    int32_t frame_no = PmlxzjLxePacket_frame_no(&iter->packet);
    if (iter->frame_no != frame_no) {
      iter->frame_no = frame_no;
      iter->frame_packet_no = 0;
      return PmlxzjLxePacketIter_NEXT_FRAME;
    }

    is_cursor = PmlxzjLxePacket_is_cursor(&iter->packet);
    header_size = is_cursor ?
      sizeof(iter->packet.cursor) : sizeof(iter->packet.image);
    return_if_fail (fread(
      (unsigned char *) &iter->packet + 4, header_size - 4, 1, iter->file
    ) == 1) ERR_SYS(fread);

    stream_size = PmlxzjLxePacket_data_size(&iter->packet);
  }

  iter->frame_packet_no++;
  iter->offset = iter->begin_offset + header_size;
  iter->end_offset = iter->offset + stream_size;
  return is_cursor ?
    PmlxzjLxePacketIter_NEXT_CURSOR : PmlxzjLxePacketIter_NEXT_IMAGE;
}


int PmlxzjLxePacketIter_init (
    struct PmlxzjLxePacketIter *iter, const struct Pmlxzj *pl,
    int32_t frames_limit) {
  iter->packet = (union PmlxzjLxePacket) {};
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


int Pmlxzj_print_video (
    const struct Pmlxzj *pl, FILE *out, int32_t frames_limit) {
  struct PmlxzjLxePacketIter iter;
  PmlxzjLxePacketIter_init(&iter, pl, frames_limit);

  int ret = fputs("Stream header (Frame 0):\n", out);

  while (true) {
    int state = PmlxzjLxePacketIter_next(&iter);
    break_if_fail (state >= 0);

    if (state == PmlxzjLxePacketIter_NEXT_FRAME) {
      break_if_fail (iter.frame_no < iter.frames_cnt);
      if (iter.frame_no > 0) {
        ret += fprintf(out, "Frame: %" PRId32 "\n", iter.frame_no);
      }
    } else if (state == PmlxzjLxePacketIter_NEXT_CURSOR) {
      struct PmlxzjCursor cursor;
      PmlxzjCursor_init(&cursor, &iter.packet.cursor);
      ret += PmlxzjCursor_print(&cursor, out, iter.begin_offset);
    } else {
      struct PmlxzjImage image;
      PmlxzjImage_init(&image, &iter.packet.image);
      ret += PmlxzjImage_print(&image, out, iter.begin_offset);
    }
  }

  return ret;
}


int Pmlxzj_set_playlock (const struct Pmlxzj *pl, const char *password) {
  return_if_fail (pl->encrypted >= 0) ERR(PL_EKEY);

  bool setkey = password != NULL && password[0] != '\0';
  return_if_fail (pl->encrypted > 0 || setkey) 0;

  struct PmlxzjLxePacketIter iter;
  PmlxzjLxePacketIter_init(&iter, pl, -1);

  char key[20] = {};
  if (setkey) {
    char buf[22];
    strncpy(buf, password, sizeof(buf) - 1);
    pmlxzj_key_enc(key, buf);
  }
  if (pl->encrypted > 0) {
    for (unsigned int i = 0; i < 20; i++) {
      key[i] ^= pl->key[i];
    }
  }

  bool recrypt = (pl->encrypted > 0) == setkey;

  int ret;
  while (true) {
    int state = PmlxzjLxePacketIter_next(&iter);
    if_fail (state >= 0) {
      ret = state;
      goto fail;
    }

    if (state == PmlxzjLxePacketIter_NEXT_FRAME) {
      break_if_fail (iter.frame_no < iter.frames_cnt);
      continue;
    }

    if (state == PmlxzjLxePacketIter_NEXT_IMAGE) {
      size_t size = le32toh(iter.packet.image.size);
      if (size <= 10240) {
        continue;
      }

      unsigned char head[20];
      if (!recrypt) {
        if_fail (fseeko(pl->file, iter.offset + 4, SEEK_SET) == 0) {
          ret = ERR_SYS(fseeko);
          goto fail;
        }
        if_fail (fread(head, sizeof(head), 1, pl->file) == 1) {
          ret = ERR_SYS(fread);
          goto fail;
        }
      }

      unsigned char encrypted[20];
      if_fail (fseeko(pl->file, iter.offset + size / 2, SEEK_SET) == 0) {
        ret = ERR_SYS(fseeko);
        goto fail;
      }
      if_fail (fread(encrypted, sizeof(encrypted), 1, pl->file) == 1) {
        ret = ERR_SYS(fread);
        goto fail;
      }

      if_fail (fseeko(pl->file, iter.offset + size / 2, SEEK_SET) == 0) {
        ret = ERR_SYS(fseeko);
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
        ret = ERR_SYS(fwrite);
        goto fail;
      }
    }
  }

  if_fail (fseeko(pl->file, -sizeof(pl->footer) + offsetof(
      struct PmlxzjLxeFooter, editlock_key), SEEK_END) == 0) {
    ret = ERR_SYS(fseeko);
    goto fail;
  }
  uint32_t cksum = 0;
  if_fail (fwrite(&cksum, sizeof(cksum), 1, pl->file) == 1) {
    ret = ERR_SYS(fwrite);
    goto fail;
  }
  if (setkey) {
    cksum = htole32(pmlxzj_password_cksum(password));
  }
  if_fail (fwrite(&cksum, sizeof(cksum), 1, pl->file) == 1) {
    ret = ERR_SYS(fwrite);
    goto fail;
  }

  ret = 0;
fail:
  return ret;
}


int Pmlxzj_set_playlock_iconv (const struct Pmlxzj *pl, const char *password) {
  return_if_fail (pl->encrypted >= 0) ERR(PL_EKEY);

  bool setkey = password != NULL && password[0] != '\0';
  return_if_fail (pl->encrypted > 0 || setkey) 0;

  if (!setkey) {
    return Pmlxzj_set_playlock(pl, NULL);
  }

  char *buf = pmlxzj_iconv_enc_new(password, 0);
  return_if_fail (buf != NULL) -sc_exc.code;

  int ret = Pmlxzj_set_playlock(pl, buf);
  free(buf);
  return ret;
}

