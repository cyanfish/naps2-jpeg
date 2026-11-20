#include "naps2_jpeg.h"
#include "naps2_util.h"
#include "skcms.h"

using namespace naps2;

extern "C" int NAPS2_JPEG_CALL naps2_icc_to_srgb(
    uint8_t* pixels, int width, int height, int stride, int pixel_format, const uint8_t* icc, size_t icc_size,
    char* error, int error_size) {
    return Run(error, error_size, [&] {
        int bytes_per_pixel = BytesPerPixel(pixel_format);
        Check(pixels != nullptr && icc != nullptr && width > 0 && height > 0 && bytes_per_pixel > 0 &&
                  stride >= static_cast<int64_t>(width) * bytes_per_pixel,
              "Invalid arguments");
        skcms_ICCProfile profile;
        Check(skcms_Parse(icc, icc_size, &profile), "Invalid or unsupported color profile");
        // e.g. a CMYK profile for an image that was already converted to RGB
        Check(profile.data_color_space == (bytes_per_pixel == 1 ? skcms_Signature_Gray : skcms_Signature_RGB),
              "The color profile doesn't match the pixel format");
        auto format = bytes_per_pixel == 1   ? skcms_PixelFormat_G_8
                      : bytes_per_pixel == 3 ? skcms_PixelFormat_RGB_888
                                             : skcms_PixelFormat_RGBA_8888;
        auto alpha = bytes_per_pixel == 4 ? skcms_AlphaFormat_Unpremul : skcms_AlphaFormat_Opaque;
        for (int y = 0; y < height; ++y) {
            uint8_t* row = pixels + static_cast<size_t>(y) * stride;
            Check(skcms_Transform(row, format, alpha, &profile, row, format, alpha, skcms_sRGB_profile(), width),
                  "The color profile can't be applied");
        }
    });
}
