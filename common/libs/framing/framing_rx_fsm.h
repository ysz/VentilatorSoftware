#ifndef __FRAMING_RX_FSM
#define __FRAMING_RX_FSM

#include "network_protocol.pb.h"
#include "uart_dma.h"

template <class Transport> class FramingRxFSM : public RxListener {
  enum State_t { STATE_LOST, STATE_WAIT_START, STATE_RX_FRAME };

  Transport &transport_;
  State_t state = STATE_LOST;
  uint32_t error_counter_ = 0;
  bool frame_available_ = false;
  static constexpr uint32_t RX_BUF_LEN = (GuiStatus_size + 4) * 2 + 2;
  uint8_t out_buf[RX_BUF_LEN];
  uint32_t out_buf_length = 0;

public:
  FramingRxFSM(Transport &t) : transport_(t){};
  void Begin();
  void onRxComplete() override;
  void onCharacterMatch() override;
  void onRxError(RxError_t e) override;
  uint8_t *get_received_buf();
  uint32_t get_received_length();
  bool is_frame_available();

private:
  void processReceivedData();
};

template <class Transport> void FramingRxFSM<Transport>::Begin() {
  state = STATE_LOST;
  transport_.Begin(this);
}

template <class Transport> void FramingRxFSM<Transport>::onRxComplete() {
  // We should never reach the full read of rx buffer.
  // If we get here, this means, there are no marker
  // chars in the stream, so we are lost
  error_counter_++;
  state = STATE_LOST;
  transport_.RestartRX(this);
}

template <class Transport> void FramingRxFSM<Transport>::onCharacterMatch() {
  switch (state) {
  case STATE_LOST:
    // if we have received something before this marker,
    // we assume, this is the frame end marker, so wait
    // for start
    if (transport_.ReceivedLength() > 1) {
      state = STATE_WAIT_START;
      // printf("\nLOST > WAIT_START\n");
      // if we were lucky to get lost in the interframe silence,
      // assume this is the start of the frame
    } else if (transport_.ReceivedLength() == 1) {
      state = STATE_RX_FRAME;
      // printf("\nLOST > RX_FRAME\n");
    } else {
      // printf("!!!! DMA not working!");
      // TODO alert, safe reset
      // Should never end up here
      // DMA is not working?
    }
    break;
  case STATE_WAIT_START:
    if (transport_.ReceivedLength() == 1) {
      // printf("\nWAIT_START > RX_FRAME\n");
      state = STATE_RX_FRAME;
    } else {
      // some junk received while waiting for start marker,
      // but should have been just silence
      error_counter_++;
      state = STATE_LOST;
      // printf("! JUNK!");
    }
    break;
  case STATE_RX_FRAME:
    // end marker received, check if we got something
    if (transport_.ReceivedLength() > 1) {
      processReceivedData();
      // printf("\nRX_FRAME > WAIT_START\n");
      state = STATE_WAIT_START;
    } else {
      // printf("! REPEAT MARK");
      // repeated marker char received
      // assume we are still good
    }
    break;
  }
  transport_.RestartRX(this);
}

template <class Transport>
void FramingRxFSM<Transport>::onRxError(RxError_t e) {
  switch (state) {
  case STATE_LOST:
  case STATE_WAIT_START:
    // no change
    break;
  case STATE_RX_FRAME:
    state = STATE_LOST;
    break;
  }
  error_counter_++;
};

template <class Transport> void FramingRxFSM<Transport>::processReceivedData() {
  // we strip markers from the stream, but that does not influence the frame
  // decoder code
  out_buf_length = transport_.ReceivedLength() - 1;
  memcpy(out_buf, transport_.get_rx_buf(), out_buf_length);
  frame_available_ = true;
}

template <class Transport>
uint8_t *FramingRxFSM<Transport>::get_received_buf() {
  frame_available_ = false;
  return out_buf;
}

template <class Transport>
uint32_t FramingRxFSM<Transport>::get_received_length() {
  return out_buf_length;
}

template <class Transport> bool FramingRxFSM<Transport>::is_frame_available() {
  return frame_available_;
}

// #ifdef TEST_MODE
// template <class Transport>
// void FramingRxFSM<Transport>::test_PutRxBuffer(uint8_t *buf, uint32_t len) {
//   transport_.test_PutRxBuffer(buf, len);
// }
// #endif

#endif
