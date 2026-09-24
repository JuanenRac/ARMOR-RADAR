// ARMOR-RADAR - a resynchronising byte-stream framer for fixed-length radar frames.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A UART delivers bytes, not frames: it starts mid-frame, loses bytes and picks
// up noise. The framer finds a frame by its header, checks its length and tail,
// and on any mismatch drops one byte and searches again, so one bad byte costs
// one frame and never the whole stream.
//
// The framer knows nothing about what is inside a frame. The header, tail and
// length come from a ProtocolSpec, and the real LD2450/LD2461 values must be
// entered only from the vendor protocol document (see docs/HARDWARE_BOUNDARY.md),
// never from memory. Until then the spec is "unconfigured" and the framer emits
// nothing, so no radar data can be invented.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace armor {

constexpr std::size_t kMaxFrameLength = 64;
constexpr std::size_t kMaxMarkerLength = 4;

struct ProtocolSpec {
  std::array<std::uint8_t, kMaxMarkerLength> header{};
  std::size_t header_length = 0;
  std::array<std::uint8_t, kMaxMarkerLength> tail{};
  std::size_t tail_length = 0;
  std::size_t frame_length = 0;  // header + payload + tail

  constexpr bool configured() const {
    return header_length > 0 && header_length <= kMaxMarkerLength && tail_length <= kMaxMarkerLength &&
           frame_length > header_length + tail_length && frame_length <= kMaxFrameLength;
  }
  static constexpr ProtocolSpec unconfigured() { return ProtocolSpec{}; }
};

struct FramerStats {
  std::uint32_t frames = 0;
  std::uint32_t dropped_bytes = 0;  // bytes discarded while searching for a header
  std::uint32_t bad_frames = 0;     // a header was found but the tail did not match
};

class FrameFramer {
 public:
  using FrameHandler = std::function<void(const std::uint8_t* frame, std::size_t length)>;

  explicit FrameFramer(const ProtocolSpec& spec) : spec_(spec) {}

  bool configured() const { return spec_.configured(); }
  const FramerStats& stats() const { return stats_; }

  // Feed received bytes. `on_frame` is called once per complete, tail-checked frame.
  void feed(const std::uint8_t* data, std::size_t length, const FrameHandler& on_frame) {
    if (!spec_.configured()) return;
    for (std::size_t i = 0; i < length; ++i) {
      buffer_[filled_++] = data[i];
      drain(on_frame);
    }
  }

 private:
  bool starts_with_header() const {
    for (std::size_t i = 0; i < spec_.header_length; ++i) {
      if (buffer_[i] != spec_.header[i]) return false;
    }
    return true;
  }
  bool header_prefix_matches() const {  // the bytes so far could still become a header
    const std::size_t n = filled_ < spec_.header_length ? filled_ : spec_.header_length;
    for (std::size_t i = 0; i < n; ++i) {
      if (buffer_[i] != spec_.header[i]) return false;
    }
    return true;
  }
  void discard_first() {
    for (std::size_t i = 1; i < filled_; ++i) buffer_[i - 1] = buffer_[i];
    --filled_;
  }
  void drain(const FrameHandler& on_frame) {
    while (filled_ > 0) {
      if (!header_prefix_matches()) {
        discard_first();
        ++stats_.dropped_bytes;
        continue;
      }
      if (filled_ < spec_.frame_length) return;  // wait for more bytes
      bool tail_ok = true;
      for (std::size_t i = 0; i < spec_.tail_length; ++i) {
        if (buffer_[spec_.frame_length - spec_.tail_length + i] != spec_.tail[i]) tail_ok = false;
      }
      if (tail_ok) {
        ++stats_.frames;
        on_frame(buffer_.data(), spec_.frame_length);
        for (std::size_t i = spec_.frame_length; i < filled_; ++i) buffer_[i - spec_.frame_length] = buffer_[i];
        filled_ -= spec_.frame_length;
      } else {
        ++stats_.bad_frames;  // a false header: drop one byte and look again inside the same bytes
        discard_first();
      }
    }
  }

  ProtocolSpec spec_;
  std::array<std::uint8_t, kMaxFrameLength> buffer_{};
  std::size_t filled_ = 0;
  FramerStats stats_{};
};

}  // namespace armor
