#include "comms.h"

#include "algorithm.h"
#include "checksum.h"
#include "framing.h"
#include "hal.h"
#include "network_protocol.pb.h"
#include "uart_dma.h"

#include "debug.h"

extern UART_DMA uart_dma;

RxBufferUartDma<RX_FRAME_LEN_MAX> rx_buffer(uart_dma);
FrameDetector<RxBufferUartDma<RX_FRAME_LEN_MAX>, RX_FRAME_LEN_MAX>
    frame_detector(rx_buffer);

// Note that the initial value of last_tx has to be invalid; changing it to 0
// wouldn't work.  We immediately transmit on boot, and after
// we do that, we want to wait a full TX_INTERVAL_MS.  If we initialized
// last_tx to 0 and our first transmit happened at time millis() == 0, we
// would set last_tx back to 0 and then retransmit immediately.
bool Comms::is_time_to_transmit() {
  return (last_tx == kInvalidTime || Hal.now() - last_tx > TX_INTERVAL);
}

bool Comms::is_transmitting() { return uart_dma_.isTxInProgress(); }

void Comms::onTxComplete() { debugPrint("$"); }

void Comms::onTxError() { debugPrint("E"); }

static uint32_t hard_crc32(const uint8_t *data, uint32_t length) {
  return Hal.crc32(data, length);
}

static auto EncodeControllerStatusFrame =
    EncodeFrame<ControllerStatus, ControllerStatus_fields,
                ControllerStatus_size, &hard_crc32>;

void Comms::process_tx(const ControllerStatus &controller_status) {
  // Serialize our current state into the buffer if
  //  - we're not currently transmitting,
  //  - it's been a while since we last transmitted.

  if (!is_transmitting() && is_time_to_transmit()) {
    uint32_t frame_len =
        EncodeControllerStatusFrame(controller_status, tx_buffer, TX_BUF_LEN);

    if (0 == frame_len) {
      debugPrint("0");
      // TODO log an error
    }

    debugPrint("*");
    uart_dma_.startTX(tx_buffer, frame_len, this);
    last_tx = Hal.now();
  }

  // TODO: Alarm if we haven't been able to send a status in a certain amount
  // of time.
}

static auto DecodeGuiStatusFrame =
    DecodeFrame<GuiStatus, GuiStatus_fields, &hard_crc32>;

void Comms::process_rx(GuiStatus *gui_status) {
  if (frame_detector_.is_frame_available()) {
    uint8_t *buf = frame_detector_.get_frame_buf();
    uint32_t len = frame_detector_.get_frame_length();

    GuiStatus new_gui_status = GuiStatus_init_zero;
    DecodeResult result = DecodeGuiStatusFrame(buf, len, &new_gui_status);
    if (DecodeResult::SUCCESS == result) {
      *gui_status = new_gui_status;
      last_rx = Hal.now();
    }
  }
}

void Comms::init() { frame_detector_.Begin(); }

void Comms::handler(const ControllerStatus &controller_status,
                    GuiStatus *gui_status) {

  process_tx(controller_status);
  process_rx(gui_status);
}
