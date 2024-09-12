#ifndef BMP_H
#define BMP_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "include/defs.h"


typedef struct tagBITMAPFILEHEADER {
  /// BMP file type
  uint8_t bfType[2];
  /// File size
  uint32_t bfSize;
  uint16_t bfReserved1;
  uint16_t bfReserved2;
  /// Offset to pixel data
  uint32_t bfOffBits;
} __packed BITMAPFILEHEADER;


typedef struct tagBITMAPINFOHEADER {
  /// Header size
  uint32_t biSize;
  /// Width
  int32_t biWidth;
  /// Height
  int32_t biHeight;
  /// Planes
  uint16_t biPlanes;
  /// Bits per pixel
  uint16_t biBitCount;
  uint32_t biCompression;
  /// Image size
  uint32_t biSizeImage;
  int32_t biXPelsPerMeter;
  int32_t biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
} __packed BITMAPINFOHEADER;

/* biCompression */
#define BI_RGB           0
#define BI_RLE8          1
#define BI_RLE4          2
#define BI_BITFIELDS     3
#define BI_JPEG          4
#define BI_PNG           5


extern const unsigned char to_depth8_table[8][256];

__wur __attribute_const__
static inline unsigned char to_depth8 (
    unsigned char depth, unsigned char color) {
  return to_depth8_table[depth - 1][color];
}


#ifdef __cplusplus
}
#endif

#endif /* BMP_H */
