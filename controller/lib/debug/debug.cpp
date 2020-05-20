/* Copyright 2020, RespiraWorks

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "debug.h"
#include "debug.pb.h"
#include "hal.h"
#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "sprintf.h"
#include <optional>
#include <stdarg.h>
#include <string.h>

namespace {

template <typename T, size_t N> static constexpr size_t ArraySize(T (&)[N]) {
  return N;
}

// HAL istream
//  - Bytes available?
//  - Read N bytes (blocking)
//  - Read up to N bytes (nonblocking)
//
// Unframer (istream)
//  - Reads bytes from a source
//  - Outputs unescaped bytes
//  - EOF when we hit a frame boundary.
//  - Does checksuming too?
//
// Buffer (istream)
//  - Reads bytes from a source
//
// ProtoIstream
//  - Reads bytes from a source.
//  - Writes into a proto*.

// TODO: Should this even be a class?
// TODO: Move to a separate library?
class DebugSerialPbIstream {
public:
  pb_istream_t *pb_stream() { return &pb_stream_; }

private:
  static bool callback(pb_istream_t *stream, uint8_t *buf, size_t count) {
    // TODO: Framing.
    // TODO: Checksum
    size_t written = 0;
    uint16_t remaining = static_cast<uint16_t>(count);
    while (remaining > 0) {
      written += Hal.debugRead(reinterpret_cast<char *>(buf), remaining);
    }
    return true;
  }

  pb_istream_t pb_stream_ = {
      .callback = &callback,
      .state = nullptr,
      .bytes_left = SIZE_MAX,
  };
};

class DebugSerialPbOstream {
public:
  pb_ostream_t *pb_stream() { return &pb_stream_; }

private:
  static bool callback(pb_ostream_t *stream, const uint8_t *buf, size_t count) {
    // TODO: Framing.
    // TODO: Checksum
    for (size_t i = 0; i < count; i++) {
      while (Hal.debugWrite(reinterpret_cast<const char *>(buf + i), 1) == 0) {
        // nop
      }
    }
    return true;
  }

  pb_ostream_t pb_stream_ = {
      .callback = &callback,
      .state = nullptr,
      .max_size = SIZE_MAX,
      .bytes_written = 0,
  };
};

template <typename Fn> class NanopbEncodeCallback {
public:
  explicit NanopbEncodeCallback(Fn &&fn) : fn_(std::forward<Fn>(fn)) {}

  pb_callback_t cb() {
    pb_callback_t ret;
    ret.arg = this;
    ret.funcs.encode = +[](pb_ostream_t *stream, const pb_field_iter_t *field,
                           void *const *arg) {
      if (!pb_encode_tag_for_field(stream, field))
        return false;
      auto *that = reinterpret_cast<NanopbEncodeCallback *>(*arg);
      return that->fn_(stream, field);
    };
    return ret;
  }

private:
  Fn fn_;
};

template <typename Fn>
NanopbEncodeCallback<Fn> MakeNanopbEncodeCallback(Fn &&fn) {
  return NanopbEncodeCallback<Fn>(std::forward<Fn>(fn));
}

void SendDebugResponse(const DebugResponse &resp) {
  DebugSerialPbOstream ostream;
  pb_encode(ostream.pb_stream(), DebugResponse_fields, &resp);
  // TODO: Raise an error if sending fails.
}

} // anonymous namespace

void DebugSerial::Poll() {
  if (!Hal.debugBytesAvailableForRead()) {
    return;
  }

  // There's a byte in the debug serial buffer.  Maaaaaail time!  Read a whole
  // DebugRequest message.

  DebugSerialPbIstream istream;
  DebugRequest req;
  if (!pb_decode(istream.pb_stream(), &DebugRequest_msg, &req)) {
    // TODO: Log an error
    return;
  }

  switch (req.which_request) {
  case DebugRequest_peek_tag:
    HandlePeek(req.request.peek);
    break;
  case DebugRequest_poke_tag:
    HandlePoke(req.request.poke);
    break;
  case DebugRequest_read_print_buf_tag:
    HandleReadPrintBuf(req.request.read_print_buf);
    break;
  case DebugRequest_read_vars_tag:
    HandleReadVars(req.request.read_vars);
    break;
  case DebugRequest_write_var_tag:
    HandleWriteVar(req.request.write_var);
    break;
  case DebugRequest_trace_tag:
    HandleTrace(req.request.trace);
    break;
  }
}

int DebugSerial::Print(const char *fmt, ...) {
  char buf[256];

  // Note that this uses a local sprintf implementation because
  // the one from the standard libraries will potentially dynamically
  // allocate memory.
  va_list ap;
  va_start(ap, fmt);
  int len = RWvsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  // Write as much as will fit to my print buffer.
  for (int i = 0; i < len; i++) {
    if (!printBuf.Put(buf[i]))
      return i;
  }
  return len;
}

void DebugSerial::HandlePeek(const DebugPeekRequest &req) {
  DebugResponse resp = DebugResponse_init_default;
  resp.which_response = DebugResponse_peek_tag;

  DebugPeekResponse &peek = resp.response.peek;
  peek = DebugPeekResponse_init_default;

  if (req.num_bytes > ArraySize(peek.data.bytes)) {
    peek.status = DebugPeekResponse_Status_TOO_MANY_BYTES;
  } else {
    peek.status = DebugPeekResponse_Status_OK;
    memcpy(peek.data.bytes, reinterpret_cast<const char *>(req.address),
           req.num_bytes);
    peek.data.size = static_cast<uint16_t>(req.num_bytes);
  }
  SendDebugResponse(resp);
}

void DebugSerial::HandlePoke(const DebugPokeRequest &req) {
  DebugResponse resp = DebugResponse_init_default;
  resp.which_response = DebugResponse_poke_tag;

  DebugPokeResponse &poke = resp.response.poke;
  poke = DebugPokeResponse_init_default;

  memcpy(reinterpret_cast<char *>(req.address), req.data.bytes, req.data.size);
  SendDebugResponse(resp);
}

void DebugSerial::HandleReadPrintBuf(const DebugReadPrintBufRequest &) {
  DebugResponse resp = DebugResponse_init_default;
  resp.which_response = DebugResponse_read_print_buf_tag;

  DebugReadPrintBufResponse &read_print_buf = resp.response.read_print_buf;
  auto data_cb = MakeNanopbEncodeCallback(
      [this](pb_ostream_t *stream, const pb_field_iter_t *) -> bool {
        // TODO: The circular buffer is actually made up of one or two
        // contiguous buffers, so if we cared about speed, we could use them
        // directly, rather than calling pb_write once for each char.
        for (std::optional<uint8_t> val = printBuf.Get(); val != std::nullopt;
             val = printBuf.Get()) {
          if (!pb_write(stream, &*val, 1)) {
            return false;
          }
        }
        return true;
      });
  read_print_buf.data = data_cb.cb();

  SendDebugResponse(resp);
}

void DebugSerial::HandleReadVars(const DebugReadVarsRequest &) {}
void DebugSerial::HandleWriteVar(const DebugWriteVarRequest &) {}
void DebugSerial::HandleTrace(const DebugTraceRequest &) {}

#if 0
// global debug handler
DebugSerial debug;

// List of registered command handlers
DebugCmd *DebugCmd::cmdList[256];

DebugSerial::DebugSerial() {
  buffNdx = 0;
  pollState = DbgPollState::WAIT_CMD;
  prevCharEsc = false;

  // TODO - This is annoying.  I had intended to make the constructors
  // of the various commands automatically add them to this list, but
  // the linker keeps removing them and I can't figure out how to
  // prevent that.  For now I'm just explicitely adding them here.
  // They still add themselves in their static constructors, but
  // that shouldn't cause any harm.
  extern DebugCmd mode, peek, poke, pbRead, varCmd, traceCmd;

  DebugCmd::cmdList[static_cast<int>(DbgCmdCode::MODE)] = &mode;
  DebugCmd::cmdList[static_cast<int>(DbgCmdCode::PEEK)] = &peek;
  DebugCmd::cmdList[static_cast<int>(DbgCmdCode::POKE)] = &poke;
  DebugCmd::cmdList[static_cast<int>(DbgCmdCode::PRINT_BUFF_READ)] = &pbRead;
  DebugCmd::cmdList[static_cast<int>(DbgCmdCode::VAR)] = &varCmd;
  DebugCmd::cmdList[static_cast<int>(DbgCmdCode::TRACE)] = &traceCmd;
}

// This function is called from the main low priority background loop.
// Its a simple state machine that waits for a new command to be received
// over the debug serial port.  Process the command when one is received
// and sends the response back.
void DebugSerial::Poll() {
  switch (pollState) {
  // Waiting for a new command to be received.
  // I continue to process bytes until there are no more available,
  // or a full command has been received.  Either way, the
  // ReadNextByte function will return false when its time
  // to move on.
  case DbgPollState::WAIT_CMD:
    while (ReadNextByte()) {
    }
    return;

  // Process the current command
  case DbgPollState::PROCESS_CMD:
    ProcessCmd();
    return;

  // Send my response
  case DbgPollState::SEND_RESP:
    while (SendNextByte()) {
    }
    return;
  }
}

// Read the next byte from the debug serial port
// Returns false if there were no more bytes available
// Also returns false if a full command has been received.
bool DebugSerial::ReadNextByte() {
  // Get the next byte from the debug serial port
  // if there is one.
  char ch;
  if (Hal.debugRead(&ch, 1) < 1)
    return false;

  uint8_t byte = static_cast<uint8_t>(ch);

  // If the previous character received was an escape character
  // then just save this byte (assuming there's space)
  if (prevCharEsc) {
    prevCharEsc = false;

    if (buffNdx < static_cast<int>(sizeof(cmdBuff)))
      cmdBuff[buffNdx++] = byte;
    return true;
  }

  // If this is an escape character, don't save it
  // just keep track of the fact that we saw it.
  if (byte == static_cast<uint8_t>(DbgSpecial::ESC)) {
    prevCharEsc = true;
    return true;
  }

  // If this is an termination character, then
  // change our state and return false
  if (byte == static_cast<uint8_t>(DbgSpecial::TERM)) {
    pollState = DbgPollState::PROCESS_CMD;
    return false;
  }

  // For other boring characters, just save them
  // if there's space in my buffer
  if (buffNdx < static_cast<int>(sizeof(cmdBuff)))
    cmdBuff[buffNdx++] = byte;
  return true;
}

// Send the next byte of my response to the last command.
// Returns false if no more data will fit in the output
// buffer, or if the entire response has been sent
bool DebugSerial::SendNextByte() {

  // To simplify things below, I require at least 3 bytes
  // in the output buffer to continue
  if (Hal.debugBytesAvailableForWrite() < 3)
    return false;

  // See what the next character to send is.
  uint8_t ch = cmdBuff[buffNdx++];

  // If its a special character, I need to escape it.
  if ((ch == static_cast<uint8_t>(DbgSpecial::TERM)) ||
      (ch == static_cast<uint8_t>(DbgSpecial::ESC))) {
    char tmp[2];
    tmp[0] = static_cast<char>(DbgSpecial::ESC);
    tmp[1] = ch;
    Hal.debugWrite(tmp, 2);
  }

  else {
    char tmp = ch;
    Hal.debugWrite(&tmp, 1);
  }

  // If there's more response to send, return true
  if (buffNdx < respLen)
    return true;

  // If that was the last byte in my response, send the
  // terminitaion character and start waiting on the next
  // command.
  char tmp = static_cast<char>(DbgSpecial::TERM);
  Hal.debugWrite(&tmp, 1);

  pollState = DbgPollState::WAIT_CMD;
  buffNdx = 0;
  return false;
}

// Process the received command
void DebugSerial::ProcessCmd() {
  // The total number of bytes received (not including
  // the termination byte) is the value of buffNdx.
  // This should be at least 3 (command & checksum).
  // If its not, I just ignore the command and jump
  // to waiting on the next.
  // This means we can send TERM characters to synchronize
  // communications if necessary
  if (buffNdx < 3) {
    buffNdx = 0;
    pollState = DbgPollState::WAIT_CMD;
    return;
  }

  uint16_t crc = CalcCRC(cmdBuff, buffNdx - 2);

  if (crc != u8_to_u16(&cmdBuff[buffNdx - 2])) {
    SendError(DbgErrCode::CRC_ERR);
    return;
  }

  DebugCmd *cmd = DebugCmd::cmdList[cmdBuff[0]];
  if (!cmd) {
    SendError(DbgErrCode::BAD_CMD);
    return;
  }

  // The length that we pass in to the command handler doesn't
  // include the command code or CRC.  The max size is also reduced
  // by 3 to make sure we can add the error code and CRC.
  int len = buffNdx - 3;
  DbgErrCode err =
      cmd->HandleCmd(&cmdBuff[1], &len, static_cast<int>(sizeof(cmdBuff) - 3));
  if (err != DbgErrCode::OK) {
    SendError(err);
    return;
  }

  cmdBuff[0] = static_cast<uint8_t>(DbgErrCode::OK);

  // Calculate the CRC on the data and error code returned
  // and append this to the end of the response
  crc = CalcCRC(cmdBuff, len + 1);
  u16_to_u8(crc, &cmdBuff[len + 1]);

  pollState = DbgPollState::SEND_RESP;
  buffNdx = 0;
  respLen = len + 3;
}

void DebugSerial::SendError(DbgErrCode err) {
  cmdBuff[0] = static_cast<uint8_t>(err);
  uint16_t crc = CalcCRC(cmdBuff, 1);
  u16_to_u8(crc, &cmdBuff[1]);
  pollState = DbgPollState::SEND_RESP;
  buffNdx = 0;
  respLen = 3;
}

// 16-bit CRC calculation for debug commands and responses
uint16_t DebugSerial::CalcCRC(uint8_t *buff, int len) {
  const uint16_t CRC16POLY = 0xA001;
  static bool init = false;
  static uint16_t tbl[256];

  // The first time this is called I'll build a table
  // to speed up CRC handling
  if (!init) {
    init = true;
    for (uint16_t i = 0; i < 256; i++) {
      uint16_t crc = i;

      for (int j = 0; j < 8; j++) {
        int lsbSet = (crc & 1) == 1;
        crc = static_cast<uint16_t>(crc >> 1);
        if (lsbSet)
          crc ^= CRC16POLY;
      }

      tbl[i] = crc;
    }
  }

  uint16_t crc = 0;

  for (int i = 0; i < len; i++) {
    uint16_t tmp = tbl[0xFF & (buff[i] ^ crc)];

    crc = static_cast<uint16_t>(tmp ^ (crc >> 8));
  }
  return crc;
}

DebugCmd::DebugCmd(DbgCmdCode opcode) {
  cmdList[static_cast<uint8_t>(opcode)] = this;
}
#endif
