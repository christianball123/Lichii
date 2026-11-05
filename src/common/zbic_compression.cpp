// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <string>

#include <fmt/format.h>

#include "common/logging.h"
#include "common/zbic_compression.h"

#define ZSTD_ZBIC_SUPPORT 1
#define ZSTDLIB_VISIBLE static
#define ZSTDLIB_HIDDEN static
#define ZSTDERRORLIB_VISIBLE static
#define ZSTDERRORLIB_HIDDEN static
#undef ZSTD_MULTITHREAD

#if defined(__ANDROID__)
#undef _GNU_SOURCE
#endif

#include "zstd.h"
#define g_ZSTD_threading_useless_symbol g_ZSTD_zbic_threading_useless_symbol
#include "zstd.c"
#undef g_ZSTD_threading_useless_symbol

namespace Common::Compression {

bool IsZBIC(std::span<const u8> src) {
    if (src.size() < sizeof(u32)) {
        return false;
    }
    u32 magic = 0;
    std::memcpy(&magic, src.data(), sizeof(u32));
    return magic == ZSTD_MAGICNUMBER; // 0x4349425A ("ZBIC")
}

namespace {

std::string HexBytes(std::span<const u8> bytes) {
    std::string out;
    for (const u8 byte : bytes) {
        out += fmt::format("{:02x}", byte);
    }
    return out;
}

void LogFailure(std::span<u8> dst, std::span<const u8> src, size_t res) {
    const unsigned long long content_size = ZSTD_getFrameContentSize(src.data(), src.size());
    const size_t frame_size = ZSTD_findFrameCompressedSize(src.data(), src.size());
    const size_t edge = (std::min)(src.size(), size_t{16});
    LOG_ERROR(Common, "ZBIC: decompression failed ({}): {} -> {} bytes, frame content size {}, "
                      "first frame {} bytes ({}), head {}, tail {}",
              ZSTD_getErrorName(res), src.size(), dst.size(),
              content_size == ZSTD_CONTENTSIZE_UNKNOWN ? std::string{"unknown"}
              : content_size == ZSTD_CONTENTSIZE_ERROR ? std::string{"error"}
                                                       : std::to_string(content_size),
              ZSTD_isError(frame_size) ? 0 : frame_size,
              ZSTD_isError(frame_size) ? ZSTD_getErrorName(frame_size) : "ok",
              HexBytes(src.first(edge)), HexBytes(src.last(edge)));
}

} // Anonymous namespace

int DecompressDataZBIC(std::span<u8> dst, std::span<const u8> src) {
    if (dst.empty() || src.empty()) {
        return -1;
    }
    const size_t res = ZSTD_decompress(dst.data(), dst.size(), src.data(), src.size());
    if (!ZSTD_isError(res)) {
        return static_cast<int>(res);
    }
    LogFailure(dst, src, res);
    // Data after the first frame (padding) fails the whole call: decode the first frame alone,
    // and accept it only when it fills the segment exactly.
    const size_t frame_size = ZSTD_findFrameCompressedSize(src.data(), src.size());
    if (!ZSTD_isError(frame_size) && frame_size < src.size()) {
        const size_t first = ZSTD_decompress(dst.data(), dst.size(), src.data(), frame_size);
        if (!ZSTD_isError(first) && first == dst.size()) {
            LOG_WARNING(Common, "ZBIC: decoded the first frame alone ({} of {} bytes)", frame_size,
                        src.size());
            return static_cast<int>(first);
        }
        LOG_ERROR(Common, "ZBIC: first frame alone failed too ({})",
                  ZSTD_isError(first) ? ZSTD_getErrorName(first) : "size mismatch");
    }
    return -1;
}

} // namespace Common::Compression
