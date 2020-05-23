#ifndef STREAM_H_
#define STREAM_H_

#include "checksum.h"
#include "circular_buffer.h"
#include "pb.h"
#include "pb_decode.h"
#include <optional>
#include <stdint.h>

class HalSerialSource {};
class Unframer {};
class CrcChecker {};
class FramedBufferSink {
  bool process();
  bool finished();
  bool error();
};

#if 0
class CrcChecker {
public:
  std::optional<uint8_t> ProcessByte(uint8_t b) {
    switch (mode_) {
    case NORMAL:
      calculated_crc_ = soft_crc32_single(calculated_crc_, b);
      return b;
    case CHECKSUM:
      if (seen_crc_pos_ < sizeof(seen_crc_)) {
        seen_crc_ |= uint32_t{b} << (8 * seen_crc_pos_);
        seen_crc_pos_++;
      } else {
        mode_ = ERROR;
      }
      return std::nullopt;
    case ERROR:
      return std::nullopt;
    }
    // All cases covered above (GCC checks this).
    __builtin_unreachable();
  }

  void StartChecksumPortion() { mode_ = CHECKSUM; }

  bool Eof() {
    return mode_ == CHECKSUM && seen_crc_pos_ == sizeof(seen_crc_) &&
           seen_crc_ == calculated_crc_;
  }
  bool Error() {
    return mode_ == ERROR ||
           (mode_ == CHECKSUM && seen_crc_pos_ == sizeof(seen_crc_) &&
            seen_crc_ != calculated_crc_);
  }

private:
  enum Mode {
    NORMAL,
    CHECKSUM,
    ERROR,
  };
  Mode mode_ = NORMAL;

  uint32_t calculated_crc_ = static_cast<uint32_t>(-1);
  uint32_t seen_crc_pos_ = 0;
  uint32_t seen_crc_ = 0;
};

class Unframer {
public:
  std::optional<uint8_t> ProcessByte(uint8_t b) {
    switch (mode_) {
    case NORMAL:
      if (b != ESCAPE_BYTE) {
        return crc_.ProcessByte(b);
      } else {
        mode_ = ESCAPE;
      }
    case ESCAPE:
      switch (b) {
      case ESCAPE_BYTE:
        return crc_.ProcessByte(b);
      case CRC_BYTE:
        crc_.StartChecksumPortion();
        return std::nullopt;
      default:
        mode_ = ERROR;
        return std::nullopt;
      }
    case ERROR:
      return std::nullopt;
    }
    // All cases covered above (GCC checks this).
    __builtin_unreachable();
  }

  bool Eof() { return crc_.Eof(); }
  bool Error() { return mode_ == ERROR || crc_.Error(); }

private:
  static constexpr uint8_t ESCAPE_BYTE = 0;
  static constexpr uint8_t CRC_BYTE = 1;

  enum Mode {
    NORMAL,
    ESCAPE,
    ERROR,
  };
  Mode mode_;
  CrcChecker crc_;
};

template <int BufSize> class FramedInputBuffer {
public:
  // Returns true if can consume additional bytes.
  bool Consume(uint8_t b) {
    if (Error() || Eof()) {
      return false;
    }
    std::optional<uint8_t> c = unframer_.ProcessByte(b);
    if (c != std::nullopt) {
      if (pos_ < BufSize) {
        buf_[pos_++] = *c;
      } else {
        mode_ = OVERFLOW;
      }
    }
    return Error() || Eof();
  }

  bool Error() { return mode_ == OVERFLOW || unframer_.Error(); }
  bool Eof() { return mode_ == NORMAL && unframer_.Eof(); }

  std::pair<uint8_t*, int32_t> Get() {
    return {buf_, pos_};
  }
  pb_istream_t PbStream() {
    return pb_istream_from_buffer(buf_, pos_);
  }

private:
  enum Mode {
    NORMAL,
    OVERFLOW,
  };
  Mode mode_ = NORMAL;
  Unframer unframer_;
  uint8_t buf_[BufSize];
  int32_t pos_ = 0;
};

class Framer {
public:
private:
  CircBuff<uint8_t, 4>  buf_;
};
#endif

#endif // STREAM_H_
