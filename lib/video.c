#define _BSD_SOURCE

#include <endian.h>
#include <inttypes.h>
#include <png.h>
#include <stdbit.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "include/alg.h"
#include "include/parser.h"
#include "include/video.h"
#include "macro.h"
#include "bmp.h"
#include "log.h"
#include "threadpool.h"
#include "utils.h"


#define PMLXZJ_PNG_TRANSPARENT 222
#define PMLXZJ_PNG_DELAY_DEN 1000


struct png_acTL {
  uint32_t num_frames;
  uint32_t num_plays;
} __packed;


struct png_fcTL {
  uint32_t sequence_number;
  uint32_t width;
  uint32_t height;
  uint32_t x_offset;
  uint32_t y_offset;
  uint16_t delay_num;
  uint16_t delay_den;
  uint8_t dispose_op;
  uint8_t blend_op;
} __packed;

/* dispose_op flags from inside fcTL */
#define PNG_DISPOSE_OP_NONE        0x00U
#define PNG_DISPOSE_OP_BACKGROUND  0x01U
#define PNG_DISPOSE_OP_PREVIOUS    0x02U

/* blend_op flags from inside fcTL */
#define PNG_BLEND_OP_SOURCE        0x00U
#define PNG_BLEND_OP_OVER          0x01U


static inline size_t bmp_max_size (uint_fast32_t width, uint_fast32_t height) {
  // they use 0x1400, which seems too large
  return 2 * (width + (width & 1)) * height + 256;
}


static void pixel_canvas_copy (
    unsigned char *dst, const unsigned char *src,
    const struct PmlxzjRect *rect, uint_fast32_t width, uint_fast32_t height) {
  unused(height);

  uint_fast32_t cut_width = PmlxzjRect_width(rect);

  for (int_fast32_t y = rect->p1.y; y < rect->p2.y; y++) {
    size_t offset = 3 * (width * y + rect->p1.x);
    memcpy(dst + offset, src + offset, 3 * cut_width);
  }
}


static void pmlxzj_png_fcTL_init (
    struct png_fcTL *fcTL, const struct PmlxzjRect *rect, uint_fast32_t seq,
    uint_fast16_t delay, uint_fast16_t next_subframes_remain) {
  uint_fast16_t delay_num = delay == 0 ? 1 :
    PMLXZJ_PNG_DELAY_DEN / 5 * delay - next_subframes_remain;
  *fcTL = (struct png_fcTL) {
    htobe32(seq),
    htobe32(PmlxzjRect_width(rect)), htobe32(PmlxzjRect_height(rect)),
    htobe32(rect->p1.x), htobe32(rect->p1.y),
    htobe16(delay_num), htobe16(PMLXZJ_PNG_DELAY_DEN),
    PNG_DISPOSE_OP_NONE, PNG_BLEND_OP_OVER
  };
}


static unsigned char *pmlxzj_png_IDAT_gendiff (
    const unsigned char *pixels, const unsigned char *pixels_before,
    const struct PmlxzjRect *rect, uint_fast32_t width, uint_fast32_t height,
    size_t *lenp) {
  unused(height);

  uint_fast32_t cut_width = PmlxzjRect_width(rect);
  uint_fast32_t cut_height = PmlxzjRect_height(rect);
  uint_fast32_t cut_pixels_len = (1 + 3 * cut_width) * cut_height;

  unsigned char *cut_pixels = malloc(cut_pixels_len);
  if_fail (cut_pixels != NULL) {
    (void) ERR_SYS(malloc);
    return NULL;
  }

  for (uint_fast32_t y = 0; y < cut_height; y++) {
    cut_pixels[(1 + 3 * cut_width) * y] = 0;
    for (uint_fast32_t x = 0; x < cut_width; x++) {
      uint_fast32_t offset = 3 * (width * (y + rect->p1.y) + x + rect->p1.x);

      if (memcmp(pixels + offset, pixels_before + offset, 3) == 0) {
        for (unsigned int c = 0; c < 3; c++) {
          cut_pixels[(1 + 3 * cut_width) * y + 1 + 3 * x + c] =
            PMLXZJ_PNG_TRANSPARENT;
        }
      } else {
        for (unsigned int c = 0; c < 3; c++) {
          cut_pixels[(1 + 3 * cut_width) * y + 1 + 3 * x + c] =
            pixels[offset + c];
        }
      }
    }
  }

  if (lenp != NULL) {
    *lenp = cut_pixels_len;
  }
  return cut_pixels;
}


static void *pmlxzj_compress (
    const void *src, size_t srclen, size_t dstlen_before, size_t *dstlenp) {
  uLongf buflen = compressBound(srclen);
  Bytef *dst = malloc(dstlen_before + buflen);
  if_fail (dst != NULL) {
    (void) ERR_SYS(malloc);
    return NULL;
  }

  int res = compress2(
    dst + dstlen_before, &buflen, src, srclen, Z_BEST_COMPRESSION);
  if_fail (res == Z_OK) {
    (void) ERR_ZLIB(compress2, res);
    free(dst);
    return NULL;
  }

  size_t dstlen = buflen + dstlen_before;
  dst = realloc(dst, dstlen);

  if (dstlenp != NULL) {
    *dstlenp = dstlen;
  }
  return dst;
}


struct compress_worker {
  void *src;
  size_t srclen;
  size_t dstlen_before;
  void **dstp;
  size_t *dstlenp;
};


static int pmlxzj_compress_worker (void *arg) {
  struct compress_worker *worker = arg;
  void *dst = pmlxzj_compress(
    worker->src, worker->srclen, worker->dstlen_before, worker->dstlenp);
  *worker->dstp = dst;
  free(worker->src);
  free(worker);
  return dst == NULL ? -sc_exc.code : 0;
}


static int pmlxzj_compress_bg (
    void *src, size_t srclen, size_t dstlen_before, void **dstp,
    size_t *dstlenp, struct ThreadPool *pool) {
  if_fail (pool->err_i < 0) {
    struct ThreadPoolWorker *worker = pool->workers + pool->err_i;
    sc_exc = worker->exc;
    return worker->ret;
  }

  struct compress_worker *worker = malloc(sizeof(struct compress_worker));
  return_if_fail (worker != NULL) ERR_SYS(malloc);

  *worker = (struct compress_worker) {
    src, srclen, dstlen_before, dstp, dstlenp};
  int ret = ThreadPool_run(pool, pmlxzj_compress_worker, worker);
  if_fail (ret == 0) {
    free(src);
    free(worker);
    return ret;
  }

  return 0;
}


int PmlxzjLxePacketIter_init (
    struct PmlxzjLxePacketIter *iter, const struct Pmlxzj *pl,
    int_fast32_t frames_limit) {
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
      iter->packet.cursor.left = htole32(iter->width);
      iter->packet.cursor.top = htole32(iter->height);

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
    int_fast32_t frame_no = PmlxzjLxePacket_frame_no(&iter->packet);
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


int PmlxzjBuffer_init (
    struct PmlxzjBuffer *buf, FILE *file, size_t size, off_t offset) {
  if (offset > 0) {
    return_if_fail (fseeko(file, offset, SEEK_SET) == 0) ERR_SYS(fseeko);
  }

  unsigned char *data = malloc(size);
  return_if_fail (data != NULL) ERR_SYS(malloc);

  if_fail (fread(data, size, 1, file) == 1) {
    int ret = ERR_SYS(fread);
    free(data);
    return ret;
  }

  buf->size = size;
  buf->data = data;
  return 0;
}


int PmlxzjBuffer_init_seg (
    struct PmlxzjBuffer *buf, FILE *file, const struct PmlxzjSegment *seg) {
  return PmlxzjBuffer_init(buf, file, seg->size, seg->offset);
}


int PmlxzjCursor_print (
    const struct PmlxzjCursor *cursor, FILE *out, off_t offset) {
  int ret = fputs("  Type: cursor\n", out);
  if (offset > 0) {
    ret += fprintf(out, "    Offset: 0x%08jx\n", (intmax_t) offset);
  }
  ret += fprintf(out, "    Position: %4" PRIdLEAST32 "x%3" PRIdLEAST32 "\n",
                 cursor->p.x, cursor->p.y);
  if (cursor->seg.size) {
    ret += fprintf(out, "    Stream length: 0x%zx (%zd)\n",
                   cursor->seg.size, cursor->seg.size);
  }
  return ret;
}


int PmlxzjCursor_init_file (struct PmlxzjCursor *cursor, FILE *file) {
  struct PmlxzjLxeCursor h_cursor;
  return_if_fail (fread(&h_cursor, sizeof(h_cursor), 1, file) == 1)
    ERR_SYS(fread);
  return PmlxzjCursor_init(cursor, &h_cursor);
}


bool PmlxzjImage_valid (
    const struct PmlxzjImage *image,
    uint_fast32_t width, uint_fast32_t height) {
  return_if_fail (image->buf.data != NULL) false;

  return_if_fail (image->rect.p1.x >= 0 && image->rect.p2.x >= 0) false;
  return_if_fail (PmlxzjRect_valid(&image->rect)) false;
  return_if_fail ((unsigned int) image->rect.p2.x <= width &&
                  (unsigned int) image->rect.p2.y <= height) false;

  const BITMAPINFOHEADER *info = (const void *) (
    (const BITMAPFILEHEADER *) image->buf.data + 1);
  return_if_fail (
    PmlxzjRect_width(&image->rect) == le32toh(info->biWidth)) false;
  return_if_fail (
    PmlxzjRect_height(&image->rect) == le32toh(info->biHeight)) false;

  return_if_fail (
    le16toh(info->biBitCount) / 8 *
    (le32toh(info->biWidth) + (le32toh(info->biWidth) & 1)) *
    le32toh(info->biHeight) <= le32toh(info->biSizeImage)) false;

  return true;
}


int PmlxzjImage_apply (
    const struct PmlxzjImage *image, unsigned char *pixels,
    uint_fast32_t width, uint_fast32_t height) {
  return_if_fail (PmlxzjImage_valid(image, width, height)) ERR(PL_EINVAL);

  const BITMAPFILEHEADER *header = (const void *) image->buf.data;
  const BITMAPINFOHEADER *info = (const void *) (header + 1);

  // support BI_BITFIELDS only as it seems to be the only format ever used
  return_if_fail (le16toh(info->biBitCount) == 16) ERR(PL_ENOTSUP);
  return_if_fail (le32toh(info->biCompression) == BI_BITFIELDS) ERR(PL_ENOTSUP);

  // convert color depths
  const uint32_t *h_bitmasks =
    (const void *) ((const unsigned char *) info + le32toh(info->biSize));

  uint32_t bitmasks[3];
  int rshifts[3];
  unsigned char depths[3];

  for (unsigned int c = 0; c < 3; c++) {
    bitmasks[c] = le32toh(h_bitmasks[c]);
    rshifts[c] = stdc_trailing_zeros(bitmasks[c]);
    depths[c] = stdc_trailing_zeros((bitmasks[c] >> rshifts[c]) + 1);
  }

  // get bmp geometry
  uint_fast32_t patch_width = PmlxzjRect_width(&image->rect);
  uint_fast32_t patch_height = PmlxzjRect_height(&image->rect);
  uint_fast32_t patch_width_h = patch_width + (patch_width & 1);

  const uint16_t *patch = (const void *) (
    (const unsigned char *) image->buf.data + le32toh(header->bfOffBits));

  for (uint_fast32_t y = 0; y < patch_height; y++) {
    for (uint_fast32_t x = 0; x < patch_width; x++) {
      size_t offset = width * (y + image->rect.p1.y) + x + image->rect.p1.x;
      // bmp is upside down
      size_t patch_offset = patch_width_h * (patch_height - y - 1) + x;

      for (unsigned int c = 0; c < 3; c++) {
        pixels[3 * offset + c] = to_depth8(
          depths[c],
          (le16toh(patch[patch_offset]) & bitmasks[c]) >> rshifts[c]);
      }
    }
  }

  return 0;
}


int PmlxzjImage_print (
    const struct PmlxzjImage *image, FILE *out, off_t offset) {
  int ret = fputs("  Type: image\n", out);
  if (offset > 0) {
    ret += fprintf(out, "    Offset: 0x%08jx\n", (intmax_t) offset);
  }
  ret += fprintf(
    out,
    "    Position: "
    "%4" PRIdLEAST32 "x%3" PRIdLEAST32 "+"
    "%4" PRIdLEAST32 "x%3" PRIdLEAST32
    " (%" PRIuFAST32 "x%" PRIuFAST32 ")\n",
    image->rect.p1.x, image->rect.p1.y, image->rect.p2.x, image->rect.p2.y,
    PmlxzjRect_width(&image->rect), PmlxzjRect_height(&image->rect));
  ret += fprintf(
    out, "    Stream length: 0x%zx (%zd)\n", image->seg.size, image->seg.size);
  return ret;
}


static int PmlxzjImage_uncompress_rle (
    void *dst, size_t *dstlenp, const void *src, size_t srclen) {
  // U1JIEYASUO1SHIBAI ("U1解压缩1失败")
  size_t dstlen = *dstlenp;
  uint16_t *in = (void *) src;
  uint16_t *out = (void *) dst;
  size_t in_i = 0;
  size_t out_i = 0;
  for (; in_i < srclen / 2 - 3 && out_i < dstlen / 2; ) {
    if (in[in_i] == 0 && in[in_i + 1] == 0 && in[in_i + 2] != 0) {
      for (uint_fast16_t i = 0; i < le16toh(in[in_i + 2]); i++) {
        out[out_i] = in[in_i + 3];
        out_i++;
      }
      in_i += 4;
    } else {
      out[out_i] = in[in_i];
      in_i++;
      out_i++;
    }
  }
  for (; in_i < srclen / 2 && out_i < dstlen / 2; in_i++, out_i++) {
    out[out_i] = in[in_i];
  }

  *dstlenp = 2 * out_i;
  return in_i == srclen / 2 ? 0 : ERR(PL_EFORMAT);
}


static int PmlxzjImage_uncompress_zlib (
    void *dst, size_t *dstlenp, const void *src, size_t srclen) {
  uLongf dstlen = *dstlenp;

  int res = uncompress(
    dst, &dstlen, (const unsigned char *) src + 4, srclen - 4);
  return_if_fail (res == Z_OK) ERR_ZLIB(uncompress, res);

  *dstlenp = dstlen;
  return 0;
}


// lxefileplay::jieyasuobmp()
static int PmlxzjImage_uncompress (
    void *dst, size_t *dstlenp, const void *src, size_t srclen,
    unsigned int video_type) {
  if (*(uint64_t *) src == (uint64_t) -1) {
    // lxefileplay::jkjieyasuobmp
    return ERR(PL_ENOTSUP);
  }

  return_if_fail (
    video_type >= (PMLXZJ_VIDEO_RLE | PMLXZJ_VIDEO_ZLIB)) ERR(PL_ENOTSUP);
  return_if_fail ((video_type & PMLXZJ_VIDEO_RLE) != 0) ERR(PL_EFORMAT);

  void *buf = NULL;

  if ((video_type & PMLXZJ_VIDEO_ZLIB) != 0) {
    size_t buflen = le32toh(*(uint32_t *) src);
    buf = malloc(buflen);
    return_if_fail (buf != NULL) ERR_SYS(malloc);

    int ret = PmlxzjImage_uncompress_zlib(buf, &buflen, src, srclen);
    if_fail (ret == 0) {
      free(buf);
      return ret;
    }

    src = buf;
    srclen = buflen;
  }

  int ret = PmlxzjImage_uncompress_rle(dst, dstlenp, src, srclen);
  free(buf);
  return ret;
}


int PmlxzjImage_read (
    struct PmlxzjImage *image, FILE *file, unsigned int video_type,
    const void *key) {
  return_if_fail (PmlxzjRect_valid(&image->rect)) ERR(PL_EFORMAT);

  size_t size = bmp_max_size(
    PmlxzjRect_width(&image->rect), PmlxzjRect_height(&image->rect));
  unsigned char *data = malloc(size);
  return_if_fail (data != NULL) ERR_SYS(malloc);

  int ret;

  {
    struct PmlxzjBuffer raw;
    ret = PmlxzjBuffer_init_seg(&raw, file, &image->seg);
    goto_if_fail (ret == 0) fail;

    pmlxzj_image_encdec(raw.data, raw.size, key);
    ret = PmlxzjImage_uncompress(data, &size, raw.data, raw.size, video_type);

    PmlxzjBuffer_destroy(&raw);
  }
  goto_if_fail (ret == 0) fail;

  return_if_fail (data[0] == 'B' && data[1] == 'M') ERR(PL_EFORMAT);
  return_if_fail (size == le32toh(((BITMAPFILEHEADER *) data)->bfSize))
    ERR(PL_EFORMAT);

  image->buf.size = size;
  image->buf.data = data;
  return 0;

fail:
  free(data);
  return ret;
}


int PmlxzjImage_init_file (struct PmlxzjImage *image, FILE *file) {
  struct PmlxzjLxeImage h_image;
  return_if_fail (fread(&h_image, sizeof(h_image), 1, file) == 1)
    ERR_SYS(fread);
  return PmlxzjImage_init(image, &h_image);
}


int PmlxzjFrame_apply (
    const struct PmlxzjFrame *frame, unsigned char *pixels,
    uint_fast32_t width, uint_fast32_t height) {
  int ret = 0;

  for (size_t i = 0; i < frame->patches_cnt; i++) {
    ret = PmlxzjImage_apply(frame->patches[i], pixels, width, height);
    break_if_fail (ret == 0);
  }

  return ret;
}


struct PmlxzjImage *PmlxzjFrame_new_patch (struct PmlxzjFrame *frame) {
  struct PmlxzjImage **patches = realloc(
    frame->patches, sizeof(struct PmlxzjImage *) * (frame->patches_cnt + 1));
  if_fail (patches != NULL) {
    (void) ERR_SYS(realloc);
    return NULL;
  }
  frame->patches = patches;

  struct PmlxzjImage *patch = malloc(sizeof(struct PmlxzjImage));
  if_fail (patch != NULL) {
    (void) ERR_SYS(malloc);
    return NULL;
  }

  patches[frame->patches_cnt] = patch;
  frame->patches_cnt++;

  return patch;
}


struct PmlxzjPngFrame {
  struct PmlxzjRect rect;
  uint_least32_t frame_i;
  uint_least32_t subframes_remain;

  void *fdAT;
  size_t size;
};


int PmlxzjVideo_write_apng (
    const struct PmlxzjVideo *video, const struct Pmlxzj *pl, FILE *out,
    bool use_subframes, unsigned int nproc) {
  return_if_fail (
    video->frames != NULL && video->frames[0] != NULL &&
    video->frames[0]->patches != NULL &&
    video->frames[0]->patches[0] != NULL) 0;
  return_if_fail (pl->encrypted >= 0) ERR(PL_EKEY);

  struct PmlxzjImage *first = video->frames[0]->patches[0];
  uint_fast32_t width = first->rect.p2.x;
  uint_fast32_t height = first->rect.p2.y;

  size_t png_frames_cnt = 0;
  for (size_t i = 0; i < video->frames_cnt; i++) {
    struct PmlxzjFrame *frame = video->frames[i];
    if (frame->patches_cnt > 0) {
      png_frames_cnt += !use_subframes ? 1 : frame->patches_cnt;
    }
  }
  sc_info("Orig frames: %" PRIdLEAST32 ", APNG frames: %zu\n",
          video->frames_cnt, png_frames_cnt);

  // acquire resources
  struct ThreadPool pool;
  return_with_nonzero (ThreadPool_init(&pool, nproc, "png"));

  int ret;

  size_t pixels_len = 3 * width * height;
  unsigned char *pixels = malloc(2 * pixels_len);
  if_fail (pixels != NULL) {
    ret = ERR_SYS(malloc);
    goto fail_pixels;
  }
  unsigned char *pixels_before = pixels + pixels_len;

  struct PmlxzjPngFrame *png_frames = malloc(
    sizeof(struct PmlxzjPngFrame) * (png_frames_cnt + 1));
  if_fail (png_frames != NULL) {
    ret = ERR_SYS(malloc);
    goto fail_png_frames;
  }
  for (size_t i = 0; i < png_frames_cnt; i++) {
    png_frames[i].fdAT = NULL;
  }
  png_frames[png_frames_cnt].frame_i = video->frames_cnt;

  png_structp png_ptr = png_create_write_struct(
    PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if_fail (png_ptr != NULL) {
    ret = ERR_PNG(png_create_write_struct);
    goto fail_png;
  }
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if_fail (info_ptr != NULL) {
    ret = ERR_PNG(png_create_info_struct);
    goto fail_info;
  }
  png_init_io(png_ptr, out);

  // write png header
  if_fail (setjmp(png_jmpbuf(png_ptr)) == 0) {
    ret = ERR_SYS(setjmp);
    goto fail;
  }

  png_set_IHDR(
    png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB,
    PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  static const png_color_16 trans_values[] = {{
    .red = PMLXZJ_PNG_TRANSPARENT,
    .green = PMLXZJ_PNG_TRANSPARENT,
    .blue = PMLXZJ_PNG_TRANSPARENT,
  }};
  static const png_byte trans[] = {0};
  png_set_tRNS(png_ptr, info_ptr, trans, 1, trans_values);

  static const png_color_8 sig_bit = {.red = 5, .green = 6, .blue = 5};
  png_set_sBIT(png_ptr, info_ptr, &sig_bit);

  char exif[38 + 64 + 64] =
    "II"  // byte align
    "\x2A\0"  // TAG Mark
    "\x08\0\0\0"  // offset to first IFD

    "\x02\0" // No. of directory entry

    "\x0e\x01"  // tag (ImageDescription) as title
    "\x02\0"  // format (ascii strings)
    "\0\0\0\0"  // number of components
    "\x26\0\0\0"  // offset

    "\x98\x82"  // tag (Copyright) as infotext
    "\x02\0"  // format (ascii strings)
    "\0\0\0\0"  // number of components
    "\0\0\0\0"  // offset

    "\0\0\0\0"  // offset to next IFD
  ;
  size_t title_len = Pmlxzj_get_title(pl, &exif[38], 2 * 64);
  sc_info("Player title: %s\n", title_len == 0 ? "(null)" : exif + 38);
  if (title_len > 0) {
    title_len++;
  }
  *(uint32_t *) (exif + 14) = htole32(title_len);
  *(uint32_t *) (exif + 30) = htole32(38 + title_len);
  size_t infotext_len =
    Pmlxzj_get_infotext(pl, exif + 38 + title_len, 2 * 64 - title_len);
  sc_info("Info watermark: %s\n",
          infotext_len == 0 ? "(null)" : exif + 38 + title_len);
  if (infotext_len > 0) {
    infotext_len++;
  }
  *(uint32_t *) (exif + 26) = htole32(infotext_len);
  png_set_eXIf_1(
    png_ptr, info_ptr, 38 + title_len + infotext_len, (png_bytep) exif);

  png_write_info(png_ptr, info_ptr);

  struct png_acTL acTL = {htobe32(png_frames_cnt), htobe32(0)};
  png_write_chunk(
    png_ptr, (const void *) "acTL", (const void *) &acTL, sizeof(acTL));

  // process frames
  memset(pixels_before, PMLXZJ_PNG_TRANSPARENT, pixels_len);

  size_t png_frame_i = 0;
  for (size_t i = 0; i < video->frames_cnt; i++) {
    if ((i + 1) % 32 == 0 || i + 1 >= video->frames_cnt) {
      sc_notice(
        "[%3.f%%] %zu / %u f, %zu / %zu APNG f\r",
        (i + 1) * 100. / video->frames_cnt, i + 1, video->frames_cnt,
        png_frame_i, png_frames_cnt);
    }

    struct PmlxzjFrame *frame = video->frames[i];
    if (frame->patches_cnt <= 0) {
      continue;
    }

    struct PmlxzjRect rect;
    if (!use_subframes) {
      PmlxzjRect_init(&rect);
    }

    for (size_t j = 0; j < frame->patches_cnt; j++) {
      struct PmlxzjImage *patch = frame->patches[j];
      bool read = patch->buf.data == NULL;

      if (read) {
        ret = PmlxzjImage_read(
          patch, pl->file, le32toh(pl->player.video_type),
          pl->encrypted == 0 ? NULL : pl->key);
        goto_if_fail (ret == 0) fail;
      }

      ret = PmlxzjImage_apply(patch, pixels, width, height);

      if (read) {
        free(patch->buf.data);
        patch->buf.data = NULL;
      }
      goto_if_fail (ret == 0) fail;

      if (!use_subframes) {
        PmlxzjRect_extend(&rect, &patch->rect);
      } else {
        rect = patch->rect;
      }

      if (j + 1 >= frame->patches_cnt || use_subframes) {
        size_t size;
        unsigned char *IDAT = pmlxzj_png_IDAT_gendiff(
          pixels, pixels_before, &rect, width, height, &size);
        if_fail (IDAT != NULL) {
          ret = -sc_exc.code;
          goto fail;
        }

        struct PmlxzjPngFrame *png_frame = png_frames + png_frame_i;
        png_frame_i++;

        ret = pmlxzj_compress_bg(
          IDAT, size, 4, &png_frame->fdAT, &png_frame->size, &pool);
        goto_if_fail (ret == 0) fail;

        pixel_canvas_copy(pixels_before, pixels, &rect, width, height);

        png_frame->rect = rect;
        png_frame->frame_i = i;
        png_frame->subframes_remain = frame->patches_cnt - (j + 1);
      }
    }
  }
  sc_notice("\n");
  ThreadPool_stop(&pool);

  // write frames
  uint_fast32_t apng_seq = 0;
  for (size_t i = 0; i < png_frames_cnt; i++) {
    struct PmlxzjPngFrame *png_frame = png_frames + i;

    struct png_fcTL fcTL;
    pmlxzj_png_fcTL_init(
      &fcTL, &png_frame->rect, apng_seq,
      png_frames[i + 1].frame_i - png_frame->frame_i,
      png_frames[i + 1].subframes_remain);
    apng_seq++;

    png_write_chunk(
      png_ptr, (const void *) "fcTL", (const void *) &fcTL, sizeof(fcTL));

    if (i == 0) {
      png_write_chunk(
        png_ptr, (const void *) "IDAT",
        (const void *) ((const unsigned char *) png_frame->fdAT + 4),
        png_frame->size - 4);
    } else {
      *(uint32_t *) png_frame->fdAT = htobe32(apng_seq);
      apng_seq++;
      png_write_chunk(
        png_ptr, (const void *) "fdAT", png_frame->fdAT, png_frame->size);
    }
  }
  png_write_chunk(png_ptr, (const void *) "IEND", NULL, 0);

  ret = 0;
  if (0) {
fail:
    sc_notice("\n");
  }
  for (size_t i = 0; i < png_frames_cnt; i++) {
    free(png_frames[i].fdAT);
  }
fail_info:
  png_destroy_write_struct(&png_ptr, &info_ptr);
fail_png:
  free(png_frames);
fail_png_frames:
  free(pixels);
fail_pixels:
  ThreadPool_destroy(&pool);
  return ret;
}


int PmlxzjVideo_save_apng (
    const struct PmlxzjVideo *video, const struct Pmlxzj *pl, const char *path,
    bool use_subframes, unsigned int nproc) {
  return_if_fail (
    video->frames != NULL && video->frames[0] != NULL &&
    video->frames[0]->patches != NULL &&
    video->frames[0]->patches[0] != NULL) 0;
  return_if_fail (pl->encrypted >= 0) ERR(PL_EKEY);

  FILE *out = fopen(path, "wb");
  return_if_fail (out != NULL) ERR_SYS(fopen);
  int ret = PmlxzjVideo_write_apng(video, pl, out, use_subframes, nproc);
  fclose(out);
  return ret;
}


int PmlxzjVideo_save_cursors (
    const struct PmlxzjVideo *video, FILE *in, const char *dir) {
  size_t dir_len = strlen(dir);
  char path[dir_len + 65];
  memcpy(path, dir, dir_len);
  char *filename = path + dir_len;
  filename[0] = '/';
  filename++;

  snprintf(filename, 64, "cursors.txt");
  FILE *out_cursors = fopen(path, "w");
  return_if_fail (out_cursors != NULL) ERR_SYS(fopen);

  unsigned long cksum = 0;
  int ret = 0;

  for (size_t i = 0; i < video->frames_cnt; i++) {
    struct PmlxzjCursor *cursor = &video->frames[i]->cursor;

    if (cursor->seg.size > 0) {
      struct PmlxzjBuffer *buf = &cursor->buf;
      bool read = buf->data == NULL;

      if (read) {
        ret = PmlxzjCursor_read(cursor, in);
        goto_if_fail (ret == 0) fail;
      }

      cksum = pmlxzj_crc32(buf->data, buf->size);
      snprintf(filename, 64, "c_%08lx.ico", cksum);
      if (access(path, F_OK) != 0) {
        FILE *out_icon = fopen(path, "wb");
        if_fail (out_icon != NULL) {
          ret = ERR_SYS(fopen);
          goto fail_buf;
        }
        if_fail (fwrite(buf->data, buf->size, 1, out_icon) == 1) {
          ret = ERR_SYS(fwrite);
          fclose(out_icon);
          goto fail_buf;
        }
        fclose(out_icon);
      }

      ret = 0;
fail_buf:
      if (read) {
        free(buf->data);
        buf->data = NULL;
      }
      goto_if_fail (ret == 0) fail;
    }

    if (cursor->seg.offset <= 0) {
      sc_warning("cursor info missing for frame %zu\n", i);
      ret = fputs(".\n", out_cursors);
    } else {
      ret = fprintf(
        out_cursors, "%" PRIdLEAST32 " %" PRIdLEAST32 " %08lx\n",
        cursor->p.x, cursor->p.y, cksum);
    }
    if_fail (ret >= 0) {
      ret = ERR_SYS(fprintf);
      goto fail;
    }
  }

  ret = 0;
fail:
  fclose(out_cursors);
  return ret;
}


struct PmlxzjFrame *PmlxzjVideo_new_frame (struct PmlxzjVideo *video) {
  struct PmlxzjFrame **frames = realloc(
    video->frames, sizeof(struct PmlxzjFrame *) * (video->frames_cnt + 1));
  if_fail (frames != NULL) {
    (void) ERR_SYS(realloc);
    return NULL;
  }
  video->frames = frames;

  struct PmlxzjFrame *frame = malloc(sizeof(struct PmlxzjFrame));
  if_fail (frame != NULL) {
    (void) ERR_SYS(malloc);
    return NULL;
  }

  frames[video->frames_cnt] = frame;
  video->frames_cnt++;

  PmlxzjFrame_init(frame);
  return frame;
}


int PmlxzjVideo_init (
    struct PmlxzjVideo *video, const struct Pmlxzj *pl,
    int_fast32_t frames_limit) {
  struct PmlxzjLxePacketIter iter;
  PmlxzjLxePacketIter_init(&iter, pl, frames_limit);

  video->frames = NULL;
  video->frames_cnt = 0;

  struct PmlxzjFrame *frame = NULL;
  int ret;

  while (true) {
    int state = PmlxzjLxePacketIter_next(&iter);
    if_fail (state >= 0) {
      ret = state;
      goto fail;
    }

    if (state == PmlxzjLxePacketIter_NEXT_FRAME) {
      break_if_fail (iter.frame_no < iter.frames_cnt);
      frame = PmlxzjVideo_new_frame(video);
    } else if (frame != NULL) {
      if (state == PmlxzjLxePacketIter_NEXT_CURSOR) {
        struct PmlxzjCursor *cursor = &frame->cursor;
        PmlxzjCursor_init(cursor, &iter.packet.cursor);
        cursor->seg.offset = iter.offset;
      } else {
        struct PmlxzjImage *patch = PmlxzjFrame_new_patch(frame);
        PmlxzjImage_init(patch, &iter.packet.image);
        patch->seg.offset = iter.offset;
      }
    }
  }

  return 0;

fail:
  PmlxzjVideo_destroy(video);
  return ret;
}


int Pmlxzj_extract_video_or_cursor (
    const struct Pmlxzj *pl, const char *dir, int_fast32_t frames_limit,
    bool use_subframes, unsigned int nproc,
    bool extract_video, bool extract_cursor) {
  return_if_fail (extract_video || extract_cursor) 0;

  struct PmlxzjVideo video;
  return_with_nonzero (PmlxzjVideo_init(&video, pl, frames_limit));

  int ret;

  if (extract_video) {
    size_t dir_len = strlen(dir);
    char path[dir_len + 65];
    memcpy(path, dir, dir_len);
    char *filename = path + dir_len;
    filename[0] = '/';
    filename++;
    snprintf(filename, 64, "video.apng");

    ret = PmlxzjVideo_save_apng(&video, pl, path, use_subframes, nproc);
    goto_if_fail (ret == 0) fail;
  }
  if (extract_cursor) {
    ret = PmlxzjVideo_save_cursors(&video, pl->file, dir);
    goto_if_fail (ret == 0) fail;
  }

  ret = 0;
fail:
  PmlxzjVideo_destroy(&video);
  return ret;
}


int Pmlxzj_print_video (
    const struct Pmlxzj *pl, FILE *out, int_fast32_t frames_limit) {
  struct PmlxzjLxePacketIter iter;
  PmlxzjLxePacketIter_init(&iter, pl, frames_limit);

  int ret = fputs("Stream header (Frame 0):\n", out);

  while (true) {
    int state = PmlxzjLxePacketIter_next(&iter);
    break_if_fail (state >= 0);

    if (state == PmlxzjLxePacketIter_NEXT_FRAME) {
      break_if_fail (iter.frame_no < iter.frames_cnt);
      if (iter.frame_no > 0) {
        ret += fprintf(out, "Frame: %" PRIdLEAST32 "\n", iter.frame_no);
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
  return_if_fail (buf != NULL) ERR_SYS(malloc);

  int ret = Pmlxzj_set_playlock(pl, buf);
  free(buf);
  return ret;
}
