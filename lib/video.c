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


#define PLZJ_PNG_TRANSPARENT 222
#define PLZJ_PNG_DELAY_DEN 1000


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


struct PlzjPngFrame {
  struct PlzjRect rect;
  uint32_t timecode_ms;
  uint32_t patches_remain;

  void *fdAT;
  size_t size;
};


struct PlzjEncoder {
  struct PlzjPngFrame **frames;
  size_t len;

  struct PlzjColor *canvas;
  uint32_t width;
  uint32_t height;

  int compression_level;

  struct ThreadPool pool;

  bool end;
};


__attribute_artificial__
static inline size_t bmp_max_size (uint32_t width, uint32_t height) {
  // they use 0x1400, which seems too large
  return 2 * (width + (width & 1)) * height + 256;
}


static void pixel_canvas_copy (
    struct PlzjColor *dst, const struct PlzjColor *src,
    const struct PlzjRect *rect, uint32_t width, uint32_t height) {
  (void) height;

  uint32_t cut_width = PlzjRect_width(rect);
  uint32_t cut_height = PlzjRect_height(rect);
  return_if_fail (cut_width > 0 && cut_height > 0);

  for (int32_t y = rect->p1.y; y < rect->p2.y; y++) {
    size_t offset = width * y + rect->p1.x;
    memcpy(dst + offset, src + offset, sizeof(*dst) * cut_width);
  }
}


static bool plzj_cursor_valid (const unsigned char *cursor, size_t size) {
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


static bool plzj_canvas_diff (
    const struct PlzjColor *dst, const struct PlzjColor *src,
    uint32_t width, uint32_t height, struct PlzjRect *rect) {
  for (uint32_t y = 0; y < height; y++) {
    if (memcmp(dst + width * y, src + width * y, sizeof(*dst) * width) != 0) {
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
    if (memcmp(dst + width * y, src + width * y, sizeof(*dst) * width) != 0) {
      rect->p2.y = y + 1;
      break;
    }
  }

  for (uint32_t x = 0; x < width; x++) {
    for (int32_t y = rect->p1.y; y < rect->p2.y; y++) {
      size_t offset = width * y + x;
      if (memcmp(dst + offset, src + offset, sizeof(*dst)) != 0) {
        rect->p1.x = x;
        goto p2x;
      }
    }
  }

p2x:
  for (uint32_t x = width; x > 0; ) {
    x--;
    for (int32_t y = rect->p1.y; y < rect->p2.y; y++) {
      size_t offset = width * y + x;
      if (memcmp(dst + offset, src + offset, sizeof(*dst)) != 0) {
        rect->p2.x = x + 1;
        goto end;
      }
    }
  }

end:
  return true;
}


static unsigned char *plzj_png_gen_scanline (
    const struct PlzjColor *canvas, uint32_t width, uint32_t height,
    size_t *lenp) {
  size_t scanline_len = (1 + 3 * width) * height;
  unsigned char *scanline = malloc(scanline_len);
  if_fail (scanline != NULL) {
    (void) ERR_SYS(malloc);
    return NULL;
  }

  for (uint32_t y = 0; y < height; y++) {
    scanline[(1 + 3 * width) * y] = 0;
    for (uint32_t x = 0; x < width; x++) {
      memcpy(scanline + (1 + 3 * width) * y + 1 + 3 * x,
             canvas + width * y + x, 3);
    }
  }

  *lenp = scanline_len;
  return scanline;
}


static unsigned char *plzj_png_gendiff (
    const struct PlzjColor *canvas, const struct PlzjColor *canvas_before,
    const struct PlzjRect *rect, uint32_t width, uint32_t height,
    size_t *lenp) {
  if (rect == NULL) {
    return plzj_png_gen_scanline(canvas, width, height, lenp);
  }

  uint32_t scanline_width = PlzjRect_width(rect);
  uint32_t scanline_height = PlzjRect_height(rect);
  size_t scanline_len = (1 + 3 * scanline_width) * scanline_height;

  unsigned char *scanline = malloc(scanline_len);
  if_fail (scanline != NULL) {
    (void) ERR_SYS(malloc);
    return NULL;
  }

  for (uint32_t y = 0; y < scanline_height; y++) {
    scanline[(1 + 3 * scanline_width) * y] = 0;
    for (uint32_t x = 0; x < scanline_width; x++) {
      size_t offset = width * (y + rect->p1.y) + x + rect->p1.x;
      size_t scanline_offset = (1 + 3 * scanline_width) * y + 1 + 3 * x;
      if (memcmp(
          canvas + offset, canvas_before + offset, sizeof(*canvas)) == 0) {
        memset(scanline + scanline_offset, PLZJ_PNG_TRANSPARENT, 3);
      } else {
        memcpy(scanline + scanline_offset, canvas + offset, 3);
      }
    }
  }

  *lenp = scanline_len;
  return scanline;
}


static void plzj_png_fcTL_init (
    struct png_fcTL *fcTL, const struct PlzjRect *rect, uint32_t seq,
    uint16_t delay_ms, uint16_t next_patches_remain) {
  uint16_t delay_num = delay_ms == 0 ? 1 :
    delay_ms * PLZJ_PNG_DELAY_DEN / 1000 - next_patches_remain;
  *fcTL = (struct png_fcTL) {
    htobe32(seq), htobe32(PlzjRect_width(rect)), htobe32(PlzjRect_height(rect)),
    htobe32(rect->p1.x), htobe32(rect->p1.y),
    htobe16(delay_num), htobe16(PLZJ_PNG_DELAY_DEN),
    PNG_DISPOSE_OP_NONE, PNG_BLEND_OP_OVER
  };
}


static int plzj_png_write_info (
    png_structp png_ptr, uint32_t width, uint32_t height,
    const struct Plzj *pl) {
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
    .red = PLZJ_PNG_TRANSPARENT,
    .green = PLZJ_PNG_TRANSPARENT,
    .blue = PLZJ_PNG_TRANSPARENT,
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
  size_t title_len = Plzj_get_title(pl, &exif[38], 2 * 64);
  sc_info("Player title: %s\n", title_len == 0 ? "(null)" : exif + 38);
  if (title_len > 0) {
    title_len++;
  }
  *(uint32_t *) (exif + 14) = htole32(title_len);
  *(uint32_t *) (exif + 30) = htole32(38 + title_len);
  size_t infotext_len =
    Plzj_get_infotext(pl, exif + 38 + title_len, 2 * 64 - title_len);
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


static int plzj_png_write_frames (
    png_structp png_ptr, const struct PlzjEncoder *encoder) {
  int res = setjmp(png_jmpbuf(png_ptr));
  return_if_fail (res == 0) ERR_PNG(png_jmpbuf);

  struct png_acTL acTL = {htobe32(encoder->len), htobe32(0)};
  png_write_chunk(
    png_ptr, (const void *) "acTL", (const void *) &acTL, sizeof(acTL));

  uint32_t apng_seq = 0;
  for (size_t i = 0; i < encoder->len; i++) {
    const struct PlzjPngFrame *png_frame = encoder->frames[i];
    const struct PlzjPngFrame *png_frame_next = encoder->frames[i + 1];

    struct png_fcTL fcTL;
    plzj_png_fcTL_init(
      &fcTL, &png_frame->rect, apng_seq,
      png_frame_next->timecode_ms - png_frame->timecode_ms,
      png_frame_next->patches_remain);
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


static void *plzj_compress (
    const void *src, size_t srclen, size_t dstlen_before, size_t *dstlenp,
    int level) {
  uLongf buflen = compressBound(srclen);
  Bytef *dst = malloc(dstlen_before + buflen);
  if_fail (dst != NULL) {
    (void) ERR_SYS(malloc);
    return NULL;
  }

  int res = compress2(dst + dstlen_before, &buflen, src, srclen, level);
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
  int level;
};


static int plzj_compress_worker (void *arg) {
  struct compress_worker *worker = arg;
  void *dst = plzj_compress(
    worker->src, worker->srclen, worker->dstlen_before, worker->dstlenp,
    worker->level);
  *worker->dstp = dst;
  free(worker->src);
  free(worker);
  return dst == NULL ? -sc_exc.code : 0;
}


static int plzj_compress_bg (
    void *src, size_t srclen, size_t dstlen_before, void **dstp,
    size_t *dstlenp, int level, struct ThreadPool *pool) {
  if_fail (pool->err_i < 0) {
    struct ThreadPoolWorker *worker = pool->workers + pool->err_i;
    sc_exc = worker->exc;
    return worker->ret;
  }

  struct compress_worker *worker = malloc(sizeof(*worker));
  return_if_fail (worker != NULL) ERR_SYS(malloc);

  *worker = (struct compress_worker) {
    src, srclen, dstlen_before, dstp, dstlenp, level
  };
  int ret = ThreadPool_run(pool, plzj_compress_worker, worker);
  if_fail (ret == 0) {
    free(worker);
    return ret;
  }

  return 0;
}


int PlzjBuffer_init_file (
    struct PlzjBuffer *buf, FILE *file, size_t size, off_t offset) {
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


static bool PlzjClick_rect (
    const struct PlzjClick *event, uint32_t width, uint32_t height,
    struct PlzjRect *rect_out) {
  return_if_fail (event->type != 0) false;

  int radius = event->type == 3 ? 22 : 16;
  struct PlzjRect rect = {
    {clamp(event->p.x - radius, 0, (int32_t) width),
     clamp(event->p.y - radius, 0, (int32_t) height)},
    {clamp(event->p.x + radius, 0, (int32_t) width),
     clamp(event->p.y + radius, 0, (int32_t) height)}
  };
  return_if_fail (PlzjRect_width(&rect) > 0 && PlzjRect_height(&rect) > 0)
    false;

  *rect_out = rect;
  return true;
}


static int PlzjClick_apply (
    const struct PlzjClick *event, struct PlzjColor *canvas,
    uint32_t width, uint32_t height, struct PlzjRect *rect_out) {
  struct PlzjRect rect;
  return_if_fail (PlzjClick_rect(event, width, height, &rect)) 1;

  for (long y = rect.p1.y; y < rect.p2.y; y++) {
    for (long x = rect.p1.x; x < rect.p2.x; x++) {
      unsigned long dis2 = (x - event->p.x) * (x - event->p.x) +
                           (y - event->p.y) * (y - event->p.y);
      if ((event->type == 3 && 20 * 20 <= dis2 && dis2 < 22 * 22) ||
          (14 * 14 <= dis2 && dis2 < 16 * 16)) {
        canvas[width * y + x] = (struct PlzjColor) {
          .r = 0xff, .g = event->type != 2 ? 0 : 0xff, .b = 0
        };
      }
    }
  }

  if (rect_out != NULL) {
    *rect_out = rect;
  }
  return 0;
}


static bool PlzjCursor_rect (
    const struct PlzjCursor *cursor, uint32_t width, uint32_t height,
    struct PlzjRect *rect_out) {
  return_if_fail (cursor->tbuf != NULL) false;
  const struct PlzjBuffer *cursor_ico = &cursor->tbuf->buf;

  const ICONHEADER *header = cursor_ico->data;
  const ICONDIRENTRY *entry = (const void *) (header + 1);

  uint16_t cursor_width = le16toh(entry->bWidth);
  uint16_t cursor_height = le16toh(entry->bHeight);

  struct PlzjRect rect = {
    {clamp(cursor->p.x, 0, (int32_t) width),
     clamp(cursor->p.y, 0, (int32_t) height)},
    {clamp(cursor->p.x + cursor_width, 0, (int32_t) width),
     clamp(cursor->p.y + cursor_height, 0, (int32_t) height)}
  };
  return_if_fail (PlzjRect_width(&rect) > 0 && PlzjRect_height(&rect) > 0)
    false;

  *rect_out = rect;
  return true;
}


static int PlzjCursor_apply (
    const struct PlzjCursor *cursor, struct PlzjColor *canvas,
    uint32_t width, uint32_t height, struct PlzjRect *rect_out) {
  return_if_fail (cursor->tbuf != NULL) 1;
  const struct PlzjBuffer *cursor_ico = &cursor->tbuf->buf;

  return_if_fail (plzj_cursor_valid(cursor_ico->data, cursor_ico->size))
    ERR(PL_EINVAL);

  struct PlzjRect rect;
  return_if_fail (PlzjCursor_rect(cursor, width, height, &rect)) 1;

  const ICONHEADER *header = cursor_ico->data;
  const ICONDIRENTRY *entry = (const void *) (header + 1);
  const BITMAPINFOHEADER *info = (const void *) (
    (const unsigned char *) cursor_ico->data + le32toh(entry->dwDIBOffset));

  uint16_t depth = le16toh(info->biBitCount);
  unsigned int shift = stdc_trailing_zeros(depth);

  const uint32_t *colors = (const void *) (info + 1);
  const unsigned char *pixels = (const void *) (colors + (1 << depth));
  const unsigned char *alphas =
    (const void *) (pixels + le32toh(info->biSizeImage));

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
      unsigned char cursor_pixel = BIT_FIELD(
        pixels[(cursor_offset << shift) / 8],
        8 - depth - (cursor_offset << shift) % 8, depth);

      struct PlzjColor *pixel =
        canvas + width * (y + cursor->p.y) + x + cursor->p.x;
      if (gray) {
        if (cursor_pixel != 0) {
          *pixel = (struct PlzjColor) {};  // black
        }
      } else if (BIT_FIELD(
          alphas[cursor_offset / 8], 7 - (cursor_offset % 8), 1) == 0) {
        const uint32_t color = le32toh(colors[cursor_pixel]);
        *pixel = (struct PlzjColor) {
          .r = BIT_FIELD(color, 16, 8),
          .g = BIT_FIELD(color, 8, 8),
          .b = BIT_FIELD(color, 0, 8),
        };
      }
    }
  }

  if (rect_out != NULL) {
    *rect_out = rect;
  }
  return 0;
}


int PlzjCursor_print (
    const struct PlzjCursor *cursor, FILE *out, off_t offset) {
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


int PlzjCursor_init_file (struct PlzjCursor *cursor, FILE *file) {
  struct PlzjLxeCursor h_cursor;
  return_if_fail (fread(&h_cursor, sizeof(h_cursor), 1, file) == 1)
    ERR_SYS(fread);
  return PlzjCursor_init(cursor, &h_cursor);
}


bool PlzjImage_valid (
    const struct PlzjImage *image, uint32_t width, uint32_t height) {
  return_if_fail (image->buf.data != NULL) false;

  return_if_fail (image->rect.p1.x >= 0 && image->rect.p2.x >= 0) false;
  return_if_fail (PlzjRect_valid(&image->rect)) false;
  return_if_fail ((unsigned int) image->rect.p2.x <= width &&
                  (unsigned int) image->rect.p2.y <= height) false;

  const BITMAPFILEHEADER *header = (const void *) image->buf.data;
  const BITMAPINFOHEADER *info = (const void *) (header + 1);

  return_if_fail (image->buf.size >= le32toh(header->bfSize)) false;

  return_if_fail (
    PlzjRect_width(&image->rect) == le32toh(info->biWidth)) false;
  return_if_fail (
    PlzjRect_height(&image->rect) == le32toh(info->biHeight)) false;

  return_if_fail (
    (le16toh(info->biBitCount) * le32toh(info->biWidth) + 31) / 32 * 4 *
    le32toh(info->biHeight) <= le32toh(info->biSizeImage)) false;

  return true;
}


int PlzjImage_apply (
    const struct PlzjImage *image, struct PlzjColor *canvas,
    uint32_t width, uint32_t height) {
  return_if_fail (PlzjImage_valid(image, width, height)) ERR(PL_EINVAL);

  const BITMAPFILEHEADER *header = (const void *) image->buf.data;
  const BITMAPINFOHEADER *info = (const void *) (header + 1);

  return_if_fail (le16toh(info->biBitCount) == 16) ERR(PL_ENOTSUP);

  // convert color depths
  uint32_t bitmasks[3];
  unsigned int rshifts[3];
  unsigned char depths[3];

  uint32_t biCompression = le32toh(info->biCompression);
  if (biCompression == BI_RGB) {
    bitmasks[0] = 0xf800;
    bitmasks[1] = 0x07e0;
    bitmasks[2] = 0x001f;
    rshifts[0] = 11;
    rshifts[1] = 5;
    rshifts[2] = 0;
    depths[0] = 5;
    depths[1] = 6;
    depths[2] = 5;
  } else if (biCompression == BI_BITFIELDS) {
    const uint32_t *h_bitmasks =
      (const void *) ((const unsigned char *) info + le32toh(info->biSize));
    for (unsigned int c = 0; c < 3; c++) {
      bitmasks[c] = le32toh(h_bitmasks[c]);
      rshifts[c] = stdc_trailing_zeros(bitmasks[c]);
      depths[c] = stdc_trailing_zeros((bitmasks[c] >> rshifts[c]) + 1);
    }

    for (unsigned int c = 0; c < 3; c++) {
      return_if_fail (depths[c] == 5 || depths[c] == 6) ERR(PL_ENOTSUP);
    }
  } else {
    return ERR(PL_ENOTSUP);
  }

  // get bmp geometry
  uint32_t bmp_width = PlzjRect_width(&image->rect);
  uint32_t bmp_height = PlzjRect_height(&image->rect);
  uint32_t bmp_width_h = bmp_width + (bmp_width & 1);

  const uint16_t *bmp_canvas = (const void *) (
    (const unsigned char *) image->buf.data + le32toh(header->bfOffBits));

  for (uint32_t y = 0; y < bmp_height; y++) {
    for (uint32_t x = 0; x < bmp_width; x++) {
      // bmp is upside down
      size_t bmp_offset = bmp_width_h * (bmp_height - y - 1) + x;
      struct PlzjColor *pixel =
        canvas + width * (y + image->rect.p1.y) + x + image->rect.p1.x;

      for (unsigned int c = 0; c < 3; c++) {
        pixel->values[c] = to_depth8(
          depths[c],
          (le16toh(bmp_canvas[bmp_offset]) & bitmasks[c]) >> rshifts[c]);
      }
      pixel->a = 0;
    }
  }

  return 0;
}


int PlzjImage_print (
    const struct PlzjImage *image, FILE *out, off_t offset) {
  int ret = fputs("  Type: image\n", out);
  if (offset != -1) {
    ret += fprintf(out, "    Offset: 0x%08jx\n", (intmax_t) offset);
  }
  ret += fprintf(
    out,
    "    Position: %4" PRId32 "x%3" PRId32 "+%4" PRId32 "x%3" PRId32
    " (%" PRIu32 "x%" PRIu32 ")\n",
    image->rect.p1.x, image->rect.p1.y, image->rect.p2.x, image->rect.p2.y,
    PlzjRect_width(&image->rect), PlzjRect_height(&image->rect));
  ret += fprintf(
    out, "    Stream length: 0x%zx (%zd)\n", image->seg.size, image->seg.size);
  return ret;
}


static int PlzjImage_uncompress_rle (
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

  return_if_fail (in_i == srclen / 2) ERR(PL_EFORMAT);

  *dstlenp = 2 * out_i;
  return 0;
}


static int PlzjImage_uncompress_zlib (
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
static int PlzjImage_uncompress_type (
    void *dst, size_t *dstlenp, void *src, size_t srclen, const void *key,
    unsigned int video_type) {
  return_if_fail (video_type <= (PLZJ_VIDEO_ZLIB | PLZJ_VIDEO_ENC))
    ERR(PL_ENOTSUP);

  if ((video_type & (PLZJ_VIDEO_ZLIB | PLZJ_VIDEO_ENC)) != 0) {
    plzj_image_encdec(src, srclen, key);
  }

  void *buf = NULL;
  int ret;

  if ((video_type & PLZJ_VIDEO_ZLIB) != 0) {
    size_t buflen = le32toh(*(uint32_t *) src);
    buf = malloc(buflen);
    return_if_fail (buf != NULL) ERR_SYS(malloc);

    ret = PlzjImage_uncompress_zlib(buf, &buflen, src, srclen);
    goto_if_fail (ret == 0) fail;

    src = buf;
    srclen = buflen;
  }

  ret = PlzjImage_uncompress_rle(dst, dstlenp, src, srclen);
fail:
  free(buf);
  return ret;
}


// lxefileplay::jkjieyasuobmp()
static int PlzjImage_uncompress_jk (
    void *dst, size_t *dstlenp, void *src, size_t srclen, const void *key) {
  return_if_fail (*(uint64_t *) src == (uint64_t) -1) ERR(PL_EFORMAT);

  unsigned int video_type = le32toh(((uint32_t *) src)[2]);
  size_t buflen = le32toh(((uint32_t *) src)[3]);
  size_t srclen_ = le32toh(((uint32_t *) src)[4]);
  void *src_ = (unsigned char *) src + 20;

  return_if_fail (srclen_ + 20 <= srclen) ERR(PL_EFORMAT);

  if (video_type < PLZJ_VIDEO_JK_MUL) {
    if_fail (*dstlenp >= buflen) {
      sc_warning("Jk buffer length too small, expected at least %zu, got %zu",
                 buflen, *dstlenp);
    }
    return PlzjImage_uncompress_type(
      dst, dstlenp, src_, srclen_, key, video_type);
  }

  return_if_fail (*dstlenp >= 54 && buflen > 0) ERR(PL_EINVAL);

  void *buf = malloc(buflen);
  return_if_fail (buf != NULL) ERR_SYS(malloc);

  int ret = PlzjImage_uncompress_type(
    buf, &buflen, src_, srclen_, key, video_type / PLZJ_VIDEO_JK_MUL);
  goto_if_fail (ret == 0 && buflen > 8) fail;

  // lxefileplay::buildbmpfilehead()
  uint16_t width = le16toh(((uint16_t *) buf)[0]);
  uint16_t height = le16toh(((uint16_t *) buf)[1]);
  if_fail (width * height + 8u <= buflen) {
    ret = ERR(PL_EFORMAT);
    goto fail;
  }

  uint16_t width_h = width + (width & 1);
  uint32_t image_size = 2 * width_h * height;
  uint32_t bmp_size =
    image_size + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

  BITMAPFILEHEADER *header = dst;
  *header = (BITMAPFILEHEADER) {
    {'B', 'M'}, htole32(bmp_size), 0, 0, htole32(54)
  };

  BITMAPINFOHEADER *info = (void *) (header + 1);
  *info = (BITMAPINFOHEADER) {
    htole32(40), htole32(width), htole32(height), htole16(1), htole16(16), 0,
    htole32(image_size), 0, 0, 0, 0
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
  const uint16_t *table;
  if (colors_cnt == 8) {
    table = color_8_table;
  } else if (colors_cnt == 64) {
    table = color_64_table;
  } else {
    ret = ERR_FMT(PL_EFORMAT, "Unknown jk color count %u", colors_cnt);
    goto fail;
  }

  uint16_t *pixels = (void *) (info + 1);
  const unsigned char *maps = (const unsigned char *) buf + 8;
  for (uint16_t y = 0; y < height; y++) {
    for (uint16_t x = 0; x < width; x++) {
      size_t pixels_offset = width_h * y + x;
      size_t offset = width * y + x;
      pixels[pixels_offset] = htole16(table[maps[offset]]);
    }
  }

  *dstlenp = bmp_size;

  ret = 0;
fail:
  free(buf);
  return ret;
}


static int PlzjImage_uncompress (
    void *dst, size_t *dstlenp, void *src, size_t srclen, const void *key,
    unsigned int video_type) {
  return *(uint64_t *) src == (uint64_t) -1 ?
    PlzjImage_uncompress_jk(dst, dstlenp, src, srclen, key) :
    PlzjImage_uncompress_type(dst, dstlenp, src, srclen, key, video_type);
}


int PlzjImage_read (
    struct PlzjImage *image, FILE *file, unsigned int video_type,
    const void *key) {
  return_if_fail (image->seg.size > 0) 0;
  return_if_fail (PlzjRect_valid(&image->rect)) ERR(PL_EFORMAT);

  size_t size = bmp_max_size(
    PlzjRect_width(&image->rect), PlzjRect_height(&image->rect));
  unsigned char *data = malloc(size);
  return_if_fail (data != NULL) ERR_SYS(malloc);

  int ret;

  {
    struct PlzjBuffer raw;
    ret = PlzjBuffer_init_file_seg(&raw, file, &image->seg);
    goto_if_fail (ret == 0) fail;
    promise(raw.size > 0);

    ret = PlzjImage_uncompress(
      data, &size, raw.data, raw.size, key, video_type);

    PlzjBuffer_destroy(&raw);
  }
  goto_if_fail (ret == 0) fail;

  if_fail (data[0] == 'B' && data[1] == 'M') {
    ret = ERR_WHAT(PL_EFORMAT, "Not BMP image");
    goto fail;
  }
  if_fail (size == le32toh(((BITMAPFILEHEADER *) data)->bfSize)) {
    ret = ERR_WHAT(PL_EFORMAT, "Image incomplete");
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


int PlzjImage_init_file (struct PlzjImage *image, FILE *file) {
  struct PlzjLxeImage h_image;
  return_if_fail (fread(&h_image, sizeof(h_image), 1, file) == 1)
    ERR_SYS(fread);
  return PlzjImage_init(image, &h_image);
}


int PlzjFrame_apply (
    const struct PlzjFrame *frame, struct PlzjColor *canvas,
    uint32_t width, uint32_t height) {
  int ret = 0;

  for (size_t i = 0; i < frame->patches_cnt; i++) {
    ret = PlzjImage_apply(frame->patches[i], canvas, width, height);
    break_if_fail (ret == 0);
  }

  return ret;
}


static int PlzjEncoder_write_apng (
    const struct PlzjEncoder *encoder, FILE *out, const struct Plzj *pl) {
  png_structp png_ptr = png_create_write_struct(
    PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  return_if_fail (png_ptr != NULL) ERR_PNG(png_create_write_struct);
  png_init_io(png_ptr, out);

  int ret;

  ret = plzj_png_write_info(png_ptr, encoder->width, encoder->height, pl);
  goto_if_fail (ret == 0) fail;

  ret = plzj_png_write_frames(png_ptr, encoder);

fail:
  png_destroy_write_struct(&png_ptr, NULL);
  return ret;
}


static int PlzjEncoder_stop (
    struct PlzjEncoder *encoder, uint32_t timecode_ms) {
  struct PlzjPngFrame *frame = ptrarray_new(
    &encoder->frames, &encoder->len, sizeof(*frame));
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
    struct PlzjPngFrame *frame = encoder->frames[i];
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


static int PlzjEncoder_append (
    struct PlzjEncoder *encoder, uint32_t timecode_ms,
    const struct PlzjColor *canvas, struct PlzjRect *rect_out) {
  uint32_t width = encoder->width;
  uint32_t height = encoder->height;

  struct PlzjRect rect;
  return_if_fail (plzj_canvas_diff(
    canvas, encoder->canvas, width, height, &rect)) 1;

  sc_debug(
    "Drawing frame %zu at %" PRIu32 " ms from (%" PRId32 ", %" PRId32
    ") to (%" PRId32 ", %" PRId32 ")\n",
    encoder->len, timecode_ms, rect.p1.x, rect.p1.y, rect.p2.x, rect.p2.y);

  size_t size;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
  unsigned char *scanline = plzj_png_gendiff(
    canvas, encoder->canvas, &rect, width, height, &size);
  return_if_fail (scanline != NULL) -sc_exc.code;

  int ret;

  struct PlzjPngFrame *frame = ptrarray_new(
    &encoder->frames, &encoder->len, sizeof(*frame));
  if_fail (frame != NULL) {
    ret = -sc_exc.code;
    goto fail_frame;
  }

  ret = plzj_compress_bg(
    scanline, size, 4, &frame->fdAT, &frame->size, encoder->compression_level,
    &encoder->pool);
  goto_if_fail (ret == 0) fail_compress;
  frame->rect = rect;
  frame->timecode_ms = timecode_ms;

  pixel_canvas_copy(encoder->canvas, canvas, &rect, width, height);

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


static int PlzjEncoder_append_cursor (
    struct PlzjEncoder *encoder, uint32_t timecode_ms, struct PlzjColor *canvas,
    const struct PlzjCursor *cursor, struct PlzjColor *canvas_buf,
    bool click_again, struct PlzjRect *rect_click, bool *draw_clickp) {
  uint32_t width = encoder->width;
  uint32_t height = encoder->height;

  struct PlzjRect rect_cursor;

  bool draw_cursor = PlzjCursor_rect(cursor, width, height, &rect_cursor);
  bool draw_click = click_again ? *draw_clickp :
    PlzjClick_rect(&cursor->event, width, height, rect_click);

  if (draw_cursor) {
    pixel_canvas_copy(canvas_buf, canvas, &rect_cursor, width, height);
  }
  if (!click_again && draw_click) {
    pixel_canvas_copy(canvas_buf, canvas, rect_click, width, height);
  }

  int ret;

  if (draw_cursor) {
    ret = PlzjCursor_apply(cursor, canvas, width, height, NULL);
    goto_if_fail (ret >= 0) fail;
  }
  if (draw_click) {
    PlzjClick_apply(&cursor->event, canvas, width, height, NULL);
  }

  ret = PlzjEncoder_append(encoder, timecode_ms, canvas, NULL);
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


static void PlzjEncoder_destroy (struct PlzjEncoder *encoder) {
  free(encoder->canvas);
  for (size_t i = 0; i < encoder->len + encoder->end; i++) {
    free(encoder->frames[i]->fdAT);
    free(encoder->frames[i]);
  }
  free(encoder->frames);
  ThreadPool_destroy(&encoder->pool);
}


static int PlzjEncoder_init (
    struct PlzjEncoder *encoder, uint32_t width, uint32_t height,
    int compression_level, unsigned int nproc) {
  size_t canvas_len = sizeof(*encoder->canvas) * width * height;
  encoder->canvas = malloc(canvas_len);
  if_fail (encoder->canvas != NULL) {
    return ERR_SYS(malloc);
  }

  int ret = ThreadPool_init(&encoder->pool, nproc, "png");
  if_fail (ret == 0) {
    free(encoder->canvas);
    return ret;
  }

  memset(encoder->canvas, PLZJ_PNG_TRANSPARENT, canvas_len);
  encoder->frames = NULL;
  encoder->len = 0;
  encoder->width = width;
  encoder->height = height;
  encoder->compression_level = compression_level;
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


static int32_t IplKernel_evaluate (
    const struct IplKernel *kern, const int32_t *samples, unsigned int i) {
  const float *coeff = kern->coeffs + DIM * (i - 1);

  float ret = 0;
  for (unsigned int i = 0; i < DIM; i++) {
    ret += coeff[i] * samples[i];
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

  kern->coeffs = malloc(sizeof(*kern->coeffs) * DIM * (k - 1));
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
static inline int32_t dotII (const int32_t *v1, const int32_t *v2) {
  int32_t ret = 0;
  for (unsigned int i = 0; i < DIM; i++) {
    ret += v1[i] * v2[i];
  }
  return ret;
}


struct IplKernel {
  int32_t *coeffs;
  float magnitude;
};


static int32_t IplKernel_evaluate (
    const struct IplKernel *kern, const int32_t *samples, unsigned int i) {
  return kern->magnitude * dotII(kern->coeffs + DIM * (i - 1), samples) + .5;
}


static void IplKernel_destroy (struct IplKernel *kern) {
  free(kern->coeffs);
}


static int IplKernel_init (struct IplKernel *kern, unsigned int k) {
  if_fail (k > 1) {
    kern->coeffs = NULL;
    kern->magnitude = 0;
    return 0;
  }

  kern->coeffs = malloc(sizeof(*kern->coeffs) * DIM * (k - 1));
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
      coeff[r] = dotII(m[r], js);
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


static int32_t IplKernel_evaluate (
    const struct IplKernel *kern, const int32_t *samples, unsigned int i) {
  int32_t left = samples[DIM / 2 - 1];
  int32_t right = samples[DIM / 2];
  return left + ((right - left) * (int) i + (int) kern->k / 2) / (int) kern->k;
}


static void IplKernel_destroy (struct IplKernel *kern) {
  (void) kern;
}


static int IplKernel_init (struct IplKernel *kern, unsigned int k) {
  kern->k = k;
  return 0;
}

#endif

int PlzjVideo_write_apng (
    const struct PlzjVideo *video, FILE *out, unsigned int flags,
    unsigned int transitions_cnt, int compression_level, unsigned int nproc) {
  return_if_fail (
    video->frames != NULL && video->frames[0] != NULL &&
    video->frames[0]->patches != NULL &&
    video->frames[0]->patches[0] != NULL) ERR(PL_EINVAL);
  const struct Plzj *pl = video->pl;
  return_if_fail (pl != NULL && pl->key_set >= 0) ERR(PL_EKEY);

  bool use_subframes = (flags & 1) != 0;
  bool with_cursor = (flags & 2) != 0;
  if (video->cursors_cnt <= 0) {
    with_cursor = false;
  }

  const struct PlzjImage *first = video->frames[0]->patches[0];
  uint32_t width = first->rect.p2.x;
  uint32_t height = first->rect.p2.y;

  // acquire resources
  struct PlzjEncoder encoder;
  return_with_nonzero (PlzjEncoder_init(
    &encoder, width, height, compression_level, nproc));

  int ret;

  size_t canvas_size = width * height;
  canvas_size = (canvas_size + 1023) & ~1024;
  struct PlzjColor *canvas = malloc(2 * sizeof(*canvas) * canvas_size);
  if_fail (canvas != NULL) {
    ret = ERR_SYS(malloc);
    goto fail_canvas;
  }
  struct PlzjColor *canvas_saved = canvas + canvas_size;

  uint32_t *timecodes_ipl = NULL;  // [transitions_cnt]
  if (with_cursor && transitions_cnt > 0) {
    timecodes_ipl = malloc(sizeof(*timecodes_ipl) * transitions_cnt);
    if_fail (timecodes_ipl != NULL) {
      ret = ERR_SYS(malloc);
      goto fail_timecodes_ipl;
    }
    for (unsigned int j = 0; j < transitions_cnt; j++) {
      timecodes_ipl[j] = video->frame_ms * (j + 1) / (transitions_cnt + 1);
    }
  }

  struct IplKernel kern;
  ret = IplKernel_init(&kern, !with_cursor ? 0 : transitions_cnt + 1);
  goto_if_fail (ret >= 0) fail_kern;

  // process frames
  if (with_cursor) {
    sc_info("Transition frames: %u\n", transitions_cnt);
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

    struct PlzjFrame *frame = video->frames[i];
    uint32_t timecode_base = video->frame_ms * i;

    // revert cursor subframe
    if (use_subframes && last_draw_cursor) {
      ret = PlzjEncoder_append(&encoder, timecode_base, canvas, NULL);
      goto_if_fail (ret >= 0) fail_frame;
      last_draw_cursor = false;
    }

    // apply / draw subframes
    for (size_t j = 0; j < frame->patches_cnt; j++) {
      struct PlzjImage *patch = frame->patches[j];
      bool read = patch->buf.data == NULL;

      if (read) {
        ret = PlzjImage_read(
          patch, pl->file, le32toh(pl->player.video_type),
          pl->key_set <= 0 ? NULL : pl->key);
        goto_if_fail (ret == 0) fail_frame;
      }

      ret = PlzjImage_apply(patch, canvas, width, height);

      if (read) {
        free(patch->buf.data);
        patch->buf.data = NULL;
      }
      goto_if_fail (ret == 0) fail_frame;

      if (use_subframes) {
        ret = PlzjEncoder_append(&encoder, timecode_base, canvas, NULL);
        goto_if_fail (ret >= 0) fail_frame;
      }
    }

    // draw main frame (with cursor)
    const struct PlzjCursor *cursor = &frame->cursor;
    bool cursor_valid =
      with_cursor && PlzjCursor_valid(cursor) && cursor->tbuf != NULL;

    struct PlzjRect rect_click;
    bool draw_click;

    if (cursor_valid) {
      sc_debug("Cursor %zu: %d %d\n", i, cursor->p.x, cursor->p.y);
      ret = PlzjEncoder_append_cursor(
        &encoder, timecode_base, canvas, cursor, canvas_saved, false,
        &rect_click, &draw_click);
      goto_if_fail (ret >= 0) fail_frame;
      last_draw_cursor = true;
    } else if (!use_subframes) {
      ret = PlzjEncoder_append(&encoder, timecode_base, canvas, NULL);
      goto_if_fail (ret >= 0) fail_frame;
    }

    // draw cursor interpolation
    do {
      if (!cursor_valid || transitions_cnt <= 0 || i + 1 >= video->frames_cnt) {
        break;
      }

      const struct PlzjCursor *cursor_next = &video->frames[i + 1]->cursor;
      if (!PlzjCursor_valid(cursor_next)) {
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
        const struct PlzjCursor *cursor_bw = &video->frames[i - a]->cursor;
        if (i < a || !PlzjCursor_valid(cursor_bw)) {
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
        const struct PlzjCursor *cursor_fw = &video->frames[i + a + 1]->cursor;
        if (i + a + 1 >= video->frames_cnt || !PlzjCursor_valid(cursor_fw)) {
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
        sc_log_print("X samples:");
        for (unsigned int r = 0; r < DIM; r++) {
          sc_log_print(" %" PRId32, xs[r]);
        }
        sc_log_print(", Y samples:");
        for (unsigned int r = 0; r < DIM; r++) {
          sc_log_print(" %" PRId32, ys[r]);
        }
        sc_log_print("\n");
        sc_log_end(SC_LOG_DEBUG);
      }

      struct PlzjCursor cursor_mid = frame->cursor;
      for (unsigned int j = 0; j < transitions_cnt; j++) {
        if (diff_x) {
          cursor_mid.p.x = IplKernel_evaluate(&kern, xs, j + 1);
        }
        if (diff_y) {
          cursor_mid.p.y = IplKernel_evaluate(&kern, ys, j + 1);
        }
        sc_debug(
          "Cursor %zu + %u: %d %d\n", i, j + 1, cursor_mid.p.x, cursor_mid.p.y);

        ret = PlzjEncoder_append_cursor(
          &encoder, timecode_base + timecodes_ipl[j], canvas, &cursor_mid,
          canvas_saved, true, &rect_click, &draw_click);
        goto_if_fail (ret >= 0) fail_frame;
      }
    } while (0);
  }
  sc_notice("\n");

  ret = PlzjEncoder_stop(&encoder, video->frame_ms * video->frames_cnt);
  goto_if_fail (ret == 0) fail;

  ret = PlzjEncoder_write_apng(&encoder, out, pl);
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
  PlzjEncoder_destroy(&encoder);
  return ret;
}


int PlzjVideo_save_apng (
    const struct PlzjVideo *video, const char *path, unsigned int flags,
    unsigned int transitions_cnt, int compression_level, unsigned int nproc) {
  return_if_fail (
    video->frames != NULL && video->frames[0] != NULL &&
    video->frames[0]->patches != NULL &&
    video->frames[0]->patches[0] != NULL) ERR(PL_EINVAL);
  const struct Plzj *pl = video->pl;
  return_if_fail (pl != NULL && pl->key_set >= 0) ERR(PL_EKEY);

  FILE *out = fopen(path, "wb");
  return_if_fail (out != NULL) ERR_SYS(fopen);
  int ret = PlzjVideo_write_apng(
    video, out, flags, transitions_cnt, compression_level, nproc);
  fclose(out);
  return ret;
}


int PlzjVideo_save_cursors (const struct PlzjVideo *video, const char *dir) {
  return_if_fail (video->cursors_cnt > 0 && video->cursors != NULL)
    ERR(PL_EINVAL);

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
    struct PlzjTaggedBuffer *tbuf = video->cursors[i];
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
    struct PlzjCursor *cursor = &video->frames[i]->cursor;
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


static int PlzjVideo_read_cursor (
    struct PlzjVideo *video, FILE *in, struct PlzjCursor *cursor,
    struct PlzjTaggedBuffer **tbufp) {
  if_fail (cursor->seg.size > 0) {
    cursor->tbuf = *tbufp;
    return 0;
  }

  int ret;

  struct PlzjBuffer buf;
  ret = PlzjBuffer_init_file_seg(&buf, in, &cursor->seg);
  return_if_fail (ret == 0) ret;

  unsigned long tag = plzj_crc32(buf.data, buf.size);

  struct PlzjTaggedBuffer *tbuf;
  for (size_t j = 0; j < video->cursors_cnt; j++) {
    tbuf = video->cursors[j];
    if (tbuf->tag == tag) {
      cursor->tbuf = tbuf;
      *tbufp = tbuf;

      ret = 0;
      goto fail;
    }
  }

  tbuf = ptrarray_new(&video->cursors, &video->cursors_cnt, sizeof(*tbuf));
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
  PlzjBuffer_destroy(&buf);
  return ret;
}


int PlzjVideo_read_cursors (
    struct PlzjVideo *video, FILE *in, struct PlzjTaggedBuffer **tbufp) {
  int ret;

  for (size_t i = 0; i < video->frames_cnt; i++) {
    struct PlzjCursor *cursor = &video->frames[i]->cursor;
    ret = PlzjVideo_read_cursor(video, in, cursor, tbufp);
    goto_if_fail (ret == 0) fail;
  }

  ret = 0;
fail:
  return ret;
}


int PlzjVideo_parse_clicks (
    struct PlzjVideo *video, const char *src, size_t size) {
  return_if_fail (size > 0) 0;

  long buf[8];
  unsigned int i = 0;

  char *cur = (void *) src;
  do {
    long l = strtol(cur, &cur, 10);
    return_if_fail (isspace(*cur)) ERR(PL_EINVAL);

    buf[i] = l;
    i++;

    do {
      if (i < arraysize(buf)) {
        break;
      }
      i = 0;

      if (buf[0] < 0 || (unsigned long) buf[0] >= video->frames_cnt) {
        break;
      }
      struct PlzjClick *event = &video->frames[buf[0]]->cursor.event;
      event->p.x = buf[1];
      event->p.y = buf[2];
      event->type = buf[3];

      if (buf[3] != 3 || buf[0] <= 0) {
        break;
      }
      // additional hint for double click
      struct PlzjClick *event_before = &video->frames[buf[0] - 1]->cursor.event;
      event_before->p.x = buf[1];
      event_before->p.y = buf[2];
      event_before->type = 1;
    } while (0);

    for (; cur < src + size && isspace(*cur); cur++) {}
  } while (cur < src + size);

  return 0;
}


int PlzjVideo_read_clicks (
    struct PlzjVideo *video, const struct Plzj *pl) {
  return_if_fail (pl->clicks_offset != -1) 0;

  char *clicks = malloc(pl->clicks_size);
  return_if_fail (clicks != NULL) ERR_SYS(malloc);

  int ret;

  ret = read_at(pl->file, pl->clicks_offset, SEEK_SET, clicks, pl->clicks_size);
  goto_if_fail (ret == 0) fail;

  ret = PlzjVideo_parse_clicks(video, clicks, pl->clicks_size);

fail:
  free(clicks);
  return ret;
}


int PlzjVideo_init (
    struct PlzjVideo *video, const struct Plzj *pl,
    int32_t frames_limit, bool read_cursor) {
  struct PlzjLxePacketIter iter;
  PlzjLxePacketIter_init(&iter, pl, frames_limit);

  video->frames = NULL;
  video->frames_cnt = 0;
  video->cursors = NULL;
  video->cursors_cnt = 0;

  struct PlzjFrame *frame = NULL;
  struct PlzjTaggedBuffer *tbuf = NULL;
  int ret;

  while (true) {
    int state = PlzjLxePacketIter_next(&iter);
    if_fail (state >= 0) {
      ret = state;
      goto fail;
    }

    if (state == PlzjLxePacketIter_NEXT_FRAME) {
      break_if_fail (iter.frame_no < iter.frames_cnt);
      frame = ptrarray_new(&video->frames, &video->frames_cnt, sizeof(*frame));
      if_fail (frame != NULL) {
        ret = -sc_exc.code;
        goto fail;
      }
      PlzjFrame_init(frame);
    } else if (frame != NULL) {
      if (state == PlzjLxePacketIter_NEXT_IMAGE) {
        struct PlzjImage *patch = ptrarray_new(
          &frame->patches, &frame->patches_cnt, sizeof(*patch));
        if_fail (patch != NULL) {
          ret = -sc_exc.code;
          goto fail;
        }
        PlzjImage_init(patch, &iter.packet.image);
        patch->seg.offset = iter.offset;
      } else {
        struct PlzjCursor *cursor = &frame->cursor;
        PlzjCursor_init(cursor, &iter.packet.cursor);
        cursor->seg.offset = iter.offset;

        if (read_cursor) {
          ret = PlzjVideo_read_cursor(video, pl->file, cursor, &tbuf);
          goto_if_fail (ret == 0) fail;
        }
      }
    }
  }

  video->pl = pl;
  video->frame_ms = le32toh(pl->video.frame_ms);
  return 0;

fail:
  PlzjVideo_destroy(video);
  return ret;
}


int Plzj_extract_video_or_cursor (
    const struct Plzj *pl, const char *dir, int32_t frames_limit,
    unsigned int flags, unsigned int transitions_cnt, int compression_level,
    unsigned int nproc, bool extract_video, bool extract_cursor) {
  return_if_fail (extract_video || extract_cursor) 0;

  struct PlzjVideo video;
  return_with_nonzero (PlzjVideo_init(&video, pl, frames_limit, true));

  int ret;

  if (extract_video) {
    bool with_cursor = (flags & 2) != 0;

    if (with_cursor && pl->clicks_offset != -1) {
      ret = PlzjVideo_read_clicks(&video, pl);
      goto_if_fail (ret == 0) fail;
    }

    size_t dir_len = strlen(dir);
    char path[dir_len + 65];
    memcpy(path, dir, dir_len);
    char *filename = path + dir_len;
    filename[0] = '/';
    filename++;
    snprintf(filename, 64, with_cursor ? "video.apng" : "video_raw.apng");

    ret = PlzjVideo_save_apng(
      &video, path, flags, transitions_cnt, compression_level, nproc);
    goto_if_fail (ret == 0) fail;
  }
  if (extract_cursor) {
    ret = PlzjVideo_save_cursors(&video, dir);
    goto_if_fail (ret == 0) fail;
  }

  ret = 0;
fail:
  PlzjVideo_destroy(&video);
  return ret;
}
