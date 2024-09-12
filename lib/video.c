#define _BSD_SOURCE

#include <ctype.h>
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
#include "include/iter.h"
#include "include/parser.h"
#include "include/video.h"
#include "macro.h"
#include "gdi.h"
#include "image.h"
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


struct PmlxzjPngFrame {
  struct PmlxzjRect rect;
  uint32_t timecode_ms;
  uint32_t patches_remain;

  void *fdAT;
  size_t size;
};


struct PmlxzjEncoder {
  struct PmlxzjPngFrame **frames;
  size_t len;

  unsigned char *canvas;
  uint32_t width;
  uint32_t height;

  struct ThreadPool pool;

  bool end;
};


__attribute_artificial__
static inline size_t bmp_max_size (uint32_t width, uint32_t height) {
  // they use 0x1400, which seems too large
  return 2 * (width + (width & 1)) * height + 256;
}


static void pixel_canvas_copy (
    unsigned char *dst, const unsigned char *src,
    const struct PmlxzjRect *rect, uint32_t width, uint32_t height) {
  (void) height;

  uint32_t cut_width = PmlxzjRect_width(rect);
  uint32_t cut_height = PmlxzjRect_height(rect);
  return_if_fail (cut_width > 0 && cut_height > 0);

  for (int32_t y = rect->p1.y; y < rect->p2.y; y++) {
    size_t offset = 3 * (width * y + rect->p1.x);
    memcpy(dst + offset, src + offset, 3 * cut_width);
  }
}


static bool pmlxzj_cursor_valid (const unsigned char *cursor, size_t size) {
  const ICONHEADER *header = (const void *) cursor;
  const ICONDIRENTRY *entry = (const void *) (header + 1);

  return_if_fail (le16toh(header->idType) == 1) false;
  return_if_fail (le16toh(header->idCount) >= 1) false;

  return_if_fail (le16toh(entry->bColorCount) >= 1) false;
  return_if_fail (
    le32toh(entry->dwDIBSize) + le32toh(entry->dwDIBOffset) <= size) false;

  const BITMAPINFOHEADER *info = (const void *) (
    cursor + le32toh(entry->dwDIBOffset));

  return_if_fail (le16toh(entry->bWidth) == le32toh(info->biWidth)) false;
  return_if_fail (2 * le16toh(entry->bHeight) == le32toh(info->biHeight)) false;

  uint16_t biBitCount = le16toh(info->biBitCount);
  return_if_fail (
    biBitCount == 1 || biBitCount == 2 || biBitCount == 4 || biBitCount == 8
  ) false;
  return_if_fail (le32toh(info->biCompression) == BI_RGB) false;

  return true;
}


static bool pmlxzj_canvas_diff (
    const unsigned char *dst, const unsigned char *src,
    uint32_t width, uint32_t height, struct PmlxzjRect *rect) {
  for (uint32_t y = 0; y < height; y++) {
    if (memcmp(dst + 3 * width * y, src + 3 * width * y, 3 * width) != 0) {
      rect->p1.y = y;
      rect->p2.y = y + 1;
      rect->p1.x = 0;
      rect->p2.x = width;
      goto p2y;
    }
  }
  return false;

p2y:
  for (uint32_t y = height; y > 0; ) {
    y--;
    if (memcmp(dst + 3 * width * y, src + 3 * width * y, 3 * width) != 0) {
      rect->p2.y = y + 1;
      break;
    }
  }

  for (uint32_t x = 0; x < width; x++) {
    for (int32_t y = rect->p1.y; y < rect->p2.y; y++) {
      size_t offset = 3 * (width * y + x);
      if (memcmp(dst + offset, src + offset, 3) != 0) {
        rect->p1.x = x;
        goto p2x;
      }
    }
  }

p2x:
  for (uint32_t x = width; x > 0; ) {
    x--;
    for (int32_t y = rect->p1.y; y < rect->p2.y; y++) {
      size_t offset = 3 * (width * y + x);
      if (memcmp(dst + offset, src + offset, 3) != 0) {
        rect->p2.x = x + 1;
        goto end;
      }
    }
  }

end:
  return true;
}


static unsigned char *pmlxzj_png_gendiff (
    const unsigned char *canvas, const unsigned char *canvas_before,
    const struct PmlxzjRect *rect, uint32_t width, uint32_t height,
    size_t *lenp) {
  (void) height;

  uint32_t cut_width = PmlxzjRect_width(rect);
  uint32_t cut_height = PmlxzjRect_height(rect);
  size_t cut_canvas_len = (1 + 3 * cut_width) * cut_height;

  unsigned char *cut_canvas = malloc(cut_canvas_len);
  if_fail (cut_canvas != NULL) {
    (void) ERR_SYS(malloc);
    return NULL;
  }

  for (uint32_t y = 0; y < cut_height; y++) {
    cut_canvas[(1 + 3 * cut_width) * y] = 0;
    for (uint32_t x = 0; x < cut_width; x++) {
      size_t offset = 3 * (width * (y + rect->p1.y) + x + rect->p1.x);
      size_t cut_offset = (1 + 3 * cut_width) * y + 1 + 3 * x;
      if (memcmp(canvas + offset, canvas_before + offset, 3) == 0) {
        memset(cut_canvas + cut_offset, PMLXZJ_PNG_TRANSPARENT, 3);
      } else {
        memcpy(cut_canvas + cut_offset, canvas + offset, 3);
      }
    }
  }

  *lenp = cut_canvas_len;
  return cut_canvas;
}


static void pmlxzj_png_fcTL_init (
    struct png_fcTL *fcTL, const struct PmlxzjRect *rect, uint32_t seq,
    uint16_t delay_ms, uint16_t next_patches_remain) {
  uint16_t delay_num = delay_ms == 0 ? 1 :
    delay_ms * PMLXZJ_PNG_DELAY_DEN / 1000 - next_patches_remain;
  *fcTL = (struct png_fcTL) {
    htobe32(seq),
    htobe32(PmlxzjRect_width(rect)), htobe32(PmlxzjRect_height(rect)),
    htobe32(rect->p1.x), htobe32(rect->p1.y),
    htobe16(delay_num), htobe16(PMLXZJ_PNG_DELAY_DEN),
    PNG_DISPOSE_OP_NONE, PNG_BLEND_OP_OVER
  };
}


static int pmlxzj_png_write_info (
    png_structp png_ptr, uint32_t width, uint32_t height,
    const struct Pmlxzj *pl) {
  png_infop info_ptr = png_create_info_struct(png_ptr);
  return_if_fail (info_ptr != NULL) ERR_PNG(png_create_info_struct);

  int ret;

  int res = setjmp(png_jmpbuf(png_ptr));
  if_fail (res == 0) {
    ret = ERR_PNG(png_jmpbuf);
    goto fail;
  }

  png_set_IHDR(
    png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB,
    PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  static const png_color_16 trans_color = {
    .red = PMLXZJ_PNG_TRANSPARENT,
    .green = PMLXZJ_PNG_TRANSPARENT,
    .blue = PMLXZJ_PNG_TRANSPARENT,
  };
  png_set_tRNS(png_ptr, info_ptr, NULL, 1, &trans_color);

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

  ret = 0;
fail:
  png_destroy_info_struct(png_ptr, &info_ptr);
  return ret;
}


static int pmlxzj_png_write_frames (
    png_structp png_ptr, const struct PmlxzjEncoder *encoder) {
  int res = setjmp(png_jmpbuf(png_ptr));
  return_if_fail (res == 0) ERR_PNG(png_jmpbuf);

  struct png_acTL acTL = {htobe32(encoder->len), htobe32(0)};
  png_write_chunk(
    png_ptr, (const void *) "acTL", (const void *) &acTL, sizeof(acTL));

  uint32_t apng_seq = 0;
  for (size_t i = 0; i < encoder->len; i++) {
    struct PmlxzjPngFrame *png_frame = encoder->frames[i];

    struct png_fcTL fcTL;
    pmlxzj_png_fcTL_init(
      &fcTL, &png_frame->rect, apng_seq,
      encoder->frames[i + 1]->timecode_ms - png_frame->timecode_ms,
      encoder->frames[i + 1]->patches_remain);
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
  return 0;
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
    free(worker);
    return ret;
  }

  return 0;
}


int PmlxzjBuffer_init_file (
    struct PmlxzjBuffer *buf, FILE *file, size_t size, off_t offset) {
  if (offset != -1) {
    return_if_fail (fseeko(file, offset, SEEK_SET) == 0) ERR_SYS(fseeko);
  }

  unsigned char *data = malloc(size);
  return_if_fail (data != NULL) ERR_SYS(malloc);

  if (size > 0) {
    if_fail (fread(data, size, 1, file) == 1) {
      int ret = ERR_SYS(fread);
      free(data);
      return ret;
    }
  }

  buf->data = data;
  buf->size = size;
  return 0;
}


static bool PmlxzjClick_rect (
    const struct PmlxzjClick *event, uint32_t width, uint32_t height,
    struct PmlxzjRect *rect_out) {
  return_if_fail (event->type != 0) false;

  int radius = event->type == 3 ? 22 : 16;
  struct PmlxzjRect rect = {
    {clamp(event->p.x - radius, 0, (int32_t) width),
     clamp(event->p.y - radius, 0, (int32_t) height)},
    {clamp(event->p.x + radius, 0, (int32_t) width),
     clamp(event->p.y + radius, 0, (int32_t) height)}
  };
  return_if_fail (PmlxzjRect_width(&rect) > 0 && PmlxzjRect_height(&rect) > 0)
    false;

  *rect_out = rect;
  return true;
}


static int PmlxzjClick_apply (
    const struct PmlxzjClick *event, unsigned char *canvas,
    uint32_t width, uint32_t height, struct PmlxzjRect *rect_out) {
  struct PmlxzjRect rect;
  return_if_fail (PmlxzjClick_rect(event, width, height, &rect)) 1;

  for (long y = rect.p1.y; y < rect.p2.y; y++) {
    for (long x = rect.p1.x; x < rect.p2.x; x++) {
      unsigned long dis2 = (x - event->p.x) * (x - event->p.x) +
                           (y - event->p.y) * (y - event->p.y);
      if ((event->type == 3 && 20 * 20 <= dis2 && dis2 < 22 * 22) ||
          (14 * 14 <= dis2 && dis2 < 16 * 16)) {
        unsigned char *pixel = canvas + 3 * (width * y + x);
        pixel[0] = 0xff;
        pixel[1] = event->type != 2 ? 0 : 0xff;
        pixel[2] = 0;
      }
    }
  }

  if (rect_out != NULL) {
    *rect_out = rect;
  }
  return 0;
}


static bool PmlxzjCursor_rect (
    const struct PmlxzjCursor *cursor, uint32_t width, uint32_t height,
    struct PmlxzjRect *rect_out) {
  return_if_fail (cursor->tbuf != NULL) false;
  const struct PmlxzjBuffer *cursor_ico = &cursor->tbuf->buf;

  const ICONHEADER *header = cursor_ico->data;
  const ICONDIRENTRY *entry = (const void *) (header + 1);

  uint16_t cursor_width = le16toh(entry->bWidth);
  uint16_t cursor_height = le16toh(entry->bHeight);

  struct PmlxzjRect rect = {
    {clamp(cursor->p.x, 0, (int32_t) width),
     clamp(cursor->p.y, 0, (int32_t) height)},
    {clamp(cursor->p.x + cursor_width, 0, (int32_t) width),
     clamp(cursor->p.y + cursor_height, 0, (int32_t) height)}
  };
  return_if_fail (PmlxzjRect_width(&rect) > 0 && PmlxzjRect_height(&rect) > 0)
    false;

  *rect_out = rect;
  return true;
}


static int PmlxzjCursor_apply (
    const struct PmlxzjCursor *cursor, unsigned char *canvas,
    uint32_t width, uint32_t height, struct PmlxzjRect *rect_out) {
  return_if_fail (cursor->tbuf != NULL) 1;
  const struct PmlxzjBuffer *cursor_ico = &cursor->tbuf->buf;

  return_if_fail (pmlxzj_cursor_valid(cursor_ico->data, cursor_ico->size))
    ERR(PL_EINVAL);

  struct PmlxzjRect rect;
  return_if_fail (PmlxzjCursor_rect(cursor, width, height, &rect)) 1;

  const ICONHEADER *header = cursor_ico->data;
  const ICONDIRENTRY *entry = (const void *) (header + 1);
  const BITMAPINFOHEADER *info = (const void *) (
    (const unsigned char *) cursor_ico->data + le32toh(entry->dwDIBOffset));

  uint16_t depth = le16toh(info->biBitCount);
  unsigned int shift = stdc_trailing_zeros(depth);
  const unsigned char *colors = (const void *) (info + 1);
  const unsigned char *pixels = (const void *) (colors + (4 << depth));
  const unsigned char *alphas = (const void *) (
    pixels + le32toh(info->biSizeImage));

  uint16_t cursor_width = le16toh(entry->bWidth);
  uint16_t cursor_height = le16toh(entry->bHeight);
  uint16_t cursor_width_h =
    (((cursor_width << shift) + 31) & ~31) >> shift;

  bool gray = true;
  for (size_t i = 0; i < cursor_width_h * cursor_height / 8; i++) {
    if (alphas[i] != 0xff) {
      gray = false;
      break;
    }
  }

  for (uint16_t y = 0; y < cursor_height; y++) {
    if (cursor->p.y + y < 0 || cursor->p.y + (uint32_t) y >= height) {
      continue;
    }

    for (uint16_t x = 0; x < cursor_width; x++) {
      if (cursor->p.x + x < 0 || cursor->p.x + (uint32_t) x >= width) {
        continue;
      }

      // bmp is upside down
      size_t cursor_offset = cursor_width_h * (cursor_height - y - 1) + x;
      unsigned char pixel =
        pixels[(cursor_offset << shift) / 8] >>
        (8 - depth - (cursor_offset << shift) % 8) & ((1 << depth) - 1);
      size_t offset = width * (y + cursor->p.y) + x + cursor->p.x;

      if (gray) {
        if (pixel != 0) {
          memset(canvas + 3 * offset, 0, 3);
        }
      } else if (
          (alphas[cursor_offset / 8] & (1 << (7 - (cursor_offset % 8)))) == 0) {
        const unsigned char *color = colors + 4 * pixel;
        unsigned char *pixel = canvas + 3 * offset;
        pixel[0] = color[2];
        pixel[1] = color[1];
        pixel[2] = color[0];
      }
    }
  }

  if (rect_out != NULL) {
    *rect_out = rect;
  }
  return 0;
}


int PmlxzjCursor_print (
    const struct PmlxzjCursor *cursor, FILE *out, off_t offset) {
  int ret = fputs("  Type: cursor\n", out);
  if (offset != -1) {
    ret += fprintf(out, "    Offset: 0x%08jx\n", (intmax_t) offset);
  }
  ret += fprintf(out, "    Position: %4" PRId32 "x%3" PRId32 "\n",
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
    const struct PmlxzjImage *image, uint32_t width, uint32_t height) {
  return_if_fail (image->buf.data != NULL) false;

  return_if_fail (image->rect.p1.x >= 0 && image->rect.p2.x >= 0) false;
  return_if_fail (PmlxzjRect_valid(&image->rect)) false;
  return_if_fail ((unsigned int) image->rect.p2.x <= width &&
                  (unsigned int) image->rect.p2.y <= height) false;

  const BITMAPFILEHEADER *header = (const void *) image->buf.data;
  const BITMAPINFOHEADER *info = (const void *) (header + 1);

  return_if_fail (image->buf.size >= le32toh(header->bfSize)) false;

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
    const struct PmlxzjImage *image, unsigned char *canvas,
    uint32_t width, uint32_t height) {
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
  unsigned int rshifts[3];
  unsigned char depths[3];

  for (unsigned int c = 0; c < 3; c++) {
    bitmasks[c] = le32toh(h_bitmasks[c]);
    rshifts[c] = stdc_trailing_zeros(bitmasks[c]);
    depths[c] = stdc_trailing_zeros((bitmasks[c] >> rshifts[c]) + 1);
  }

  for (unsigned int c = 0; c < 3; c++) {
    return_if_fail (depths[c] == 5 || depths[c] == 6) ERR(PL_ENOTSUP);
  }

  // get bmp geometry
  uint32_t pixels_width = PmlxzjRect_width(&image->rect);
  uint32_t pixels_height = PmlxzjRect_height(&image->rect);
  uint32_t pixels_width_h = pixels_width + (pixels_width & 1);

  const uint16_t *pixels = (const void *) (
    (const unsigned char *) image->buf.data + le32toh(header->bfOffBits));

  for (uint32_t y = 0; y < pixels_height; y++) {
    for (uint32_t x = 0; x < pixels_width; x++) {
      size_t offset = width * (y + image->rect.p1.y) + x + image->rect.p1.x;
      // bmp is upside down
      size_t pixels_offset = pixels_width_h * (pixels_height - y - 1) + x;

      for (unsigned int c = 0; c < 3; c++) {
        canvas[3 * offset + c] = to_depth8(
          depths[c],
          (le16toh(pixels[pixels_offset]) & bitmasks[c]) >> rshifts[c]);
      }
    }
  }

  return 0;
}


int PmlxzjImage_print (
    const struct PmlxzjImage *image, FILE *out, off_t offset) {
  int ret = fputs("  Type: image\n", out);
  if (offset != -1) {
    ret += fprintf(out, "    Offset: 0x%08jx\n", (intmax_t) offset);
  }
  ret += fprintf(
    out,
    "    Position: "
    "%4" PRId32 "x%3" PRId32 "+%4" PRId32 "x%3" PRId32
    " (%" PRIu32 "x%" PRIu32 ")\n",
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
      for (uint16_t i = 0; i < le16toh(in[in_i + 2]); i++) {
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

  size_t origlen = le32toh(*(uint32_t *) src);
  if_fail (dstlen == origlen) {
    sc_warning(
      "Unexpected zlib stream length, expected %zu, got %lu", origlen, dstlen);
  }

  *dstlenp = dstlen;
  return 0;
}


// lxefileplay::jieyasuobmp()
static int PmlxzjImage_uncompress_type (
    void *dst, size_t *dstlenp, void *src, size_t srclen, const void *key,
    unsigned int video_type) {
  return_if_fail (video_type >= (PMLXZJ_VIDEO_ZLIB | PMLXZJ_VIDEO_ENC))
    ERR(PL_ENOTSUP);

  if ((video_type & (PMLXZJ_VIDEO_ZLIB | PMLXZJ_VIDEO_ENC)) != 0) {
    pmlxzj_image_encdec(src, srclen, key);
  }

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


// lxefileplay::jkjieyasuobmp()
static int PmlxzjImage_uncompress_jk (
    void *dst, size_t *dstlenp, void *src, size_t srclen, const void *key) {
  return_if_fail (*(uint64_t *) src == (uint64_t) -1) ERR(PL_EFORMAT);

  unsigned int video_type = le32toh(((uint32_t *) src)[2]);
  size_t buflen = le32toh(((uint32_t *) src)[3]);
  size_t srclen_ = le32toh(((uint32_t *) src)[4]);
  void *src_ = (unsigned char *) src + 20;

  return_if_fail (srclen_ + 20 <= srclen) ERR(PL_EFORMAT);

  if (video_type < PMLXZJ_VIDEO_JK_MUL) {
    if_fail (*dstlenp >= buflen) {
      sc_warning("Jk buffer length too small, expected at least %zu, got %zu",
                 buflen, *dstlenp);
    }
    return PmlxzjImage_uncompress_type(
      dst, dstlenp, src_, srclen_, key, video_type);
  }

  return_if_fail (*dstlenp >= 54 && buflen > 0) ERR(PL_EINVAL);

  void *buf = malloc(buflen);
  return_if_fail (buf != NULL) ERR_SYS(malloc);

  int ret = PmlxzjImage_uncompress_type(
    buf, &buflen, src_, srclen_, key, video_type / PMLXZJ_VIDEO_JK_MUL);
  goto_if_fail (ret == 0 && buflen > 8) fail;

  // lxefileplay::buildbmpfilehead()
  uint16_t width = le16toh(((uint16_t *) buf)[0]);
  uint16_t height = le16toh(((uint16_t *) buf)[1]);
  goto_if_fail (2ul * width * height + 8 <= buflen) fail;

  uint16_t width_h = width + (width & 1);
  uint32_t bmpsize = 2 * width_h * height;

  BITMAPFILEHEADER *header = dst;
  *header = (BITMAPFILEHEADER) {
    {'B', 'M'}, htole32(bmpsize + 54), 0, 0, htole32(54)
  };

  BITMAPINFOHEADER *info = (void *) (header + 1);
  *info = (BITMAPINFOHEADER) {
    htole32(40), htole32(width), htole32(height), htole16(1), htole16(16), 0,
    htole32(bmpsize), 0, 0, 0, 0
  };

  static const uint16_t color_8_table[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0x1f, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x7e0, 0, 0, 0, 0, 0, 0, 0, 0x7ff, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0xf800, 0, 0, 0, 0, 0, 0, 0, 0xf81f, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0xffe0, 0, 0, 0, 0, 0, 0, 0, 0xffff, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };
  static const uint16_t color_64_table[256] = {
    0, 0, 0, 0, 0xa, 0, 0, 0, 0x15, 0, 0, 0, 0x1f, 0, 0, 0,
    0x2a0, 0, 0, 0, 0x2aa, 0, 0, 0, 0x2b5, 0, 0, 0, 0x2bf, 0, 0, 0,
    0x540, 0, 0, 0, 0x54a, 0, 0, 0, 0x555, 0, 0, 0, 0x55f, 0, 0, 0,
    0x7e0, 0, 0, 0, 0x7ea, 0, 0, 0, 0x7f5, 0, 0, 0, 0x7ff, 0, 0, 0,
    0x5000, 0, 0, 0, 0x500a, 0, 0, 0, 0x5015, 0, 0, 0, 0x501f, 0, 0, 0,
    0x52a0, 0, 0, 0, 0x52aa, 0, 0, 0, 0x52b5, 0, 0, 0, 0x52bf, 0, 0, 0,
    0x5540, 0, 0, 0, 0x554a, 0, 0, 0, 0x5555, 0, 0, 0, 0x555f, 0, 0, 0,
    0x57e0, 0, 0, 0, 0x57ea, 0, 0, 0, 0x57f5, 0, 0, 0, 0x57ff, 0, 0, 0,
    0xa800, 0, 0, 0, 0xa80a, 0, 0, 0, 0xa815, 0, 0, 0, 0xa81f, 0, 0, 0,
    0xaaa0, 0, 0, 0, 0xaaaa, 0, 0, 0, 0xaab5, 0, 0, 0, 0xaabf, 0, 0, 0,
    0xad40, 0, 0, 0, 0xad4a, 0, 0, 0, 0xad55, 0, 0, 0, 0xad5f, 0, 0, 0,
    0xafe0, 0, 0, 0, 0xafea, 0, 0, 0, 0xaff5, 0, 0, 0, 0xafff, 0, 0, 0,
    0xf800, 0, 0, 0, 0xf80a, 0, 0, 0, 0xf815, 0, 0, 0, 0xf81f, 0, 0, 0,
    0xfaa0, 0, 0, 0, 0xfaaa, 0, 0, 0, 0xfab5, 0, 0, 0, 0xfabf, 0, 0, 0,
    0xfd40, 0, 0, 0, 0xfd4a, 0, 0, 0, 0xfd55, 0, 0, 0, 0xfd5f, 0, 0, 0,
    0xffe0, 0, 0, 0, 0xffea, 0, 0, 0, 0xfff5, 0, 0, 0, 0xffff, 0, 0, 0
  };

  uint16_t colors_cnt = le16toh(((uint16_t *) buf)[3]);
  const uint16_t *table = colors_cnt != 64 ? color_8_table : color_64_table;

  uint16_t *pixels = (void *) (info + 1);
  const unsigned char *maps = (const unsigned char *) buf + 8;
  for (uint16_t y = 0; y < height; y++) {
    for (uint16_t x = 0; x < width; x++) {
      size_t pixels_offset = width_h * y + x;
      size_t offset = width * y + x;
      pixels[pixels_offset] = table[maps[offset]];
    }
  }

fail:
  free(buf);
  return 0;
}


static int PmlxzjImage_uncompress (
    void *dst, size_t *dstlenp, void *src, size_t srclen, const void *key,
    unsigned int video_type) {
  if (*(uint64_t *) src == (uint64_t) -1) {
    return PmlxzjImage_uncompress_jk(dst, dstlenp, src, srclen, key);
  }
  return PmlxzjImage_uncompress_type(
    dst, dstlenp, src, srclen, key, video_type);
}


int PmlxzjImage_read (
    struct PmlxzjImage *image, FILE *file, unsigned int video_type,
    const void *key) {
  return_if_fail (image->seg.size > 0) 0;
  return_if_fail (PmlxzjRect_valid(&image->rect)) ERR(PL_EFORMAT);

  size_t size = bmp_max_size(
    PmlxzjRect_width(&image->rect), PmlxzjRect_height(&image->rect));
  unsigned char *data = malloc(size);
  return_if_fail (data != NULL) ERR_SYS(malloc);

  int ret;

  {
    struct PmlxzjBuffer raw;
    ret = PmlxzjBuffer_init_file_seg(&raw, file, &image->seg);
    goto_if_fail (ret == 0) fail;
    promise(raw.size > 0);

    ret = PmlxzjImage_uncompress(
      data, &size, raw.data, raw.size, key, video_type);

    PmlxzjBuffer_destroy(&raw);
  }
  goto_if_fail (ret == 0) fail;

  if_fail (data[0] == 'B' && data[1] == 'M') {
    ret = ERR(PL_EFORMAT);
    goto fail;
  }
  if_fail (size == le32toh(((BITMAPFILEHEADER *) data)->bfSize)) {
    ret = ERR(PL_EFORMAT);
    goto fail;
  }

  unsigned char *new_data = realloc(data, size);
  if (new_data != NULL) {
    data = new_data;
  }

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
    const struct PmlxzjFrame *frame, unsigned char *canvas,
    uint32_t width, uint32_t height) {
  int ret = 0;

  for (size_t i = 0; i < frame->patches_cnt; i++) {
    ret = PmlxzjImage_apply(frame->patches[i], canvas, width, height);
    break_if_fail (ret == 0);
  }

  return ret;
}


static int PmlxzjEncoder_write_apng (
    const struct PmlxzjEncoder *encoder, FILE *out, const struct Pmlxzj *pl) {
  png_structp png_ptr = png_create_write_struct(
    PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  return_if_fail (png_ptr != NULL) ERR_PNG(png_create_write_struct);
  png_init_io(png_ptr, out);

  int ret;

  ret = pmlxzj_png_write_info(png_ptr, encoder->width, encoder->height, pl);
  goto_if_fail (ret == 0) fail;

  ret = pmlxzj_png_write_frames(png_ptr, encoder);

fail:
  png_destroy_write_struct(&png_ptr, NULL);
  return ret;
}


static int PmlxzjEncoder_stop (
    struct PmlxzjEncoder *encoder, uint32_t timecode_ms) {
  struct PmlxzjPngFrame *frame = ptrarray_new(
    &encoder->frames, &encoder->len, sizeof(struct PmlxzjPngFrame));
  return_if_fail (frame != NULL) -sc_exc.code;
  frame->timecode_ms = timecode_ms;
  frame->patches_remain = 0;
  frame->fdAT = NULL;
  frame->size = 0;

  encoder->len--;
  encoder->end = true;

  uint32_t last_timecode_ms = timecode_ms;
  uint32_t patches_remain = 0;
  for (size_t i = encoder->len; i > 0; ) {
    i--;
    struct PmlxzjPngFrame *frame = encoder->frames[i];
    if (frame->timecode_ms == last_timecode_ms) {
      patches_remain++;
    } else {
      last_timecode_ms = frame->timecode_ms;
      patches_remain = 0;
    }
    frame->patches_remain = patches_remain;
  }

  ThreadPool_stop(&encoder->pool);
  if_fail (encoder->pool.err_i < 0) {
    struct ThreadPoolWorker *worker =
      encoder->pool.workers + encoder->pool.err_i;
    sc_exc = worker->exc;
    return worker->ret;
  }

  return 0;
}


static int PmlxzjEncoder_append (
    struct PmlxzjEncoder *encoder, uint32_t timecode_ms,
    const unsigned char *canvas, struct PmlxzjRect *rect_out) {
  uint32_t width = encoder->width;
  uint32_t height = encoder->height;

  struct PmlxzjRect rect;
  return_if_fail (pmlxzj_canvas_diff(
    canvas, encoder->canvas, width, height, &rect)) 1;

  sc_debug(
    "Drawing at %" PRIu32 " from (%" PRId32 ", %" PRId32
    ") to (%" PRId32 ", %" PRId32 ")\n",
    timecode_ms, rect.p1.x, rect.p1.y, rect.p2.x, rect.p2.y);

  size_t size;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
  unsigned char *scanline = pmlxzj_png_gendiff(
    canvas, encoder->canvas, &rect, width, height, &size);
  return_if_fail (scanline != NULL) -sc_exc.code;

  int ret;

  struct PmlxzjPngFrame *frame = ptrarray_new(
    &encoder->frames, &encoder->len, sizeof(struct PmlxzjPngFrame));
  if_fail (frame != NULL) {
    ret = -sc_exc.code;
    goto fail_frame;
  }

  ret = pmlxzj_compress_bg(
    scanline, size, 4, &frame->fdAT, &frame->size, &encoder->pool);
  goto_if_fail (ret == 0) fail_compress;
  frame->rect = rect;
  frame->timecode_ms = timecode_ms;

  pixel_canvas_copy(
    encoder->canvas, canvas, &rect, width, height);

  if (rect_out != NULL) {
    *rect_out = rect;
  }
  return 0;
#pragma GCC diagnostic pop

fail_compress:
  free(frame);
  encoder->len--;
fail_frame:
  free(scanline);
  return ret;
}


static int PmlxzjEncoder_append_cursor (
    struct PmlxzjEncoder *encoder, uint32_t timecode_ms, unsigned char *canvas,
    const struct PmlxzjCursor *cursor, unsigned char *canvas_buf,
    bool click_again, struct PmlxzjRect *rect_click, bool *draw_clickp) {
  uint32_t width = encoder->width;
  uint32_t height = encoder->height;

  struct PmlxzjRect rect_cursor;

  bool draw_cursor = PmlxzjCursor_rect(cursor, width, height, &rect_cursor);
  bool draw_click = click_again ? *draw_clickp :
    PmlxzjClick_rect(&cursor->event, width, height, rect_click);

  if (draw_cursor) {
    pixel_canvas_copy(canvas_buf, canvas, &rect_cursor, width, height);
  }
  if (!click_again && draw_click) {
    pixel_canvas_copy(canvas_buf, canvas, rect_click, width, height);
  }

  int ret;

  if (draw_cursor) {
    ret = PmlxzjCursor_apply(cursor, canvas, width, height, NULL);
    goto_if_fail (ret >= 0) fail;
  }
  if (draw_click) {
    PmlxzjClick_apply(&cursor->event, canvas, width, height, NULL);
  }

  ret = PmlxzjEncoder_append(encoder, timecode_ms, canvas, NULL);
  goto_if_fail (ret >= 0) fail;

  if (draw_cursor) {
    pixel_canvas_copy(canvas, canvas_buf, &rect_cursor, width, height);
  }
  if (draw_click) {
    pixel_canvas_copy(canvas, canvas_buf, rect_click, width, height);
  }

  if (!click_again) {
    *draw_clickp = draw_click;
  }

fail:
  return ret;
}


static void PmlxzjEncoder_destroy (struct PmlxzjEncoder *encoder) {
  free(encoder->canvas);
  for (size_t i = 0; i < encoder->len + encoder->end; i++) {
    free(encoder->frames[i]->fdAT);
    free(encoder->frames[i]);
  }
  free(encoder->frames);
  ThreadPool_destroy(&encoder->pool);
}


static int PmlxzjEncoder_init (
    struct PmlxzjEncoder *encoder, uint32_t width, uint32_t height,
    unsigned int nproc) {
  size_t canvas_len = 3 * width * height;
  encoder->canvas = malloc(canvas_len);
  if_fail (encoder->canvas != NULL) {
    return ERR_SYS(malloc);
  }

  int ret = ThreadPool_init(&encoder->pool, nproc, "png");
  if_fail (ret == 0) {
    free(encoder->canvas);
    return ret;
  }

  memset(encoder->canvas, PMLXZJ_PNG_TRANSPARENT, canvas_len);
  encoder->frames = NULL;
  encoder->len = 0;
  encoder->width = width;
  encoder->height = height;
  encoder->end = false;
  return 0;
}


#define DIM (2 * KERNEL_SIZE)

#if 0

#include <math.h>

#define KERNEL_SIZE 2


struct IplKernel {
  float *coeffs;
};


static int32_t IplKernel_pred (
    const struct IplKernel *kern, unsigned int j, const int32_t *val) {
  const float *coeff = kern->coeffs + DIM * (j - 1);

  float ret = 0;
  for (unsigned int i = 0; i < DIM; i++) {
    ret += coeff[i] * val[i];
  }
  return ret + .5;
}


static void IplKernel_destroy (struct IplKernel *kern) {
  free(kern->coeffs);
}


static int IplKernel_init (struct IplKernel *kern, unsigned int k) {
  if_fail (k > 1) {
    kern->coeffs = NULL;
    return 0;
  }

  kern->coeffs = malloc(sizeof(kern->coeffs[0]) * DIM * (k - 1));
  return_if_fail (kern->coeffs != NULL) ERR_SYS(malloc);

  unsigned int j;

  for (j = 1; j <= (k + 1) / 2; j++) {
    float *coeff = kern->coeffs + DIM * (j - 1);
    float jk = - DIM / 2 + 1 - j / (float) k;

    for (unsigned int r = 0; r < DIM; r++) {
      float pix = M_PI * ((int) r + jk);
      coeff[r] = sin(pix) * sin(pix / (DIM / 2)) / pix / pix;
    }

    for (unsigned int r = 0; r < DIM; r++) {
      coeff[r] *= (DIM / 2);
    }
  }

  for (; j < k; j++) {
    float *coeff = kern->coeffs + DIM * (j - 1);
    float *coeff_ref = kern->coeffs + DIM * (k - j - 1);
    for (unsigned int r = 0; r < DIM; r++) {
      coeff[r] = coeff_ref[DIM - 1 - r];
    }
  }

  if (sc_log_begin(SC_LOG_DEBUG)) {
    sc_log_print("Resampling kernel (row %u, dim %u):\n", k - 1, DIM);
    for (unsigned int j = 1; j < k; j++) {
      float *coeff = kern->coeffs + DIM * (j - 1);
      for (unsigned int r = 0; r < DIM; r++) {
        sc_log_print(r == 0 ? "%f" : " %f", (float) coeff[r]);
      }
      sc_log_print("\n");
    }
    sc_log_end(SC_LOG_DEBUG);
  }

  return 0;
}

#elif 1

#define KERNEL_SIZE 2


__attribute_artificial__
static inline int32_t dotIII (const int32_t *v1, const int32_t *v2) {
  int32_t ret = 0;
  for (unsigned int i = 0; i < DIM; i++) {
    ret += v1[i] * v2[i];
  }
  return ret;
}


struct IplKernel {
  float magnitude;
  int32_t *coeffs;
};


static int32_t IplKernel_pred (
    const struct IplKernel *kern, unsigned int j, const int32_t *val) {
  return kern->magnitude * dotIII(kern->coeffs + DIM * (j - 1), val) + .5;
}


static void IplKernel_destroy (struct IplKernel *kern) {
  free(kern->coeffs);
}


static int IplKernel_init (struct IplKernel *kern, unsigned int k) {
  if_fail (k > 1) {
    kern->magnitude = 0;
    kern->coeffs = NULL;
    return 0;
  }

  kern->coeffs = malloc(sizeof(kern->coeffs[0]) * DIM * (k - 1));
  return_if_fail (kern->coeffs != NULL) ERR_SYS(malloc);

  int32_t ks[DIM];
  int32_t kk = 1;
  for (unsigned int r = DIM; r > 0; ) {
    r--;
    ks[r] = kk;
    if (r == 0) {
      break;
    }
    kk *= k;
  }
  kern->magnitude = .5 / kk;

  for (unsigned int j = 1; j < k; j++) {
    int32_t *coeff = kern->coeffs + DIM * (j - 1);

    int32_t js[DIM];
    int32_t jj = 1;
    for (unsigned int r = 0; r < DIM; r++) {
      js[r] = jj;
      if (r >= DIM) {
        break;
      }
      jj *= j;
    }

    for (unsigned int r = 0; r < DIM; r++) {
      js[r] *= ks[r];
    }

    static const int32_t m[][DIM] = {
      { 0, -1,  2, -1},
      { 2,  0, -5,  3},
      { 0,  1,  4, -3},
      { 0,  0, -1,  1},
    };

    for (unsigned int r = 0; r < DIM; r++) {
      coeff[r] = dotIII(m[r], js);
    }
  }

  if (sc_log_begin(SC_LOG_DEBUG)) {
    sc_log_print("Resampling kernel (row %u, dim %u):\n", k - 1, DIM);
    for (unsigned int j = 1; j < k; j++) {
      int32_t *coeff = kern->coeffs + DIM * (j - 1);
      for (unsigned int r = 0; r < DIM; r++) {
        sc_log_print(r == 0 ? "%" PRId32 : " %" PRId32, coeff[r]);
      }
      sc_log_print("\n");
    }
    sc_log_end(SC_LOG_DEBUG);
  }

  return 0;
}

#elif 1

#define KERNEL_SIZE 1


struct IplKernel {
  unsigned int k;
};


static int32_t IplKernel_pred (
    const struct IplKernel *kern, unsigned int j, const int32_t *val) {
  int d =
    ((val[DIM / 2] - val[DIM / 2 - 1]) * (int) j + (int) kern->k / 2) /
    (int) kern->k;
  return val[DIM / 2 - 1] + d;
}


static void IplKernel_destroy (struct IplKernel *kern) {
  (void) kern;
}


static int IplKernel_init (struct IplKernel *kern, unsigned int k) {
  kern->k = k;
  return 0;
}

#endif

int PmlxzjVideo_write_apng (
    const struct PmlxzjVideo *video, const struct Pmlxzj *pl, FILE *out,
    unsigned int flags, unsigned int frames_k, unsigned int nproc) {
  return_if_fail (
    video->frames != NULL && video->frames[0] != NULL &&
    video->frames[0]->patches != NULL &&
    video->frames[0]->patches[0] != NULL) ERR(PL_EINVAL);
  return_if_fail (pl->encrypted >= 0) ERR(PL_EKEY);

  bool use_subframes = (flags & 1) != 0;
  bool with_cursor = (flags & 2) != 0;
  if (video->cursors_cnt <= 0) {
    with_cursor = false;
  }

  const struct PmlxzjImage *first = video->frames[0]->patches[0];
  uint32_t width = first->rect.p2.x;
  uint32_t height = first->rect.p2.y;

  // acquire resources
  struct PmlxzjEncoder encoder;
  return_with_nonzero (PmlxzjEncoder_init(&encoder, width, height, nproc));

  int ret;

  size_t canvas_len = 3 * width * height;
  canvas_len = (canvas_len + 4095) & ~4095;
  unsigned char *canvas = malloc(2 * canvas_len);
  if_fail (canvas != NULL) {
    ret = ERR_SYS(malloc);
    goto fail_canvas;
  }
  unsigned char *canvas_saved = canvas + canvas_len;

  uint32_t *timecodes_ipl = NULL;  // [frames_k]
  if (with_cursor && frames_k > 1) {
    timecodes_ipl = malloc(sizeof(timecodes_ipl[0]) * frames_k);
    if_fail (timecodes_ipl != NULL) {
      ret = ERR_SYS(malloc);
      goto fail_timecodes_ipl;
    }

    timecodes_ipl[0] = 0;
    for (unsigned int j = 1; j < frames_k; j++) {
      timecodes_ipl[j] = video->frame_ms * j / frames_k;
    }
  }

  struct IplKernel kern;
  ret = IplKernel_init(&kern, !with_cursor ? 0 : frames_k);
  goto_if_fail (ret >= 0) fail_kern;

  // process frames
  if (with_cursor) {
    sc_info("Interpolation multiplier: %u\n", frames_k);
  }
  sc_info("Original frames: %zu\n", video->frames_cnt);

  bool last_draw_cursor = false;
  for (size_t i = 0; i < video->frames_cnt; i++) {
    if ((i + 1) % 32 == 0 || i + 1 >= video->frames_cnt) {
      sc_notice(
        sc_log_level < SC_LOG_DEBUG ? "[%3.f%%] %zu / %zu f, %zu APNG f\r" :
        "[%3.f%%] %zu / %zu f, %zu APNG f\n",
        (i + 1) * 100. / video->frames_cnt, i + 1, video->frames_cnt,
        encoder.len);
    }

    struct PmlxzjFrame *frame = video->frames[i];
    uint32_t timecode_base = video->frame_ms * i;

    // revert cursor subframe
    if (use_subframes && last_draw_cursor) {
      ret = PmlxzjEncoder_append(&encoder, timecode_base, canvas, NULL);
      goto_if_fail (ret >= 0) fail_frame;
      last_draw_cursor = false;
    }

    // apply / draw subframes
    for (size_t j = 0; j < frame->patches_cnt; j++) {
      struct PmlxzjImage *patch = frame->patches[j];
      bool read = patch->buf.data == NULL;

      if (read) {
        ret = PmlxzjImage_read(
          patch, pl->file, le32toh(pl->player.video_type),
          pl->encrypted <= 0 ? NULL : pl->key);
        goto_if_fail (ret == 0) fail_frame;
      }

      ret = PmlxzjImage_apply(patch, canvas, width, height);

      if (read) {
        free(patch->buf.data);
        patch->buf.data = NULL;
      }
      goto_if_fail (ret == 0) fail_frame;

      if (use_subframes) {
        ret = PmlxzjEncoder_append(&encoder, timecode_base, canvas, NULL);
        goto_if_fail (ret >= 0) fail_frame;
      }
    }

    // draw main frame (with cursor)
    const struct PmlxzjCursor *cursor = &frame->cursor;
    bool cursor_valid =
      with_cursor && PmlxzjCursor_valid(cursor) && cursor->tbuf != NULL;

    struct PmlxzjRect rect_click;
    bool draw_click;

    if (cursor_valid) {
      sc_debug("Cursor %zu: %d %d\n", i, cursor->p.x, cursor->p.y);
      ret = PmlxzjEncoder_append_cursor(
        &encoder, timecode_base, canvas, cursor, canvas_saved, false,
        &rect_click, &draw_click);
      goto_if_fail (ret >= 0) fail_frame;
      last_draw_cursor = true;
    } else if (!use_subframes) {
      ret = PmlxzjEncoder_append(&encoder, timecode_base, canvas, NULL);
      goto_if_fail (ret >= 0) fail_frame;
    }

    // draw cursor interpolation
    do {
      if (!cursor_valid || frames_k <= 1 || i + 1 >= video->frames_cnt) {
        break;
      }

      const struct PmlxzjCursor *cursor_next = &video->frames[i + 1]->cursor;
      if (!PmlxzjCursor_valid(cursor_next)) {
        break;
      }

      bool diff_x = cursor_next->p.x != cursor->p.x;
      bool diff_y = cursor_next->p.y != cursor->p.y;
      if (!diff_x && !diff_y) {
        break;
      }

      int32_t xs[DIM];
      int32_t ys[DIM];

      xs[DIM / 2 - 1] = cursor->p.x;
      ys[DIM / 2 - 1] = cursor->p.y;
      for (unsigned int a = 1; a < DIM / 2; a++) {
        const struct PmlxzjCursor *cursor_bw = &video->frames[i - a]->cursor;
        if (i < a || !PmlxzjCursor_valid(cursor_bw)) {
          int32_t x = xs[DIM / 2 - a];
          int32_t y = ys[DIM / 2 - a];
          for (unsigned int b = a; b < DIM / 2; b++) {
            xs[DIM / 2 - (b + 1)] = x;
          }
          for (unsigned int b = a; b < DIM / 2; b++) {
            ys[DIM / 2 - (b + 1)] = y;
          }
          break;
        }
        xs[DIM / 2 - (a + 1)] = cursor_bw->p.x;
        ys[DIM / 2 - (a + 1)] = cursor_bw->p.y;
      }

      xs[DIM / 2] = cursor_next->p.x;
      ys[DIM / 2] = cursor_next->p.y;
      for (unsigned int a = 1; a < DIM / 2; a++) {
        const struct PmlxzjCursor *cursor_fw =
          &video->frames[i + a + 1]->cursor;
        if (i + a + 1 >= video->frames_cnt || !PmlxzjCursor_valid(cursor_fw)) {
          int32_t x = xs[DIM / 2 + a - 1];
          int32_t y = ys[DIM / 2 + a - 1];
          for (unsigned int b = a; b < DIM / 2; b++) {
            xs[DIM / 2 + b] = x;
          }
          for (unsigned int b = a; b < DIM / 2; b++) {
            ys[DIM / 2 + b] = y;
          }
          break;
        }
        xs[DIM / 2 + a] = cursor_fw->p.x;
        ys[DIM / 2 + a] = cursor_fw->p.y;
      }

      if (sc_log_begin(SC_LOG_DEBUG)) {
        sc_log_print("X window:");
        for (unsigned int r = 0; r < DIM; r++) {
          sc_log_print(" %" PRId32, xs[r]);
        }
        sc_log_print(", Y window:");
        for (unsigned int r = 0; r < DIM; r++) {
          sc_log_print(" %" PRId32, ys[r]);
        }
        sc_log_print("\n");
        sc_log_end(SC_LOG_DEBUG);
      }

      struct PmlxzjCursor cursor_mid = frame->cursor;
      for (unsigned int j = 1; j < frames_k; j++) {
        if (diff_x) {
          cursor_mid.p.x = IplKernel_pred(&kern, j, xs);
        }
        if (diff_y) {
          cursor_mid.p.y = IplKernel_pred(&kern, j, ys);
        }
        sc_debug(
          "Cursor %zu + %u: %d %d\n", i, j, cursor_mid.p.x, cursor_mid.p.y);

        ret = PmlxzjEncoder_append_cursor(
          &encoder, timecode_base + timecodes_ipl[j], canvas, &cursor_mid,
          canvas_saved, true, &rect_click, &draw_click);
        goto_if_fail (ret >= 0) fail_frame;
      }
    } while (0);
  }
  sc_notice("\n");

  ret = PmlxzjEncoder_stop(&encoder, video->frame_ms * video->frames_cnt);
  goto_if_fail (ret == 0) fail;

  ret = PmlxzjEncoder_write_apng(&encoder, out, pl);
  goto_if_fail (ret == 0) fail;

fail:
  if (0) {
fail_frame:
    sc_notice("\n");
  }
  IplKernel_destroy(&kern);
fail_kern:
  free(timecodes_ipl);
fail_timecodes_ipl:
  free(canvas);
fail_canvas:
  PmlxzjEncoder_destroy(&encoder);
  return ret;
}


int PmlxzjVideo_save_apng (
    const struct PmlxzjVideo *video, const struct Pmlxzj *pl, const char *path,
    unsigned int flags, unsigned int frames_k, unsigned int nproc) {
  return_if_fail (
    video->frames != NULL && video->frames[0] != NULL &&
    video->frames[0]->patches != NULL &&
    video->frames[0]->patches[0] != NULL) ERR(PL_EINVAL);
  return_if_fail (pl->encrypted >= 0) ERR(PL_EKEY);

  FILE *out = fopen(path, "wb");
  return_if_fail (out != NULL) ERR_SYS(fopen);
  int ret = PmlxzjVideo_write_apng(video, pl, out, flags, frames_k, nproc);
  fclose(out);
  return ret;
}


int PmlxzjVideo_save_cursors (
    const struct PmlxzjVideo *video, const char *dir) {
  return_if_fail (video->cursors_cnt > 0) ERR(PL_EINVAL);

  size_t dir_len = strlen(dir);
  char path[dir_len + 65];
  memcpy(path, dir, dir_len);
  char *filename = path + dir_len;
  filename[0] = '/';
  filename++;

  snprintf(filename, 64, "cursors.txt");
  FILE *out_cursors = fopen(path, "w");
  return_if_fail (out_cursors != NULL) ERR_SYS(fopen);

  int ret = 0;

  for (size_t i = 0; i < video->cursors_cnt; i++) {
    struct PmlxzjTaggedBuffer *tbuf = video->cursors[i];
    snprintf(filename, 64, "c_%08lx.ico", tbuf->tag);
    FILE *out_icon = fopen(path, "wb");
    if_fail (out_icon != NULL) {
      ret = ERR_SYS(fopen);
      goto fail;
    }
    if_fail (fwrite(tbuf->buf.data, tbuf->buf.size, 1, out_icon) == 1) {
      ret = ERR_SYS(fwrite);
      fclose(out_icon);
      goto fail;
    }
    fclose(out_icon);
  }

  for (size_t i = 0; i < video->frames_cnt; i++) {
    struct PmlxzjCursor *cursor = &video->frames[i]->cursor;
    if (cursor->seg.offset <= 0) {
      sc_warning("cursor info missing for frame %zu\n", i);
      ret = fputs(".\n", out_cursors);
    } else {
      ret = fprintf(
        out_cursors, "%" PRId32 " %" PRId32 " %08lx\n",
        cursor->p.x, cursor->p.y, cursor->tbuf == NULL ? 0 : cursor->tbuf->tag);
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


static int PmlxzjVideo_read_cursor (
    struct PmlxzjVideo *video, FILE *in, struct PmlxzjCursor *cursor,
    struct PmlxzjTaggedBuffer **tbufp) {
  if_fail (cursor->seg.size > 0) {
    cursor->tbuf = *tbufp;
    return 0;
  }

  int ret;

  struct PmlxzjBuffer buf;
  ret = PmlxzjBuffer_init_file_seg(&buf, in, &cursor->seg);
  return_if_fail (ret == 0) ret;

  unsigned long tag = pmlxzj_crc32(buf.data, buf.size);

  struct PmlxzjTaggedBuffer *tbuf;
  for (size_t j = 0; j < video->cursors_cnt; j++) {
    tbuf = video->cursors[j];
    if (tbuf->tag == tag) {
      cursor->tbuf = tbuf;
      *tbufp = tbuf;

      ret = 0;
      goto fail;
    }
  }

  tbuf = ptrarray_new(
    &video->cursors, &video->cursors_cnt, sizeof(struct PmlxzjTaggedBuffer));
  if_fail (tbuf != NULL) {
    ret = -sc_exc.code;
    goto fail;
  }
  tbuf->tag = tag;
  tbuf->buf = buf;

  cursor->tbuf = tbuf;
  *tbufp = tbuf;
  return 0;

fail:
  PmlxzjBuffer_destroy(&buf);
  return ret;
}


int PmlxzjVideo_read_cursors (
    struct PmlxzjVideo *video, FILE *in, struct PmlxzjTaggedBuffer **tbufp) {
  int ret;

  for (size_t i = 0; i < video->frames_cnt; i++) {
    struct PmlxzjCursor *cursor = &video->frames[i]->cursor;
    ret = PmlxzjVideo_read_cursor(video, in, cursor, tbufp);
    goto_if_fail (ret == 0) fail;
  }

  ret = 0;
fail:
  return ret;
}


int PmlxzjVideo_parse_clicks (
    struct PmlxzjVideo *video, const char *src, size_t size) {
  return_if_fail (size > 0) 0;

  long buf[8];
  unsigned int i = 0;

  char *cur = (void *) src;
  do {
    long l = strtol(cur, &cur, 10);
    return_if_fail (isspace(*cur)) ERR(PL_EINVAL);

    buf[i] = l;
    i++;
    if (i >= arraysize(buf)) {
      i = 0;
      if (buf[0] >= 0 && (unsigned long) buf[0] < video->frames_cnt) {
        struct PmlxzjClick *event = &video->frames[buf[0]]->cursor.event;
        event->p.x = buf[1];
        event->p.y = buf[2];
        event->type = buf[3];

        if (buf[3] == 3 && buf[0] > 0) {
          // additional symbol for double click
          struct PmlxzjClick *event = &video->frames[buf[0] - 1]->cursor.event;
          event->p.x = buf[1];
          event->p.y = buf[2];
          event->type = 1;
        }
      }
    }

    for (; cur < src + size && isspace(*cur); cur++) {}
  } while (cur < src + size);

  return 0;
}


int PmlxzjVideo_read_clicks (
    struct PmlxzjVideo *video, const struct Pmlxzj *pl) {
  return_if_fail (pl->clicks_offset != -1) 0;

  return_if_fail (fseeko(pl->file, pl->clicks_offset, SEEK_SET) == 0)
    ERR_SYS(fseeko);

  char *clicks = malloc(pl->clicks_size);
  return_if_fail (clicks != NULL) ERR_SYS(malloc);

  int ret;

  if_fail (fread(clicks, pl->clicks_size, 1, pl->file) == 1) {
    ret = ERR_SYS(fread);
    goto end;
  }

  ret = PmlxzjVideo_parse_clicks(video, clicks, pl->clicks_size);

end:
  free(clicks);
  return ret;
}


int PmlxzjVideo_init (
    struct PmlxzjVideo *video, const struct Pmlxzj *pl,
    int32_t frames_limit, bool read_cursor) {
  struct PmlxzjLxePacketIter iter;
  PmlxzjLxePacketIter_init(&iter, pl, frames_limit);

  video->frames = NULL;
  video->frames_cnt = 0;
  video->cursors = NULL;
  video->cursors_cnt = 0;

  struct PmlxzjFrame *frame = NULL;
  struct PmlxzjTaggedBuffer *tbuf = NULL;
  int ret;

  while (true) {
    int state = PmlxzjLxePacketIter_next(&iter);
    if_fail (state >= 0) {
      ret = state;
      goto fail;
    }

    if (state == PmlxzjLxePacketIter_NEXT_FRAME) {
      break_if_fail (iter.frame_no < iter.frames_cnt);
      frame = ptrarray_new(
        &video->frames, &video->frames_cnt, sizeof(struct PmlxzjFrame));
      if_fail (frame != NULL) {
        ret = -sc_exc.code;
        goto fail;
      }
      PmlxzjFrame_init(frame);
    } else if (frame != NULL) {
      if (state == PmlxzjLxePacketIter_NEXT_IMAGE) {
        struct PmlxzjImage *patch = ptrarray_new(
          &frame->patches, &frame->patches_cnt, sizeof(struct PmlxzjImage));
        if_fail (patch != NULL) {
          ret = -sc_exc.code;
          goto fail;
        }
        PmlxzjImage_init(patch, &iter.packet.image);
        patch->seg.offset = iter.offset;
      } else {
        struct PmlxzjCursor *cursor = &frame->cursor;
        PmlxzjCursor_init(cursor, &iter.packet.cursor);
        cursor->seg.offset = iter.offset;

        if (read_cursor) {
          ret = PmlxzjVideo_read_cursor(video, pl->file, cursor, &tbuf);
          goto_if_fail (ret == 0) fail;
        }
      }
    }
  }

  video->frame_ms = le32toh(pl->video.frame_ms);
  return 0;

fail:
  PmlxzjVideo_destroy(video);
  return ret;
}


int Pmlxzj_extract_video_or_cursor (
    const struct Pmlxzj *pl, const char *dir, int32_t frames_limit,
    unsigned int flags, unsigned int frames_k, unsigned int nproc,
    bool extract_video, bool extract_cursor) {
  return_if_fail (extract_video || extract_cursor) 0;

  struct PmlxzjVideo video;
  return_with_nonzero (PmlxzjVideo_init(&video, pl, frames_limit, true));

  int ret;

  if (extract_video) {
    bool with_cursor = (flags & 2) != 0;

    if (with_cursor && pl->clicks_offset != -1) {
      ret = PmlxzjVideo_read_clicks(&video, pl);
      goto_if_fail (ret == 0) fail;
    }

    size_t dir_len = strlen(dir);
    char path[dir_len + 65];
    memcpy(path, dir, dir_len);
    char *filename = path + dir_len;
    filename[0] = '/';
    filename++;
    snprintf(filename, 64, with_cursor ? "video.apng" : "video_raw.apng");

    ret = PmlxzjVideo_save_apng(
      &video, pl, path, flags, frames_k, nproc);
    goto_if_fail (ret == 0) fail;
  }
  if (extract_cursor) {
    ret = PmlxzjVideo_save_cursors(&video, dir);
    goto_if_fail (ret == 0) fail;
  }

  ret = 0;
fail:
  PmlxzjVideo_destroy(&video);
  return ret;
}
