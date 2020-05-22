#ifndef __RX_BUFFER_H
#define __RX_BUFFER_H
#include "framing.h"
#include "serial_listeners.h"

template <int RX_BYTES_MAX> class RxBuffer {
  uint8_t rx_buf_[RX_BYTES_MAX];
  uint32_t rx_i_ = 0;
  RxListener *rx_listener;
  uint8_t match_char_ = 0;
  static constexpr uint32_t RX_TIMEOUT_ = 115200 * 10;

public:
  RxBuffer(uint8_t match_char) : match_char_(match_char){};
  void Begin(RxListener *listener) { RestartRX(); };
  void RestartRX(RxListener *) {
    rx_i = 0;
    rx_listener = listener;
  }
  uint32_t ReceivedLength() { return RX_BYTES_MAX - rx_i_; };
  uint8_t *get() { return rx_buf_; }
  void PutByte(uint8_t b) {
    if (rx_i_ < RX_BYTES_MAX) {
      rx_buf_[rx_i_++] = b;
      if (FRAMING_MARK == b) {
        rx_listener->onCharMatch();
      }
    }
    if (rx_i_ >= RX_BYTES_MAX) {
      rx_listener->onRxComplete();
    }
  };
};

#endif
