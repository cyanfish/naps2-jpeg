#include <string.h>

#include "naps2_jpeg.h"
#include "naps2_util.h"
#include "webp/decode.h"
#include "webp/demux.h"
#include "webp/encode.h"
#include "webp/mux.h"

using namespace naps2;

namespace {

struct MemoryWriter : WebPMemoryWriter {
    MemoryWriter() { WebPMemoryWriterInit(this); }
    ~MemoryWriter() { WebPMemoryWriterClear(this); }
};

void Encode(const uint8_t* pixels, int width, int height, int bytes_per_pixel, int quality, bool lossless,
            MemoryWriter* writer) {
    WebPConfig config;
    WebPPicture picture;
    Check(WebPConfigInit(&config) && WebPPictureInit(&picture), "Version mismatch");
    config.quality = static_cast<float>(quality);
    config.lossless = lossless;
    // By default, lossless encoding changes the color of fully transparent pixels
    config.exact = lossless;
    config.thread_level = 1;
    // Lossless encoding works on ARGB; lossy on YUV
    picture.use_argb = lossless;
    picture.width = width;
    picture.height = height;
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = writer;
    bool ok = bytes_per_pixel == 4 ? WebPPictureImportRGBA(&picture, pixels, width * 4)
                                   : WebPPictureImportRGB(&picture, pixels, width * 3);
    ok = ok && WebPEncode(&config, &picture);
    WebPEncodingError error = picture.error_code;
    WebPPictureFree(&picture);
    Check(ok, error == VP8_ENC_ERROR_OUT_OF_MEMORY || error == VP8_ENC_ERROR_BITSTREAM_OUT_OF_MEMORY
                  ? "Out of memory"
                  : "Error encoding WebP");
}

void AddExif(const MemoryWriter& writer, const uint8_t* exif, size_t exif_size, uint8_t** out_data,
             size_t* out_size) {
    std::unique_ptr<WebPMux, decltype(&WebPMuxDelete)> mux(WebPMuxNew(), WebPMuxDelete);
    Check(mux != nullptr, "Out of memory");
    WebPData image = {writer.mem, writer.size};
    WebPData exif_data = {exif, exif_size};
    WebPData assembled;
    WebPDataInit(&assembled);
    bool ok = WebPMuxSetImage(mux.get(), &image, 0) == WEBP_MUX_OK &&
              WebPMuxSetChunk(mux.get(), "EXIF", &exif_data, 0) == WEBP_MUX_OK &&
              WebPMuxAssemble(mux.get(), &assembled) == WEBP_MUX_OK;
    if (ok) {
        CopyToOutput(assembled.bytes, assembled.size, out_data, out_size);
    }
    WebPDataClear(&assembled);
    Check(ok, "Error adding Exif metadata");
}

void DecodeFirstAnimationFrame(const WebPData& data, uint8_t* pixels, size_t pixels_size) {
    WebPAnimDecoderOptions options;
    Check(WebPAnimDecoderOptionsInit(&options), "Version mismatch");
    options.color_mode = MODE_RGBA;
    std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(
        WebPAnimDecoderNew(&data, &options), WebPAnimDecoderDelete);
    uint8_t* frame;
    int timestamp;
    Check(decoder != nullptr && WebPAnimDecoderGetNext(decoder.get(), &frame, &timestamp),
          "Error decoding WebP data");
    memcpy(pixels, frame, pixels_size);
}

// Gets the Exif metadata, and converts the pixels to sRGB if the image has an embedded color profile (which libwebp
// doesn't apply itself).
void ApplyMetadata(const WebPData& data, uint8_t* pixels, int width, int height, bool rgba, uint8_t** out_exif,
                   size_t* out_exif_size) {
    std::unique_ptr<WebPDemuxer, decltype(&WebPDemuxDelete)> demux(WebPDemux(&data), WebPDemuxDelete);
    if (demux == nullptr) return;
    WebPChunkIterator iter;
    if (WebPDemuxGetChunk(demux.get(), "EXIF", 1, &iter)) {
        CopyToOutput(iter.chunk.bytes, iter.chunk.size, out_exif, out_exif_size);
        WebPDemuxReleaseChunkIterator(&iter);
    }
    if (WebPDemuxGetChunk(demux.get(), "ICCP", 1, &iter)) {
        naps2_icc_to_srgb(pixels, width, height, width * (rgba ? 4 : 3),
                          rgba ? NAPS2_JPEG_PIXEL_RGBA32 : NAPS2_JPEG_PIXEL_RGB24, iter.chunk.bytes, iter.chunk.size,
                          nullptr, 0);
        WebPDemuxReleaseChunkIterator(&iter);
    }
}

}  // namespace

extern "C" int NAPS2_JPEG_CALL naps2_webp_compress(
    const uint8_t* pixels, int width, int height, int pixel_format, int quality, int lossless, const uint8_t* exif,
    size_t exif_size, uint8_t** out_data, size_t* out_size, char* error, int error_size) {
    return Run(error, error_size, [&] {
        int bytes_per_pixel = BytesPerPixel(pixel_format);
        Check(pixels != nullptr && out_data != nullptr && out_size != nullptr && width > 0 && height > 0 &&
                  bytes_per_pixel >= 3 && quality >= 0 && quality <= 100,
              "Invalid arguments");
        Check(width <= WEBP_MAX_DIMENSION && height <= WEBP_MAX_DIMENSION,
              "The image is too big for WebP (the maximum is 16383x16383 pixels)");
        MemoryWriter writer;
        Encode(pixels, width, height, bytes_per_pixel, quality, lossless != 0, &writer);
        if (exif != nullptr && exif_size > 0) {
            AddExif(writer, exif, exif_size, out_data, out_size);
        } else {
            CopyToOutput(writer.mem, writer.size, out_data, out_size);
        }
    });
}

extern "C" int NAPS2_JPEG_CALL naps2_webp_decompress(
    const uint8_t* data, size_t size, uint8_t** out_pixels, int* out_width, int* out_height, int* out_pixel_format,
    uint8_t** out_exif, size_t* out_exif_size, char* error, int error_size) {
    return Run(error, error_size, [&] {
        Check(data != nullptr && out_pixels != nullptr && out_width != nullptr && out_height != nullptr &&
                  out_pixel_format != nullptr && out_exif != nullptr && out_exif_size != nullptr,
              "Invalid arguments");
        *out_exif = nullptr;
        *out_exif_size = 0;
        WebPBitstreamFeatures features;
        Check(WebPGetFeatures(data, size, &features) == VP8_STATUS_OK, "Invalid WebP data");
        // Animated frames are composited onto an RGBA canvas
        bool rgba = features.has_alpha || features.has_animation;
        int stride = features.width * (rgba ? 4 : 3);
        size_t pixels_size = static_cast<size_t>(stride) * features.height;
        Buffer pixels = Allocate(pixels_size);
        WebPData webp_data = {data, size};
        if (features.has_animation) {
            DecodeFirstAnimationFrame(webp_data, pixels.get(), pixels_size);
        } else {
            auto decode = rgba ? WebPDecodeRGBAInto : WebPDecodeRGBInto;
            Check(decode(data, size, pixels.get(), pixels_size, stride) != nullptr, "Error decoding WebP data");
        }
        ApplyMetadata(webp_data, pixels.get(), features.width, features.height, rgba, out_exif, out_exif_size);
        *out_width = features.width;
        *out_height = features.height;
        *out_pixel_format = rgba ? NAPS2_JPEG_PIXEL_RGBA32 : NAPS2_JPEG_PIXEL_RGB24;
        *out_pixels = pixels.release();
    });
}
