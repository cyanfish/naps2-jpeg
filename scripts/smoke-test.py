"""Smoke test for a built naps2_jpeg library (standard library only, so it can run on any build machine).

Usage: python3 smoke-test.py <path to naps2_jpeg library> [output dir]
"""
import ctypes as C
import random
import struct
import sys

RGB24, BGR24, RGBA32, BGRA32, GRAY8 = range(5)


def load(path):
    lib = C.CDLL(path)
    lib.naps2_jpeg_compress.restype = C.c_int
    lib.naps2_jpeg_compress.argtypes = [
        C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_double, C.c_double,
        C.POINTER(C.POINTER(C.c_uint8)), C.POINTER(C.c_size_t), C.c_char_p, C.c_int]
    lib.naps2_jpeg_free.restype = None
    lib.naps2_jpeg_free.argtypes = [C.POINTER(C.c_uint8)]
    if not hasattr(lib, "naps2_webp_compress"):
        # A jpegli-only build (NAPS2_JPEG_ONLY)
        return lib
    out_args = [C.POINTER(C.POINTER(C.c_uint8)), C.POINTER(C.c_size_t), C.c_char_p, C.c_int]
    for name in ("naps2_webp_compress", "naps2_jxl_compress"):
        f = getattr(lib, name)
        f.restype = C.c_int
        f.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_char_p, C.c_size_t] + out_args
    lib.naps2_jxl_transcode_jpeg.restype = C.c_int
    lib.naps2_jxl_transcode_jpeg.argtypes = [C.c_char_p, C.c_size_t, C.c_char_p, C.c_size_t] + out_args
    for name in ("naps2_webp_decompress", "naps2_jxl_decompress"):
        f = getattr(lib, name)
        f.restype = C.c_int
        f.argtypes = [C.c_char_p, C.c_size_t, C.POINTER(C.POINTER(C.c_uint8))] + [C.POINTER(C.c_int)] * 3 + [
            C.POINTER(C.POINTER(C.c_uint8)), C.POINTER(C.c_size_t), C.c_char_p, C.c_int]
    return lib


def take_output(lib, out, size):
    data = C.string_at(out, size)
    lib.naps2_jpeg_free(out)
    return data


def encode(lib, name, pixels, w, h, fmt, quality=75, lossless=0, exif=None):
    out = C.POINTER(C.c_uint8)()
    size = C.c_size_t()
    err = C.create_string_buffer(200)
    rc = getattr(lib, name)(pixels, w, h, fmt, quality, lossless, exif, len(exif or b""), C.byref(out),
                            C.byref(size), err, len(err))
    if rc != 0:
        return None, err.value.decode()
    return take_output(lib, out, size.value), None


def transcode(lib, jpeg, exif=None):
    out = C.POINTER(C.c_uint8)()
    size = C.c_size_t()
    err = C.create_string_buffer(200)
    rc = lib.naps2_jxl_transcode_jpeg(jpeg, len(jpeg), exif, len(exif or b""), C.byref(out), C.byref(size), err,
                                      len(err))
    if rc != 0:
        return None, err.value.decode()
    return take_output(lib, out, size.value), None


def decode(lib, name, data):
    """Returns ((pixels, width, height, pixel format, exif), None) or (None, error)."""
    out = C.POINTER(C.c_uint8)()
    w, h, fmt = C.c_int(), C.c_int(), C.c_int()
    exif = C.POINTER(C.c_uint8)()
    exif_size = C.c_size_t()
    err = C.create_string_buffer(200)
    rc = getattr(lib, name)(data, len(data), C.byref(out), C.byref(w), C.byref(h), C.byref(fmt), C.byref(exif),
                            C.byref(exif_size), err, len(err))
    if rc != 0:
        return None, err.value.decode()
    bpp = {RGB24: 3, RGBA32: 4, GRAY8: 1}[fmt.value]
    pixels = take_output(lib, out, w.value * h.value * bpp)
    exif_data = take_output(lib, exif, exif_size.value) if exif else None
    return (pixels, w.value, h.value, fmt.value, exif_data), None


def mean_abs_diff(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def compress(lib, pixels, w, h, stride, fmt, quality=75, dpi=(300.0, 300.0)):
    out = C.POINTER(C.c_uint8)()
    size = C.c_size_t()
    err = C.create_string_buffer(200)
    rc = lib.naps2_jpeg_compress(pixels, w, h, stride, fmt, quality, dpi[0], dpi[1],
                                 C.byref(out), C.byref(size), err, len(err))
    if rc != 0:
        return None, err.value.decode()
    data = C.string_at(out, size.value)
    lib.naps2_jpeg_free(out)
    return data, None


def markers(data):
    """Returns {marker: payload} for the header segments (up to SOS)."""
    assert data[:2] == b"\xff\xd8", "missing SOI"
    assert data[-2:] == b"\xff\xd9", "missing EOI"
    result, i = {}, 2
    while True:
        assert data[i] == 0xFF
        m = data[i + 1]
        length = struct.unpack(">H", data[i + 2:i + 4])[0]
        result.setdefault(m, data[i + 4:i + 2 + length])
        if m == 0xDA:
            return result
        i += 2 + length


def main():
    lib = load(sys.argv[1])
    random.seed(1)
    w, h = 403, 301  # odd dimensions exercise edge padding
    rgb = bytearray()
    for y in range(h):
        for x in range(w):
            n = random.randint(-20, 20)
            rgb += bytes(max(0, min(255, v + n)) for v in (x * 255 // w, y * 255 // h, (x + y) % 256))
    rgb = bytes(rgb)
    bgr = bytes(b for i in range(0, len(rgb), 3) for b in (rgb[i + 2], rgb[i + 1], rgb[i]))
    rgba = bytes(b for i in range(0, len(rgb), 3) for b in (rgb[i], rgb[i + 1], rgb[i + 2], 0x7F))
    bgra = bytes(b for i in range(0, len(rgb), 3) for b in (rgb[i + 2], rgb[i + 1], rgb[i], 0x7F))
    gray = bytes(rgb[i + 1] for i in range(0, len(rgb), 3))
    # Padded stride (rows not tightly packed)
    padded = b"".join(rgb[y * w * 3:(y + 1) * w * 3] + b"\0" * 5 for y in range(h))

    base, err = compress(lib, rgb, w, h, w * 3, RGB24)
    assert base is not None, err
    m = markers(base)
    assert 0xC0 in m and 0xC2 not in m, "expected baseline (SOF0), not progressive"
    sof = m[0xC0]
    assert struct.unpack(">HH", sof[1:5]) == (h, w), "wrong dimensions"
    assert sof[5] == 3 and sof[7] == 0x22 and sof[10] == 0x11 and sof[13] == 0x11, "expected 3 components, 4:2:0"
    jfif = m[0xE0]
    assert jfif[:5] == b"JFIF\0" and jfif[7] == 1 and struct.unpack(">HH", jfif[8:12]) == (300, 300), "wrong density"

    for name, buf, stride, fmt in (("BGR24", bgr, w * 3, BGR24), ("RGBA32", rgba, w * 4, RGBA32),
                                   ("BGRA32", bgra, w * 4, BGRA32), ("padded RGB24", padded, w * 3 + 5, RGB24)):
        data, err = compress(lib, buf, w, h, stride, fmt)
        assert data == base, f"{name} output differs from RGB24 ({err})"

    g, err = compress(lib, gray, w, h, w, GRAY8)
    assert g is not None, err
    assert markers(g)[0xC0][5] == 1, "expected 1 component for GRAY8"

    nodpi, err = compress(lib, rgb, w, h, w * 3, RGB24, dpi=(0, 0))
    assert markers(nodpi)[0xE0][7] == 0, "expected no density unit without dpi"

    # Validation errors (checked by the wrapper)
    for args, label in (((rgb, w, h, w * 3, 99), "pixel format"), ((rgb, w, h, w * 3, RGB24, 0), "quality"),
                        ((rgb, 0, h, w * 3, RGB24), "dimensions"), ((rgb, w, h, w, RGB24), "stride")):
        data, err = compress(lib, *args)
        assert data is None and err, f"expected {label} error"

    # Errors raised inside jpegli (exercises the setjmp/longjmp path): JPEG can't exceed 65500 pixels per side
    big_w = 70000
    data, err = compress(lib, bytes(big_w * 3), big_w, 1, big_w * 3, RGB24)
    assert data is None and err, "expected jpegli error for oversized image"
    # Messages are reformatted as "<text> (<file>:<line>)", without the build path and without truncation
    assert err.startswith("Input image too big") and err.endswith(")") and "/" not in err and "\\" not in err, \
        f"unexpected error message format: {err!r}"
    jpegli_error = err
    # The library must still work afterwards
    again, err = compress(lib, rgb, w, h, w * 3, RGB24)
    assert again == base, "output changed after an error"

    if not hasattr(lib, "naps2_webp_compress"):
        print(f"OK (JPEG only): {len(base)} bytes (q75 JPEG), {len(g)} bytes (gray JPEG)")
        return
    rgba_alpha = bytes(b for i in range(0, len(rgb), 3) for b in (rgb[i], rgb[i + 1], rgb[i + 2], (i // 3) % 256))
    # Any payload is passed through as-is (it's built/parsed by NAPS2)
    exif = b"II*\0" + bytes(range(60))
    sizes = {"q75 JPEG": len(base), "gray JPEG": len(g)}

    for codec in ("webp", "jxl"):
        enc, dec = f"naps2_{codec}_compress", f"naps2_{codec}_decompress"
        # Lossless round trips are exact
        cases = [("RGB24", rgb, RGB24), ("RGBA32", rgba_alpha, RGBA32)] + ([("GRAY8", gray, GRAY8)] if codec == "jxl" else [])
        for name, buf, fmt in cases:
            data, err = encode(lib, enc, buf, w, h, fmt, lossless=1, exif=exif)
            assert data is not None, err
            result, err = decode(lib, dec, data)
            assert result == (buf, w, h, fmt, exif), f"{codec}: lossless {name} round trip differs ({err})"
            if fmt == RGB24:
                sizes[f"lossless {codec}"] = len(data)

        lossy, err = encode(lib, enc, rgb, w, h, RGB24, quality=75)
        assert lossy is not None, err
        result = decode(lib, dec, lossy)[0]
        assert result[1:] == (w, h, RGB24, None), f"{codec}: lossy info differs"
        assert mean_abs_diff(result[0], rgb) < 12, f"{codec}: lossy output too different"
        sizes[f"q75 {codec}"] = len(lossy)

        for args, label in (((rgb, w, h, 99), "pixel format"), ((rgb, 0, h, RGB24), "dimensions")):
            data, err = encode(lib, enc, *args)
            assert data is None and err, f"{codec}: expected {label} error"
        data, err = decode(lib, dec, b"not an image")
        assert data is None and err, f"{codec}: expected decode error"
        data, err = decode(lib, dec, lossy[:len(lossy) // 2])
        assert data is None and err, f"{codec}: expected truncated data error"

    # WebP can't exceed 16383 pixels per side, and has no grayscale mode
    data, err = encode(lib, "naps2_webp_compress", bytes(16384 * 3), 16384, 1, RGB24)
    assert data is None and "16383" in err, "expected webp size error"
    data, err = encode(lib, "naps2_webp_compress", gray, w, h, GRAY8)
    assert data is None and err, "expected webp gray error"

    # Lossless JPEG recompression
    transcoded, err = transcode(lib, base, exif)
    assert transcoded is not None, err
    assert len(transcoded) < len(base), "expected the recompressed JPEG to be smaller"
    result = decode(lib, "naps2_jxl_decompress", transcoded)[0]
    assert result[1:] == (w, h, RGB24, exif), "wrong transcoded jxl info"
    assert mean_abs_diff(result[0], rgb) < 12, "transcoded output too different"
    sizes["transcoded JPEG"] = len(transcoded)
    data, err = transcode(lib, b"not a jpeg")
    assert data is None and err, "expected transcode error"

    if len(sys.argv) > 2:
        for name, data in (("jpegli.jpg", base), ("transcoded.jxl", transcoded)):
            with open(f"{sys.argv[2]}/{name}", "wb") as f:
                f.write(data)
    print("OK: " + ", ".join(f"{v} bytes ({k})" for k, v in sizes.items()))
    print(f"jpegli error message: {jpegli_error!r}")


main()
