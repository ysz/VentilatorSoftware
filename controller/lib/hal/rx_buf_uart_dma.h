#ifndef __HAL_TRANSPORT
#include "network_protocol.pb.h"
#include "uart_dma.h"

template <int RX_BYTES_MAX> class RxBufferUartDma {
  UART_DMA &uart_dma;
  uint8_t rx_buf_[RX_BYTES_MAX];
  static constexpr uint32_t RX_TIMEOUT_ = 115200 * 10;

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

template <int RX_BYTES_MAX>
void RxBufferUartDma<RX_BYTES_MAX>::Begin(RxListener *rxl) {
  uart_dma.charMatchEnable();
  uart_dma.startRX(rx_buf_, RX_BYTES_MAX, RX_TIMEOUT_, rxl);
}
template <int RX_BYTES_MAX>
void RxBufferUartDma<RX_BYTES_MAX>::RestartRX(RxListener *rxl) {
  uart_dma.stopRX();
  uart_dma.startRX(rx_buf_, RX_BYTES_MAX, RX_TIMEOUT_, rxl);
}
template <int RX_BYTES_MAX>
uint32_t RxBufferUartDma<RX_BYTES_MAX>::ReceivedLength() {
  return (RX_BYTES_MAX - uart_dma.getRxBytesLeft());
}

#ifdef TEST_MODE
extern uint32_t rx_i;
template <int RX_BYTES_MAX>
void RxBufferUartDma<RX_BYTES_MAX>::test_PutByte(uint8_t b) {
  // printf("[%d] ", b);
  rx_buf_[rx_i++] = b;
}
#endif

#endif
