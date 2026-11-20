// Minimal C API over jpegli, libwebp and libjxl for use from NAPS2 (via P/Invoke).
//
// The libjpeg-style jpegli API can't be used directly from managed code: jpeg_compress_struct has a
// platform-dependent layout, and fatal errors are reported through an error_exit callback that must not
// return (which requires setjmp/longjmp). This wrapper handles both and exposes plain functions. libwebp, libjxl and
// skcms are wrapped similarly (one call per operation), which keeps their struct layouts and version-specific
// details out of managed code, and their symbols hidden.
//
// All functions return 0 on success and non-zero on failure, in which case a null-terminated message is written to
// error/error_size (which may be null/0). Output buffers must be released with naps2_jpeg_free.

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

// Pixel layouts. For naps2_jpeg_compress, the 4th byte of RGBA32/BGRA32 is ignored.
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

// Releases a buffer returned by any of the naps2_* functions.
NAPS2_JPEG_API void NAPS2_JPEG_CALL naps2_jpeg_free(uint8_t* data);

// The functions below take and return tightly packed, top-down GRAY8, RGB24 or RGBA32 pixels (RGBA32 meaning
// unpremultiplied alpha). Exif metadata is passed as a TIFF payload (exif/exif_size may be null/0), and returned the
// same way in a buffer (null if there's none) that must also be released with naps2_jpeg_free.

// Encodes an RGB24 or RGBA32 image as WebP. quality is 0-100: the visual quality for lossy compression, or the
// compression effort for lossless. The image can be at most 16383x16383 pixels (a limit of the WebP format).
NAPS2_JPEG_API int NAPS2_JPEG_CALL naps2_webp_compress(
    const uint8_t* pixels, int width, int height, int pixel_format, int quality, int lossless, const uint8_t* exif,
    size_t exif_size, uint8_t** out_data, size_t* out_size, char* error, int error_size);

// Decodes a WebP image (the first frame, if it's animated) to sRGB as RGB24, or RGBA32 if it has an alpha channel.
NAPS2_JPEG_API int NAPS2_JPEG_CALL naps2_webp_decompress(
    const uint8_t* data, size_t size, uint8_t** out_pixels, int* out_width, int* out_height, int* out_pixel_format,
    uint8_t** out_exif, size_t* out_exif_size, char* error, int error_size);

// Encodes an image as JPEG XL. quality is 1-100, roughly matching libjpeg quality (ignored if lossless).
NAPS2_JPEG_API int NAPS2_JPEG_CALL naps2_jxl_compress(
    const uint8_t* pixels, int width, int height, int pixel_format, int quality, int lossless, const uint8_t* exif,
    size_t exif_size, uint8_t** out_data, size_t* out_size, char* error, int error_size);

// Losslessly recompresses a JPEG file as JPEG XL. The original JPEG file can be reconstructed exactly from the
// result. Any Exif metadata in the JPEG is kept, so exif should only be given if it doesn't have any.
NAPS2_JPEG_API int NAPS2_JPEG_CALL naps2_jxl_transcode_jpeg(
    const uint8_t* jpeg_data, size_t jpeg_size, const uint8_t* exif, size_t exif_size, uint8_t** out_data,
    size_t* out_size, char* error, int error_size);

// Decodes a JPEG XL image (the first frame, if it's animated) to sRGB as GRAY8, RGB24 or RGBA32 depending on the
// image's channels.
NAPS2_JPEG_API int NAPS2_JPEG_CALL naps2_jxl_decompress(
    const uint8_t* data, size_t size, uint8_t** out_pixels, int* out_width, int* out_height, int* out_pixel_format,
    uint8_t** out_exif, size_t* out_exif_size, char* error, int error_size);

// Converts GRAY8, RGB24 or RGBA32 pixels (rows stride bytes apart) in place from the given ICC color profile to sRGB.
// Fails without changing the pixels if the profile can't be applied (e.g. a CMYK profile with RGB pixels).
NAPS2_JPEG_API int NAPS2_JPEG_CALL naps2_icc_to_srgb(
    uint8_t* pixels, int width, int height, int stride, int pixel_format, const uint8_t* icc, size_t icc_size,
    char* error, int error_size);

#ifdef __cplusplus
}
#endif

#endif // NAPS2_JPEG_H
