// Minimal C API over jpegli for use from NAPS2 (via P/Invoke).
//
// The libjpeg-style jpegli API can't be used directly from managed code: jpeg_compress_struct has a
// platform-dependent layout, and fatal errors are reported through an error_exit callback that must not
// return (which requires setjmp/longjmp). This wrapper handles both and exposes plain functions.

#ifndef NAPS2_JPEG_H
#define NAPS2_JPEG_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define NAPS2_JPEG_API __declspec(dllexport)
#define NAPS2_JPEG_CALL __cdecl
#else
#define NAPS2_JPEG_API __attribute__((visibility("default")))
#define NAPS2_JPEG_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Pixel layouts accepted by naps2_jpeg_compress. Alpha/padding bytes are ignored.
enum {
    NAPS2_JPEG_PIXEL_RGB24 = 0,
    NAPS2_JPEG_PIXEL_BGR24 = 1,
    NAPS2_JPEG_PIXEL_RGBA32 = 2,
    NAPS2_JPEG_PIXEL_BGRA32 = 3,
    NAPS2_JPEG_PIXEL_GRAY8 = 4,
};

// Encodes an image as a baseline JPEG with jpegli (adaptive quantization, optimized Huffman codes, 4:2:0 chroma
// subsampling for color images).
//
// pixels/stride: top-down rows; stride is the number of bytes between the starts of consecutive rows.
// quality: 1-100, roughly matching libjpeg quality (mapped to a jpegli butteraugli distance).
// x_dpi/y_dpi: written to the JFIF header if both are positive (rounded, max 65535); otherwise no density is set.
// out_data/out_size: on success, receives a buffer that must be released with naps2_jpeg_free.
// error/error_size: on failure, receives a null-terminated message (may be null/0).
//
// Returns 0 on success, non-zero on failure.
NAPS2_JPEG_API int NAPS2_JPEG_CALL naps2_jpeg_compress(
    const uint8_t* pixels, int width, int height, int stride, int pixel_format, int quality,
    double x_dpi, double y_dpi, uint8_t** out_data, size_t* out_size, char* error, int error_size);

// Releases a buffer returned by naps2_jpeg_compress.
NAPS2_JPEG_API void NAPS2_JPEG_CALL naps2_jpeg_free(uint8_t* data);

#ifdef __cplusplus
}
#endif

#endif // NAPS2_JPEG_H
