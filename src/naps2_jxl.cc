#include <string.h>

#include <vector>

#include "jxl/cms.h"
#include "jxl/decode.h"
#include "jxl/encode.h"
#include "jxl/thread_parallel_runner.h"
#include "naps2_jpeg.h"
#include "naps2_util.h"

using namespace naps2;

namespace {

using Runner = std::unique_ptr<void, decltype(&JxlThreadParallelRunnerDestroy)>;

Runner CreateRunner() {
    Runner runner(JxlThreadParallelRunnerCreate(nullptr, JxlThreadParallelRunnerDefaultNumWorkerThreads()),
                  JxlThreadParallelRunnerDestroy);
    Check(runner != nullptr, "Out of memory");
    return runner;
}

// Wraps an encoder with an output buffer and a parallel runner.
struct Encoder {
    std::unique_ptr<JxlEncoder, decltype(&JxlEncoderDestroy)> ptr{JxlEncoderCreate(nullptr), JxlEncoderDestroy};
    Runner runner = CreateRunner();
    JxlEncoderFrameSettings* settings;

    Encoder() {
        Check(ptr != nullptr, "Out of memory");
        Check(JxlEncoderSetParallelRunner(ptr.get(), JxlThreadParallelRunner, runner.get()));
        settings = JxlEncoderFrameSettingsCreate(ptr.get(), nullptr);
        Check(settings != nullptr, "Out of memory");
    }

    void Check(JxlEncoderStatus status) {
        if (status == JXL_ENC_SUCCESS) return;
        switch (JxlEncoderGetError(ptr.get())) {
            case JXL_ENC_ERR_OOM: throw Error{"Out of memory"};
            case JXL_ENC_ERR_JBRD: throw Error{"The JPEG can't be losslessly recompressed"};
            case JXL_ENC_ERR_BAD_INPUT: throw Error{"Invalid input"};
            default: throw Error{"Error encoding JPEG XL"};
        }
    }

    static void Check(bool ok, const char* message) { naps2::Check(ok, message); }

    // Adds an Exif box. Must be called before adding frames.
    void AddExif(const uint8_t* exif, size_t exif_size) {
        if (exif == nullptr || exif_size == 0) return;
        // The Exif box starts with the (big-endian) offset of the TIFF header
        std::vector<uint8_t> box(4 + exif_size);
        memcpy(box.data() + 4, exif, exif_size);
        Check(JxlEncoderUseBoxes(ptr.get()));
        Check(JxlEncoderAddBox(ptr.get(), "Exif", box.data(), box.size(), JXL_FALSE));
        JxlEncoderCloseBoxes(ptr.get());
    }

    void WriteOutput(uint8_t** out_data, size_t* out_size) {
        JxlEncoderCloseInput(ptr.get());
        std::vector<uint8_t> output(64 * 1024);
        size_t used = 0;
        JxlEncoderStatus status;
        do {
            if (used == output.size()) output.resize(output.size() * 2);
            uint8_t* next_out = output.data() + used;
            size_t avail_out = output.size() - used;
            status = JxlEncoderProcessOutput(ptr.get(), &next_out, &avail_out);
            used = next_out - output.data();
        } while (status == JXL_ENC_NEED_MORE_OUTPUT);
        Check(status);
        CopyToOutput(output.data(), used, out_data, out_size);
    }
};

// Copies a box's contents (e.g. Exif) from the decoder, growing the buffer as needed. The contents of boxes larger
// than MAX_SIZE (which can only happen with a malformed or malicious file, as compressed boxes can expand a lot) are
// discarded. Once output has started, the decoder always needs a buffer to write to until the box is complete.
struct BoxReader {
    static constexpr size_t MAX_SIZE = 16 * 1024 * 1024;
    std::vector<uint8_t> data;
    bool active = false;
    bool discarding = false;

    void Start(JxlDecoder* dec) {
        data.resize(4096);
        active = true;
        discarding = false;
        JxlDecoderSetBoxBuffer(dec, data.data(), data.size());
    }

    void Grow(JxlDecoder* dec) {
        size_t used = data.size() - JxlDecoderReleaseBoxBuffer(dec);
        if (discarding || data.size() >= MAX_SIZE) {
            // Reuse the start of the buffer as scratch space
            discarding = true;
            data.resize(64 * 1024);
            used = 0;
        } else {
            data.resize(data.size() * 2);
        }
        JxlDecoderSetBoxBuffer(dec, data.data() + used, data.size() - used);
    }

    // Returns false if the contents were discarded.
    bool Finish(JxlDecoder* dec) {
        data.resize(data.size() - JxlDecoderReleaseBoxBuffer(dec));
        active = false;
        return !discarding;
    }
};

void Decompress(const uint8_t* data, size_t size, Buffer* pixels, JxlBasicInfo* info, uint32_t* channels,
                std::vector<uint8_t>* exif) {
    std::unique_ptr<JxlDecoder, decltype(&JxlDecoderDestroy)> decoder(JxlDecoderCreate(nullptr), JxlDecoderDestroy);
    Runner runner = CreateRunner();
    JxlDecoder* dec = decoder.get();
    Check(dec != nullptr, "Out of memory");
    Check(JxlDecoderSetParallelRunner(dec, JxlThreadParallelRunner, runner.get()) == JXL_DEC_SUCCESS &&
              JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE |
                                                 JXL_DEC_BOX | JXL_DEC_BOX_COMPLETE) == JXL_DEC_SUCCESS &&
              JxlDecoderSetDecompressBoxes(dec, JXL_TRUE) == JXL_DEC_SUCCESS &&
              JxlDecoderSetCms(dec, *JxlGetDefaultCms()) == JXL_DEC_SUCCESS &&
              JxlDecoderSetInput(dec, data, size) == JXL_DEC_SUCCESS,
          "Error initializing decoder");
    JxlDecoderCloseInput(dec);

    JxlPixelFormat format = {0, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    bool have_image = false;
    BoxReader box;
    while (true) {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec);
        if (status == JXL_DEC_BASIC_INFO) {
            Check(JxlDecoderGetBasicInfo(dec, info) == JXL_DEC_SUCCESS, "Invalid JPEG XL data");
            format.num_channels = info->num_color_channels + (info->alpha_bits > 0 ? 1 : 0);
        } else if (status == JXL_DEC_COLOR_ENCODING) {
            // Convert to sRGB (with libjxl's color management)
            JxlColorEncoding srgb;
            JxlColorEncodingSetToSRGB(&srgb, info->num_color_channels == 1);
            Check(JxlDecoderSetOutputColorProfile(dec, &srgb, nullptr, 0) == JXL_DEC_SUCCESS,
                  "Unsupported color profile");
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            if (have_image) {
                // Only the first frame of an animation is used
                Check(JxlDecoderSkipCurrentFrame(dec) == JXL_DEC_SUCCESS, "Invalid JPEG XL data");
                continue;
            }
            size_t buffer_size;
            Check(JxlDecoderImageOutBufferSize(dec, &format, &buffer_size) == JXL_DEC_SUCCESS,
                  "Invalid JPEG XL data");
            // Gray + alpha needs space to be expanded to RGBA afterwards
            Check(format.num_channels != 2 || buffer_size <= SIZE_MAX / 2, "The image is too big");
            *pixels = Allocate(format.num_channels == 2 ? buffer_size * 2 : buffer_size);
            Check(JxlDecoderSetImageOutBuffer(dec, &format, pixels->get(), buffer_size) == JXL_DEC_SUCCESS,
                  "Invalid JPEG XL data");
        } else if (status == JXL_DEC_FULL_IMAGE) {
            have_image = true;
        } else if (status == JXL_DEC_BOX) {
            JxlBoxType type;
            Check(JxlDecoderGetBoxType(dec, type, JXL_TRUE) == JXL_DEC_SUCCESS, "Invalid JPEG XL data");
            if (exif->empty() && memcmp(type, "Exif", 4) == 0) box.Start(dec);
        } else if (status == JXL_DEC_BOX_NEED_MORE_OUTPUT) {
            if (box.active) box.Grow(dec);
        } else if (status == JXL_DEC_BOX_COMPLETE) {
            if (box.active && box.Finish(dec)) {
                // Skip the offset of the TIFF header
                if (box.data.size() >= 4) {
                    size_t offset = (static_cast<size_t>(box.data[0]) << 24) | (box.data[1] << 16) |
                                    (box.data[2] << 8) | box.data[3];
                    if (offset <= box.data.size() - 4) exif->assign(box.data.begin() + 4 + offset, box.data.end());
                }
            }
        } else if (status == JXL_DEC_SUCCESS) {
            break;
        } else {
            throw Error{status == JXL_DEC_NEED_MORE_INPUT ? "Truncated JPEG XL data" : "Invalid JPEG XL data"};
        }
    }
    Check(have_image, "No image in JPEG XL data");
    *channels = format.num_channels;
}

}  // namespace

extern "C" int NAPS2_JPEG_CALL naps2_jxl_compress(
    const uint8_t* pixels, int width, int height, int pixel_format, int quality, int lossless, const uint8_t* exif,
    size_t exif_size, uint8_t** out_data, size_t* out_size, char* error, int error_size) {
    return Run(error, error_size, [&] {
        int bytes_per_pixel = BytesPerPixel(pixel_format);
        Check(pixels != nullptr && out_data != nullptr && out_size != nullptr && width > 0 && height > 0 &&
                  bytes_per_pixel > 0 && (lossless || (quality >= 1 && quality <= 100)),
              "Invalid arguments");
        Encoder encoder;
        JxlEncoder* enc = encoder.ptr.get();
        bool gray = bytes_per_pixel == 1;
        bool alpha = bytes_per_pixel == 4;
        JxlBasicInfo info;
        JxlEncoderInitBasicInfo(&info);
        info.xsize = static_cast<uint32_t>(width);
        info.ysize = static_cast<uint32_t>(height);
        info.bits_per_sample = 8;
        info.num_color_channels = gray ? 1 : 3;
        info.num_extra_channels = alpha ? 1 : 0;
        info.alpha_bits = alpha ? 8 : 0;
        // Lossless requires the original color space; lossy uses XYB internally
        info.uses_original_profile = lossless ? JXL_TRUE : JXL_FALSE;
        encoder.Check(JxlEncoderSetBasicInfo(enc, &info));
        JxlColorEncoding color_encoding;
        JxlColorEncodingSetToSRGB(&color_encoding, gray);
        encoder.Check(JxlEncoderSetColorEncoding(enc, &color_encoding));
        encoder.AddExif(exif, exif_size);
        encoder.Check(lossless ? JxlEncoderSetFrameLossless(encoder.settings, JXL_TRUE)
                               : JxlEncoderSetFrameDistance(encoder.settings, JxlEncoderDistanceFromQuality(quality)));
        JxlPixelFormat format = {static_cast<uint32_t>(bytes_per_pixel), JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
        encoder.Check(JxlEncoderAddImageFrame(encoder.settings, &format, pixels,
                                              static_cast<size_t>(width) * height * bytes_per_pixel));
        encoder.WriteOutput(out_data, out_size);
    });
}

extern "C" int NAPS2_JPEG_CALL naps2_jxl_transcode_jpeg(
    const uint8_t* jpeg_data, size_t jpeg_size, const uint8_t* exif, size_t exif_size, uint8_t** out_data,
    size_t* out_size, char* error, int error_size) {
    return Run(error, error_size, [&] {
        Check(jpeg_data != nullptr && out_data != nullptr && out_size != nullptr, "Invalid arguments");
        Encoder encoder;
        // Store the data needed to reconstruct the original JPEG file
        encoder.Check(JxlEncoderStoreJPEGMetadata(encoder.ptr.get(), JXL_TRUE));
        encoder.AddExif(exif, exif_size);
        encoder.Check(JxlEncoderAddJPEGFrame(encoder.settings, jpeg_data, jpeg_size));
        encoder.WriteOutput(out_data, out_size);
    });
}

extern "C" int NAPS2_JPEG_CALL naps2_jxl_decompress(
    const uint8_t* data, size_t size, uint8_t** out_pixels, int* out_width, int* out_height, int* out_pixel_format,
    uint8_t** out_exif, size_t* out_exif_size, char* error, int error_size) {
    return Run(error, error_size, [&] {
        Check(data != nullptr && out_pixels != nullptr && out_width != nullptr && out_height != nullptr &&
                  out_pixel_format != nullptr && out_exif != nullptr && out_exif_size != nullptr,
              "Invalid arguments");
        *out_exif = nullptr;
        *out_exif_size = 0;
        Buffer pixels(nullptr, free);
        JxlBasicInfo info;
        uint32_t channels;
        std::vector<uint8_t> exif;
        Decompress(data, size, &pixels, &info, &channels, &exif);
        if (channels == 2) {
            // Expand gray + alpha to RGBA, in place (from the end)
            uint8_t* p = pixels.get();
            for (size_t i = static_cast<size_t>(info.xsize) * info.ysize; i-- > 0;) {
                uint8_t gray = p[i * 2], alpha = p[i * 2 + 1];
                p[i * 4] = p[i * 4 + 1] = p[i * 4 + 2] = gray;
                p[i * 4 + 3] = alpha;
            }
        }
        if (!exif.empty()) {
            CopyToOutput(exif.data(), exif.size(), out_exif, out_exif_size);
        }
        *out_width = static_cast<int>(info.xsize);
        *out_height = static_cast<int>(info.ysize);
        *out_pixel_format = channels == 1 ? NAPS2_JPEG_PIXEL_GRAY8
                            : channels == 3 ? NAPS2_JPEG_PIXEL_RGB24
                                            : NAPS2_JPEG_PIXEL_RGBA32;
        *out_pixels = pixels.release();
    });
}
