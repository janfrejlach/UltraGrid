#ifndef JPEGXS_CONV_H
#define JPEGXS_CONV_H

#include <svt-jpegxs/SvtJpegxs.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// The conversion between UltraGrid pixel format and JPEG XS format
struct uv_to_jpegxs_conversion {
        codec_t src;
        ColourFormat_t dst;
        void (*convert)(const uint8_t *src, int width, int height, svt_jpeg_xs_image_buffer_t *dst);
};

// The conversion between JPEG XS format and UltraGrid pixel format
struct jpegxs_to_uv_conversion {
        ColourFormat_t src;
        codec_t dst;
        void (*convert)(const svt_jpeg_xs_image_buffer_t *src, int width, int height, uint8_t *dst);
};

// Select the correct conversion function from given UltraGrid pixel format to JPEG XS format
const struct uv_to_jpegxs_conversion *get_uv_to_jpegxs_conversion(codec_t codec);

// Select the correct conversion function from JPEG XS format to given UltraGrid pixel format
const struct jpegxs_to_uv_conversion *get_jpegxs_to_uv_conversion(codec_t codec);

#ifdef __cplusplus
}
#endif

#endif // JPEGXS_CONV_H