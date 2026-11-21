#include "naps2_jpeg.h"
#include "naps2_util.h"
#include "skcms.h"

using namespace naps2;

namespace {

struct IccFormat {
    skcms_PixelFormat format;
    int bytes_per_pixel;
};

IccFormat GetIccFormat(int pixel_format) {
    switch (pixel_format) {
        case NAPS2_JPEG_PIXEL_GRAY8: return {skcms_PixelFormat_G_8, 1};
        case NAPS2_JPEG_PIXEL_RGB24: return {skcms_PixelFormat_RGB_888, 3};
        case NAPS2_JPEG_PIXEL_BGR24: return {skcms_PixelFormat_BGR_888, 3};
        case NAPS2_JPEG_PIXEL_RGBA32: return {skcms_PixelFormat_RGBA_8888, 4};
        case NAPS2_JPEG_PIXEL_BGRA32: return {skcms_PixelFormat_BGRA_8888, 4};
        default: return {skcms_PixelFormat_G_8, 0};
    }
}

}  // namespace

extern "C" int NAPS2_JPEG_CALL naps2_icc_to_srgb(
    uint8_t* pixels, int width, int height, int stride, int pixel_format, const uint8_t* icc, size_t icc_size,
    char* error, int error_size) {
    return Run(error, error_size, [&] {
        auto [format, bytes_per_pixel] = GetIccFormat(pixel_format);
        Check(pixels != nullptr && icc != nullptr && width > 0 && height > 0 && bytes_per_pixel > 0 &&
                  stride >= static_cast<int64_t>(width) * bytes_per_pixel,
              "Invalid arguments");
        skcms_ICCProfile profile;
        Check(skcms_Parse(icc, icc_size, &profile), "Invalid or unsupported color profile");
        // e.g. a CMYK profile for an image that was already converted to RGB
        Check(profile.data_color_space == (bytes_per_pixel == 1 ? skcms_Signature_Gray : skcms_Signature_RGB),
              "The color profile doesn't match the pixel format");
        // Most embedded profiles are sRGB (or equivalent), which doesn't need converting
        if (skcms_ApproximatelyEqualProfiles(&profile, skcms_sRGB_profile())) return;
        auto alpha = bytes_per_pixel == 4 ? skcms_AlphaFormat_Unpremul : skcms_AlphaFormat_Opaque;
        for (int y = 0; y < height; ++y) {
            uint8_t* row = pixels + static_cast<size_t>(y) * stride;
            Check(skcms_Transform(row, format, alpha, &profile, row, format, alpha, skcms_sRGB_profile(), width),
                  "The color profile can't be applied");
        }
    });
}
