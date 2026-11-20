// Internal helpers shared by the codec wrappers.

#ifndef NAPS2_UTIL_H
#define NAPS2_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <exception>
#include <memory>
#include <new>

namespace naps2 {

// Thrown to report an error; converted to a message and non-zero return value by Run.
struct Error {
    const char* message;
};

inline void Check(bool ok, const char* message) {
    if (!ok) throw Error{message};
}

// A buffer allocated with malloc, which is how output buffers are returned (and released with naps2_jpeg_free).
using Buffer = std::unique_ptr<uint8_t, decltype(&free)>;

inline Buffer Allocate(size_t size) {
    Buffer buffer(static_cast<uint8_t*>(malloc(size > 0 ? size : 1)), free);
    Check(buffer != nullptr, "Out of memory");
    return buffer;
}

// Copies data to an output buffer.
void CopyToOutput(const uint8_t* data, size_t size, uint8_t** out_data, size_t* out_size);

// The bytes per pixel of NAPS2_JPEG_PIXEL_GRAY8/RGB24/RGBA32, or 0 for any other pixel format.
int BytesPerPixel(int pixel_format);

// Writes a null-terminated message to the caller's error buffer (which may be null/0).
inline void SetError(char* error, int error_size, const char* message) {
    if (error != nullptr && error_size > 0) {
        snprintf(error, static_cast<size_t>(error_size), "%s", message);
    }
}

// Runs an operation, converting any error to a message (written to error/error_size, which may be null/0) and a
// non-zero return value.
template <typename F>
int Run(char* error, int error_size, F operation) {
    // The message is written inside each handler, as e.what() is only valid while the exception exists
    try {
        operation();
        return 0;
    } catch (const Error& e) {
        SetError(error, error_size, e.message);
    } catch (const std::bad_alloc&) {
        SetError(error, error_size, "Out of memory");
    } catch (const std::exception& e) {
        SetError(error, error_size, e.what());
    } catch (...) {
        SetError(error, error_size, "Unknown error");
    }
    return 1;
}

}  // namespace naps2

#endif // NAPS2_UTIL_H
