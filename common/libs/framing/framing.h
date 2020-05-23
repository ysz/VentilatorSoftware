#ifndef __FRAMING_H
#define __FRAMING_H
#include "checksum.h"
#include "network_protocol.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>
#include <stdint.h>

constexpr uint8_t FRAMING_MARK = 0xE2;
constexpr uint8_t FRAMING_ESC = 0x27;

uint32_t EscapeFrame(uint8_t *source, uint32_t sourceLength, uint8_t *dest,
                     uint32_t destLength);
uint32_t UnescapeFrame(uint8_t *source, uint32_t sourceLength, uint8_t *dest,
                       uint32_t destLength);

enum DecodeResult { SUCCESS, ERROR_FRAMING, ERROR_CRC, ERROR_PB };

template <typename PbType, auto PbObject_fields,
          uint32_t(crc_func)(const uint8_t *, uint32_t)>
DecodeResult DecodeFrame(uint8_t *buf, uint32_t len, PbType *pb_object) {
  uint32_t decoded_length = UnescapeFrame(buf, len, buf, len);
  if (0 == decoded_length) {
    return DecodeResult::ERROR_FRAMING;
  }
  if (!is_crc_pass<crc_func>(buf, len)) {
    return DecodeResult::ERROR_CRC;
  }
  pb_istream_t stream = pb_istream_from_buffer(buf, decoded_length - 4);
  if (!pb_decode(&stream, PbObject_fields, pb_object)) {
    return DecodeResult::ERROR_PB;
  }
  return DecodeResult::SUCCESS;
}

// Serializes current controller status, adds crc and escapes it.
// The resulting frame is written into tx buffer.
// Returns the length of the resulting frame.
template <typename PbType, auto PbObject_fields, int PbType_size,
          uint32_t(crc_func)(const uint8_t *, uint32_t)>
uint32_t EncodeFrame(const PbType &pb_object, uint8_t *dest_buf,
                     uint32_t dest_len) {
  uint8_t pb_buffer[PbType_size + 4];

  pb_ostream_t stream = pb_ostream_from_buffer(pb_buffer, sizeof(pb_buffer));
  if (!pb_encode(&stream, PbObject_fields, &pb_object)) {
    // TODO: Serialization failure; log an error or raise an alert.
    return 0;
  }
  uint32_t pb_data_len = (uint32_t)(stream.bytes_written);

  uint32_t crc = crc_func(pb_buffer, pb_data_len);
  if (!append_crc(pb_buffer, pb_data_len, sizeof(pb_buffer), crc)) {
    // TODO log an error, output buffer too small
    return 0;
  }

  return EscapeFrame(pb_buffer, pb_data_len + 4, dest_buf, dest_len);
}

#endif
