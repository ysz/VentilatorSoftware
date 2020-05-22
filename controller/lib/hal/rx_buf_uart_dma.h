#ifndef __HAL_TRANSPORT
#include "network_protocol.pb.h"
#include "uart_dma.h"

class RxBufferUartDma {
  UART_DMA &uart_dma;
  // Size of the buffer is set asuming a corner case where EVERY GuiStatus
  // byte and CRC32 will be escaped + two marker chars; this is too big, but
  // safe.
  static constexpr uint32_t RX_BUF_LEN = (GuiStatus_size + 4) * 2 + 2;
  static constexpr uint32_t RX_BYTES_MAX = RX_BUF_LEN;
  uint8_t rx_buf_[RX_BUF_LEN];
  static constexpr uint32_t RX_TIMEOUT = 115200 * 10;

public:
  RxBufferUartDma(UART_DMA &uart_dma) : uart_dma(uart_dma){};
  void Begin(RxListener *);
  void RestartRX(RxListener *);
  uint32_t ReceivedLength();
  uint8_t *get() { return rx_buf_; }
#ifdef TEST_MODE
  void test_PutByte(uint8_t b);
#endif
};
#endif
