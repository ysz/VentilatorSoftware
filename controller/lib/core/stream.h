#ifndef STREAM_H_
#define STREAM_H_

#include "checksum.h"
#include "pb.h"
#include "pb_decode.h"
#include <optional>
#include <stdint.h>

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

template <int BufSize> class FramedBuffer {
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

  std::pair<uint8_t *, int32_t> Get() { return {buf_, pos_}; }
  pb_istream_t PbStream() { return pb_istream_from_buffer(buf_, pos_); }

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

#if 0 // DO NOT SUBMIT - Remove me!

enum class StreamStatus {
  OK, // TODO: Rename OK to "more" or smth
  END,
  ERROR,
};

template <typename BytesAvailFn, typename ReadFn> class BaseIstream {
public:
  BaseIstream(BytesAvailFn bytes_avail_fn, ReadFn &&read_fn)
      : bytes_avail_fn_(std::move(bytes_avail_fn)),
        read_fn_(std::move(read_fn)) {}

  int32_t bytes_available() { return bytes_avail_fn_(); }

  [[nodiscard]] std::pair<int32_t, StreamStatus> read(uint8_t *buf,
                                                      int32_t max_nbytes) {
    return read_fn_(buf, max_nbytes);
  }

  void reset() {}

private:
  BytesAvailFn bytes_avail_fn_;
  ReadFn read_fn_;
};
template <typename BytesAvailFn, typename ReadFn>
decltype(auto) MakeBaseIstream(BytesAvailFn bytes_avail_fn, ReadFn read_fn) {
  return BaseIstream<BytesAvailFn, ReadFn>(std::move(bytes_avail_fn),
                                           std::move(read_fn));
}

template <typename InputStream> class FramedIstream {
public:
  explicit FramedIstream(InputStream *base) : base_(*base) {}

  [[nodiscard]] std::pair<int32_t, StreamStatus> read(uint8_t *buf,
                                                      int32_t max_nbytes) {
    return base_.read(buf, max_nbytes);
  }

  void reset() {
    // TODO
    base_.reset();
  }

private:
  InputStream &base_;
};
template <typename InputStream>
decltype(auto) MakeFramedIstream(InputStream *base) {
  return FramedIstream<InputStream>(base);
}

// TODO: Refactor so that it always returns 0 bytes until it hits the end, and
// then it returns everything.
template <int32_t NBytes, typename InputStream> class BufferedIstream {
public:
  explicit BufferedIstream(InputStream *base) : base_(*base) { reset(); }

  [[nodiscard]] StreamStatus readIntoBuffer() {
    if (error_) {
      return StreamStatus::ERROR;
    }
    int32_t n = NBytes - input_pos_;
    if (n == 0) {
      return StreamStatus::END;
    }

    auto [nread, status] = base_.read(bytes_ + input_pos_, n);
    switch (status) {
    case StreamStatus::ERROR:
      error_ = true;
      return StreamStatus::ERROR;
    case StreamStatus::OK:
    case StreamStatus::END:
      input_pos_ += nread;
      break;
    }
    return input_pos_ == NBytes ? StreamStatus::END : StreamStatus::OK;
  }
  [[nodiscard]] std::pair<int32_t, StreamStatus> read(uint8_t *buf,
                                                      int32_t max_nbytes) {
    if (error_) {
      return {0, StreamStatus::ERROR};
    }
    int32_t n = std::min(max_nbytes, NBytes - output_pos_);
    memcpy(buf, bytes_ + output_pos_, n);
    return {n, output_pos_ == NBytes ? StreamStatus::END : StreamStatus::OK};
  }

  void reset() {
    error_ = false;
    input_pos_ = 0;
    output_pos_ = 0;
    base_.reset();
  }

private:
  InputStream &base_;
  bool error_;
  int32_t input_pos_;
  int32_t output_pos_;
  uint8_t bytes_[NBytes];
};
template <uint32_t NBytes, typename InputStream>
decltype(auto) MakeBufferedIstream(InputStream *base) {
  return BufferedIstream<NBytes, InputStream>(base);
}

template <typename InputStream>
bool blockingDecodeProto(InputStream *istream, const pb_msgdesc_t *msgdsc,
                         void *proto) {
  auto *read_from_stream =
      +[](pb_istream_t *stream, uint8_t *buf, size_t count) {
        InputStream &istream = *reinterpret_cast<InputStream *>(stream->state);
        int32_t read = 0;
        int32_t remaining = static_cast<int32_t>(count);
        while (remaining > 0) {
          auto [n, status] = istream.read(buf + read, remaining);
          switch (status) {
          case StreamStatus::OK:
            read += n;
            remaining -= n;
            break;
          case StreamStatus::END:
            // Indicate EOF to nanopb: "After getting EOF error when reading,
            // set bytes_left to 0 and return false. pb_decode will detect this
            // and if the EOF was in a proper position, it will return true."
            // https://jpa.kapsi.fi/nanopb/docs/concepts.html#input-streams
            stream->bytes_left = 0;
            return false;
          case StreamStatus::ERROR:
            return false;
          }
        }
        return true;
      };
  pb_istream_t pb_stream = {
      .callback = read_from_stream,
      .state = istream,
      .bytes_left = SIZE_MAX,
  };
  return pb_decode(&pb_stream, msgdsc, proto);
}
#endif

#endif // STREAM_H_
