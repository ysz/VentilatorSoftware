#include "hal_transport.h"
#include "uart_dma.h"

void HalTransport::Begin(RxListener *rxl) {
  uart_dma.charMatchEnable();
  uart_dma.startRX(rx_buf_, RX_BYTES_MAX, RX_TIMEOUT, rxl);
}

void HalTransport::RestartRX(RxListener *rxl) {
  uart_dma.stopRX();
  uart_dma.startRX(rx_buf_, RX_BYTES_MAX, RX_TIMEOUT, rxl);
}

uint32_t HalTransport::ReceivedLength() {
  return (RX_BYTES_MAX - uart_dma.getRxBytesLeft());
}
