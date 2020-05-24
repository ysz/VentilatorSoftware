#ifndef __FRAMING_RX_FSM
#define __FRAMING_RX_FSM

#include "serial_listeners.h"
#include <string.h>

template <class RxBuffer, int FRAME_BUF_LEN>
class FrameDetector : public RxListener {
  enum State_t { STATE_LOST, STATE_WAIT_START, STATE_RX_FRAME };

  RxBuffer &rx_buffer_;
  State_t state = STATE_LOST;
  uint32_t error_counter_ = 0;
  bool frame_available_ = false;
  uint8_t frame_buf_[FRAME_BUF_LEN];
  uint32_t frame_buf_length_ = 0;

public:
  FrameDetector(RxBuffer &t) : rx_buffer_(t){};
  void Begin();
  void onRxComplete() override;
  void onCharacterMatch() override;
  void onRxError(RxError_t e) override;
  uint8_t *get_frame_buf();
  uint32_t get_frame_length();
  bool is_frame_available();

private:
  void processReceivedData();
};

template <class RxBuffer, int FRAME_BUF_LEN>
void FrameDetector<RxBuffer, FRAME_BUF_LEN>::Begin() {
  state = STATE_LOST;
  rx_buffer_.Begin(this);
}

template <class RxBuffer, int FRAME_BUF_LEN>
void FrameDetector<RxBuffer, FRAME_BUF_LEN>::onRxComplete() {
  // We should never reach the full read of rx buffer.
  // If we get here, this means, there are no marker
  // chars in the stream, so we are lost
  error_counter_++;
  state = STATE_LOST;
  rx_buffer_.RestartRX(this);
}

template <class RxBuffer, int FRAME_BUF_LEN>
void FrameDetector<RxBuffer, FRAME_BUF_LEN>::onCharacterMatch() {
  switch (state) {
  case STATE_LOST:
    // if we have received something before this marker,
    // we assume, this is the frame end marker, so wait
    // for start
    if (rx_buffer_.ReceivedLength() > 1) {
      state = STATE_WAIT_START;
      // printf("\nLOST > WAIT_START\n");
      // if we were lucky to get lost in the interframe silence,
      // assume this is the start of the frame
    } else if (rx_buffer_.ReceivedLength() == 1) {
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
    if (rx_buffer_.ReceivedLength() == 1) {
      // printf("\nWAIT_START > RX_FRAME\n");
      state = STATE_RX_FRAME;
    } else {
      // some junk received while waiting for start marker,
      // but should have been just silence
      error_counter_++;
      state = STATE_LOST;
      // printf("! JUNK ! %d", rx_buffer_.ReceivedLength());
      // printf("\nWAIT_START > LOST\n");
    }
    break;
  case STATE_RX_FRAME:
    // end marker received, check if we got something
    if (rx_buffer_.ReceivedLength() > 1) {
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
  rx_buffer_.RestartRX(this);
}

template <class RxBuffer, int FRAME_BUF_LEN>
void FrameDetector<RxBuffer, FRAME_BUF_LEN>::onRxError(RxError_t e) {
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

template <class RxBuffer, int FRAME_BUF_LEN>
void FrameDetector<RxBuffer, FRAME_BUF_LEN>::processReceivedData() {
  // we strip markers from the stream, but that does not influence the frame
  // decoder code
  frame_buf_length_ = rx_buffer_.ReceivedLength() - 1;
  memcpy(frame_buf_, rx_buffer_.get(), frame_buf_length_);
  frame_available_ = true;
}

template <class RxBuffer, int FRAME_BUF_LEN>
uint8_t *FrameDetector<RxBuffer, FRAME_BUF_LEN>::get_frame_buf() {
  frame_available_ = false;
  return frame_buf_;
}

template <class RxBuffer, int FRAME_BUF_LEN>
uint32_t FrameDetector<RxBuffer, FRAME_BUF_LEN>::get_frame_length() {
  return frame_buf_length_;
}

template <class RxBuffer, int FRAME_BUF_LEN>
bool FrameDetector<RxBuffer, FRAME_BUF_LEN>::is_frame_available() {
  return frame_available_;
}

#endif
