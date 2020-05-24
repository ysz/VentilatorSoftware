#include "checksum.h"
#include "framing.h"
#include "uart_dma.h"
#include <stdint.h>
class Stream {
public:
  virtual void Put(int32_t b) = 0;
};

constexpr uint32_t END_OF_STREAM = -1;

class CrcStream : public Stream {
  Stream &output;
  uint32_t crc;

public:
  CrcStream(Stream &os) : output(os){};
  void Put(int32_t b) {
    if (END_OF_STREAM == b) {
      EmitCrcAndReset();
    } else {
      crc = crc32_single(crc, b);
      output.Put(b);
    }
  }

  void EmitCrcAndReset() {
    output.Put(static_cast<uint8_t>((crc >> 24) & 0x000000FF));
    output.Put(static_cast<uint8_t>((crc >> 16) & 0x000000FF));
    output.Put(static_cast<uint8_t>((crc >> 8) & 0x000000FF));
    output.Put(static_cast<uint8_t>(crc & 0x000000FF));
    crc = 0xFFFFFFFF;
  }
};

class EscapeStream : public Stream {
  Stream &output;
  bool reset = true;

  inline bool shouldEscape(uint8_t b) {
    return FRAMING_MARK == b || FRAMING_ESC == b;
  }

public:
  EscapeStream(Stream &os) : output(os){};

  void Put(int32_t b) {
    if (reset) {
      reset = false;
      output.Put(FRAMING_MARK);
    }
    if (END_OF_STREAM == b) {
      output.Put(FRAMING_MARK);
      reset = true;
    } else {
      if (shouldEscape(b)) {
        output.Put(FRAMING_ESC);
        output.Put(b ^ 0x20);
      } else {
        output.Put(b);
      }
    }
  }
};
extern UART_DMA uart_dma;

class DmaStream : public Stream, public TxListener {
  static constexpr uint32_t BUF_LEN = 400;
  uint8_t buf1[BUF_LEN];
  uint8_t buf2[BUF_LEN];
  uint32_t i = 0;
  uint8_t *buf = buf1;
  uint8_t active_buf = 1;

  void SwapBuffers() {
    if (1 == active_buf) {
      buf = buf2;
      active_buf = 2;
    }
    if (2 == active_buf) {
      buf = buf1;
      active_buf = 1;
    } else {
      // halt and catch on fire
    }
    i = 0;
  }

  bool BufIsFull() { return i >= BUF_LEN; }

  void Transmit() {
    // busy wait
    while (uart_dma.isTxInProgress()) {
    }

    uart_dma.startTX(buf, i, this);
  }

public:
  void Put(int32_t b) {
    if (END_OF_STREAM == b) {
      Transmit();
      SwapBuffers();
    } else {
      buf[i++] = static_cast<uint8_t>(b);
      if (BufIsFull()) {
        Transmit();
        SwapBuffers();
      }
    }
  }

  void onTxComplete() {}

  void onTxError() {}
};

void ShipIt(const uint8_t *buf, uint32_t len) {
  DmaStream dma_stream;
  EscapeStream esc_stream(dma_stream);
  CrcStream crc_stream(esc_stream);
  for (uint32_t i = 0; i < len; i++) {
    crc_stream.Put(buf[i]);
  }
}
