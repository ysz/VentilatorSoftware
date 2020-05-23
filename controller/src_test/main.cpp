#include "debug.h"
#include "hal.h"
#include "hal_stm32.h"
#include "uart_dma.h"
#include <string.h>

char r[20];

class DummyTxListener : public TxListener {
  void onTxComplete() { debugPrint("$"); }
  void onTxError() { debugPrint("E"); };
};

class DummyRxListener : public RxListener {
public:
  void onRxComplete() {
    debugPrint("&");
    debugPrint(r);
  }
  void onCharacterMatch() { debugPrint("@"); }
  void onRxError(RxError_t e) {
    if (RX_ERROR_TIMEOUT == e) {
      debugPrint("T");
    } else {
      debugPrint("#");
    };
  }
};

// FrameDetector listener = FrameDetector();
DummyRxListener rxlistener;
DummyTxListener txlistener;

DMACtrl dmaController(DMA1_BASE);

#include "framing.h"
constexpr uint8_t txCh = 1;
constexpr uint8_t rxCh = 2;
UART_DMA uart_dma(UART3_BASE, DMA1_BASE, txCh, rxCh, '.');

int main() {
  Hal.init();
  dmaController.init();

  debugPrint("*");
  char s[] = "ping ping ping ping ping ping ping ping ping ping ping ping\n";
  bool dmaStarted = false;

  dmaStarted = uart_dma.startTX((uint8_t *)s, strlen(s), &txlistener);
  if (dmaStarted) {
    debugPrint("!");
  }

  uart_dma.charMatchEnable();

  dmaStarted = uart_dma.startRX((uint8_t *)r, 10, 115200 * 2, &rxlistener);
  if (dmaStarted) {
    debugPrint("!");
  }

  while (1) {
    Hal.watchdog_handler();
    char i[1];
    if (1 == debugRead(i, 1)) {
      Hal.reset_device();
    }
    Hal.delay(milliseconds(10));
  }
}
