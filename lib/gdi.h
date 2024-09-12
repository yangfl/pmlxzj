#ifndef GDI_H
#define GDI_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdint.h>

#include "include/defs.h"


typedef struct __packed tagBITMAPFILEHEADER {
  /// BMP file type
  uint8_t bfType[2];
  /// File size
  uint32_t bfSize;
  uint16_t bfReserved1;
  uint16_t bfReserved2;
  /// Offset to pixel data
  uint32_t bfOffBits;
} BITMAPFILEHEADER;


typedef struct __packed tagBITMAPINFOHEADER {
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
} BITMAPINFOHEADER;

/* biCompression */
#define BI_RGB           0
#define BI_RLE8          1
#define BI_RLE4          2
#define BI_BITFIELDS     3
#define BI_JPEG          4
#define BI_PNG           5


struct BMPColor {
  union {
    struct {
      uint8_t b;
      uint8_t g;
      uint8_t r;
      uint8_t a;
    };
    uint8_t values[4];
    uint32_t color;
  };
};
static_assert(sizeof(struct BMPColor) == 4);


typedef struct __packed tagICONHEADER {
  uint16_t idReserved;
  uint16_t idType;
  uint16_t idCount;
} ICONHEADER;


typedef struct __packed tagICONDIRENTRY {
  uint8_t bWidth;
  uint8_t bHeight;
  uint8_t bColorCount;
  uint8_t bReserved;
  uint16_t wPlanes;
  uint16_t wBitCount;
  uint32_t dwDIBSize;
  uint32_t dwDIBOffset;
} ICONDIRENTRY;


#ifdef __cplusplus
}
#endif

#endif /* GDI_H */
