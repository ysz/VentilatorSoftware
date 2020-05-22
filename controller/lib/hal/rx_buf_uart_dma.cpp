#include "rx_buf_uart_dma.h"
#include "uart_dma.h"

void RxBufferUartDma::Begin(RxListener *rxl) {
  uart_dma.charMatchEnable();
  uart_dma.startRX(rx_buf_, RX_BYTES_MAX, RX_TIMEOUT, rxl);
}

void RxBufferUartDma::RestartRX(RxListener *rxl) {
  uart_dma.stopRX();
  uart_dma.startRX(rx_buf_, RX_BYTES_MAX, RX_TIMEOUT, rxl);
}

uint32_t RxBufferUartDma::ReceivedLength() {
  return (RX_BYTES_MAX - uart_dma.getRxBytesLeft());
}
