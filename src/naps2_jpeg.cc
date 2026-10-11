#include "naps2_jpeg.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exception>
#include <new>

#include "lib/jpegli/encode.h"

namespace {

// Everything that's modified between setjmp and longjmp lives on the heap (rather than in automatic variables) so
// its value is well-defined after an error.
struct CompressContext {
    jpeg_compress_struct cinfo;
    jpeg_error_mgr jerr;
    jmp_buf env;
    char message[JMSG_LENGTH_MAX];
    unsigned char* output;
    unsigned long output_size;  // NOLINT (matches jpegli_mem_dest)
    bool created;
};

// jpegli messages look like "<source path>:<line>: <text>". Reformat as "<text> (<file name>:<line>)".
void FormatJpegliMessage(const char* raw, char* out, size_t out_size) {
    for (const char* c = raw; *c; ++c) {
        if (*c != ':' || c[1] < '0' || c[1] > '9') continue;
        const char* d = c + 1;
        while (*d >= '0' && *d <= '9') ++d;
        if (d[0] != ':' || d[1] != ' ') continue;
        const char* file = raw;
        for (const char* s = raw; s < c; ++s) {
            if (*s == '/' || *s == '\\') file = s + 1;
        }
        snprintf(out, out_size, "%s (%.*s)", d + 2, static_cast<int>(d - file), file);
        return;
    }
    snprintf(out, out_size, "%s", raw);
}

void ErrorExit(j_common_ptr cinfo) {
    auto* ctx = static_cast<CompressContext*>(cinfo->client_data);
    char raw[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, raw);
    FormatJpegliMessage(raw, ctx->message, sizeof(ctx->message));
    longjmp(ctx->env, 1);
}

void OutputMessage(j_common_ptr) {
    // Suppress warnings (jpegli would otherwise print them to stderr).
}

void SetError(char* error, int error_size, const char* message) {
    if (error != nullptr && error_size > 0) {
        snprintf(error, static_cast<size_t>(error_size), "%s", message);
    }
}

int Fail(CompressContext* ctx, char* error, int error_size, const char* message) {
    SetError(error, error_size, message);
    if (ctx->created) {
        jpegli_destroy_compress(&ctx->cinfo);
    }
    free(ctx->output);
    delete ctx;
    return 1;
}

UINT16 ToDensity(double dpi) {
    return static_cast<UINT16>(dpi >= 65535 ? 65535 : (dpi < 1 ? 1 : dpi + 0.5));
}

// Performs the compression. jpegli errors longjmp out of this function back to naps2_jpeg_compress.
void Compress(CompressContext* ctx, const uint8_t* pixels, int width, int height, int stride,
              J_COLOR_SPACE color_space, int components, int quality, double x_dpi, double y_dpi) {
    jpeg_compress_struct* cinfo = &ctx->cinfo;
    jpegli_create_compress(cinfo);
    ctx->created = true;
    jpegli_mem_dest(cinfo, &ctx->output, &ctx->output_size);

    cinfo->image_width = static_cast<JDIMENSION>(width);
    cinfo->image_height = static_cast<JDIMENSION>(height);
    cinfo->input_components = components;
    cinfo->in_color_space = color_space;
    jpegli_set_defaults(cinfo);
    if (cinfo->num_components == 3) {
        // 4:2:0 chroma subsampling (jpegli's default is no subsampling)
        cinfo->comp_info[0].h_samp_factor = 2;
        cinfo->comp_info[0].v_samp_factor = 2;
        for (int i = 1; i < cinfo->num_components; ++i) {
            cinfo->comp_info[i].h_samp_factor = 1;
            cinfo->comp_info[i].v_samp_factor = 1;
        }
    }
    jpegli_enable_adaptive_quantization(cinfo, TRUE);
    jpegli_set_distance(cinfo, jpegli_quality_to_distance(quality), TRUE);
    // Baseline (sequential) rather than progressive, as progressive decodes ~3x slower
    jpegli_set_progressive_level(cinfo, 0);
    cinfo->optimize_coding = TRUE;
    // Unlike libjpeg, jpegli doesn't write a JFIF header by default. It's the standard place for the resolution.
    cinfo->write_JFIF_header = TRUE;
    if (x_dpi > 0 && y_dpi > 0) {
        cinfo->density_unit = 1;  // dots per inch
        cinfo->X_density = ToDensity(x_dpi);
        cinfo->Y_density = ToDensity(y_dpi);
    }
    jpegli_set_input_format(cinfo, JPEGLI_TYPE_UINT8, JPEGLI_NATIVE_ENDIAN);

    jpegli_start_compress(cinfo, TRUE);
    while (cinfo->next_scanline < cinfo->image_height) {
        JSAMPROW row = const_cast<JSAMPROW>(pixels + static_cast<size_t>(cinfo->next_scanline) * stride);
        jpegli_write_scanlines(cinfo, &row, 1);
    }
    jpegli_finish_compress(cinfo);
    jpegli_destroy_compress(cinfo);
    ctx->created = false;
}

bool GetInputFormat(int pixel_format, J_COLOR_SPACE* color_space, int* components) {
    switch (pixel_format) {
        case NAPS2_JPEG_PIXEL_RGB24: *color_space = JCS_RGB; *components = 3; return true;
        case NAPS2_JPEG_PIXEL_BGR24: *color_space = JCS_EXT_BGR; *components = 3; return true;
        case NAPS2_JPEG_PIXEL_RGBA32: *color_space = JCS_EXT_RGBA; *components = 4; return true;
        case NAPS2_JPEG_PIXEL_BGRA32: *color_space = JCS_EXT_BGRA; *components = 4; return true;
        case NAPS2_JPEG_PIXEL_GRAY8: *color_space = JCS_GRAYSCALE; *components = 1; return true;
        default: return false;
    }
}

}  // namespace

extern "C" int NAPS2_JPEG_CALL naps2_jpeg_compress(
    const uint8_t* pixels, int width, int height, int stride, int pixel_format, int quality,
    double x_dpi, double y_dpi, uint8_t** out_data, size_t* out_size, char* error, int error_size) {
    if (out_data == nullptr || out_size == nullptr) {
        SetError(error, error_size, "Output pointers must not be null");
        return 1;
    }
    *out_data = nullptr;
    *out_size = 0;
    J_COLOR_SPACE color_space;
    int components;
    if (!GetInputFormat(pixel_format, &color_space, &components)) {
        SetError(error, error_size, "Unsupported pixel format");
        return 1;
    }
    if (pixels == nullptr || width <= 0 || height <= 0 || stride < width * components) {
        SetError(error, error_size, "Invalid image dimensions or stride");
        return 1;
    }
    if (quality < 1 || quality > 100) {
        SetError(error, error_size, "Quality must be between 1 and 100");
        return 1;
    }

    auto* ctx = new (std::nothrow) CompressContext();
    if (ctx == nullptr) {
        SetError(error, error_size, "Out of memory");
        return 1;
    }
    ctx->cinfo.err = jpegli_std_error(&ctx->jerr);
    ctx->jerr.error_exit = ErrorExit;
    ctx->jerr.output_message = OutputMessage;
    ctx->cinfo.client_data = ctx;

    if (setjmp(ctx->env)) {
        return Fail(ctx, error, error_size, ctx->message);
    }
    // jpegli reports most errors (including failed allocations in its memory manager) via error_exit, but some
    // internal allocations use C++ containers, which throw instead.
    try {
        Compress(ctx, pixels, width, height, stride, color_space, components, quality, x_dpi, y_dpi);
    } catch (const std::bad_alloc&) {
        return Fail(ctx, error, error_size, "Out of memory");
    } catch (const std::exception& e) {
        return Fail(ctx, error, error_size, e.what());
    } catch (...) {
        return Fail(ctx, error, error_size, "Unknown error");
    }

    *out_data = ctx->output;
    *out_size = ctx->output_size;
    delete ctx;
    return 0;
}

extern "C" void NAPS2_JPEG_CALL naps2_jpeg_free(uint8_t* data) {
    free(data);
}
