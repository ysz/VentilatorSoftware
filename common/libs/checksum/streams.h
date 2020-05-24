#include "checksum.h"
#include "framing.h"
#include "uart_dma.h"
#include <stdint.h>
class Stream {
public:
  void Put(uint8_t b);
};

constexpr uint32_t END_OF_STREAM = -1;

class CrcStream : Stream {
  Stream output;
  uint32_t crc;

public:
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

class EscapeStream : Stream {
  Stream output;
  bool reset = true;

  inline bool shouldEscape(uint8_t b) {
    return FRAMING_MARK == b || FRAMING_ESC == b;
  }

public:
  void Put(uint8_t b) {
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

class DmaStream : Stream() : TxListener {
  constexpr uint32_t BUF_LEN = 400;
  uint8_t buf1[BUF_LEN];
  uint8_t buf2[BUF_LEN];
  uint32_t i = 0;
  uint8_t *buf = buf1;
  uint8_t active_buf;
  void SwapBuffers() { i = 0; }
  bool BufIsFull() { return i >= BUF_LEN; }

public:
  void Put(uint8_t b) {
    if (END_OF_STREAM == b) {
      // busy wait
      while (uart_dma.isTxInProgress()) {
      }

      uart_dma.startTX(buf, i, this);
      SwapBuffers();
    } else {
      buf[i++] = b;
      if (BufIsFull()) {
        // busy wait
        while (uart_dma.isTxInProgress()) {
        }
        uart_dma.startTX(buf, i, this);
        SwapBuffers();
      }
    }
  }
  else {
  }
  void onTxComplete() { SwapBuffers(); }
  void onTxError() {}
};
