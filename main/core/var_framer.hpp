// ARMOR-RADAR - a resynchronising framer for variable-length frames that carry their own length.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The radars of the Hi-Link family (LD2461, LD2410, LD2412, LD2410S) and the MR24 module report frames whose length is inside the frame:
// a header, a length field, `length` bytes of data, and then some bytes that the length does not count (a checksum, an end marker). The
// framer finds a frame by its header, reads the length, waits for the whole frame, checks the end marker and hands the frame over; on any
// mismatch it drops one byte and searches again, so a bad byte costs one frame and never the stream. What is inside the frame is the
// decoder's business (checksums included). FrameFramer (frame_framer.hpp) does the same for the LD2450's fixed 30-byte frame.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace armor {

struct VarFrameSpec {
  std::array<std::uint8_t, 4> header{};
  std::size_t header_length = 0;
  std::size_t length_offset = 0;      // where the length field starts, counted from the first byte of the frame
  std::size_t length_bytes = 2;       // 1 or 2
  bool length_big_endian = false;
  std::size_t length_extra = 0;       // bytes after the counted data that the length does not include (checksum, end marker)
  std::array<std::uint8_t, 4> footer{};
  std::size_t footer_length = 0;
  std::size_t max_frame = 128;

  constexpr bool configured() const {
    return header_length > 0 && header_length <= 4 && footer_length <= 4 && (length_bytes == 1 || length_bytes == 2) && length_offset >= header_length &&
           max_frame >= length_offset + length_bytes + length_extra && max_frame <= 256 && length_extra >= footer_length;
  }
};

struct VarFramerStats {
  std::uint32_t frames = 0;
  std::uint32_t dropped_bytes = 0;
  std::uint32_t bad_frames = 0;   // a header and a plausible length were found but the end marker did not match
};

class VarFramer {
 public:
  using FrameHandler = std::function<void(const std::uint8_t* frame, std::size_t length)>;

  explicit VarFramer(const VarFrameSpec& spec) : spec_(spec) {}
  const VarFramerStats& stats() const { return stats_; }

  void feed(const std::uint8_t* data, std::size_t length, const FrameHandler& on_frame) {
    if (!spec_.configured()) return;
    for (std::size_t i = 0; i < length; ++i) {
      buffer_[filled_++] = data[i];
      drain(on_frame);
    }
  }

  void reset() { filled_ = 0; }

 private:
  static constexpr std::size_t kCapacity = 256;

  void drop_one() {
    for (std::size_t i = 1; i < filled_; ++i) buffer_[i - 1] = buffer_[i];
    --filled_;
    ++stats_.dropped_bytes;
  }

  void drain(const FrameHandler& on_frame) {
    for (;;) {
      // the header must match as far as the bytes that have arrived
      const std::size_t comparable = filled_ < spec_.header_length ? filled_ : spec_.header_length;
      bool matches = true;
      for (std::size_t i = 0; i < comparable; ++i) if (buffer_[i] != spec_.header[i]) { matches = false; break; }
      if (!matches) { drop_one(); if (filled_ == 0) return; continue; }
      const std::size_t length_end = spec_.length_offset + spec_.length_bytes;
      if (filled_ < length_end) return;
      std::size_t counted = buffer_[spec_.length_offset];
      if (spec_.length_bytes == 2) {
        const std::size_t second = buffer_[spec_.length_offset + 1];
        counted = spec_.length_big_endian ? (counted << 8) | second : counted | (second << 8);
      }
      const std::size_t total = length_end + counted + spec_.length_extra;
      if (total > spec_.max_frame || total > kCapacity) { ++stats_.bad_frames; drop_one(); if (filled_ == 0) return; continue; }
      if (filled_ < total) return;
      bool footer_ok = true;
      for (std::size_t i = 0; i < spec_.footer_length; ++i) if (buffer_[total - spec_.footer_length + i] != spec_.footer[i]) { footer_ok = false; break; }
      if (!footer_ok) { ++stats_.bad_frames; drop_one(); if (filled_ == 0) return; continue; }
      ++stats_.frames;
      on_frame(buffer_.data(), total);
      for (std::size_t i = total; i < filled_; ++i) buffer_[i - total] = buffer_[i];
      filled_ -= total;
      if (filled_ == 0) return;
    }
  }

  VarFrameSpec spec_;
  std::array<std::uint8_t, kCapacity> buffer_{};
  std::size_t filled_ = 0;
  VarFramerStats stats_;
};

}  // namespace armor
