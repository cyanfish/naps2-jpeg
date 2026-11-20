#include "naps2_util.h"

#include <string.h>

#include "naps2_jpeg.h"

namespace naps2 {

void CopyToOutput(const uint8_t* data, size_t size, uint8_t** out_data, size_t* out_size) {
    Buffer buffer = Allocate(size);
    memcpy(buffer.get(), data, size);
    *out_data = buffer.release();
    *out_size = size;
}

int BytesPerPixel(int pixel_format) {
    switch (pixel_format) {
        case NAPS2_JPEG_PIXEL_GRAY8: return 1;
        case NAPS2_JPEG_PIXEL_RGB24: return 3;
        case NAPS2_JPEG_PIXEL_RGBA32: return 4;
        default: return 0;
    }
}

}  // namespace naps2
