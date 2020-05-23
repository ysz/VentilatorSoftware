#ifndef __UART_DMA
#define __UART_DMA
#include "hal_stm32_regs.h"
#include "serial_listeners.h"

class DMACtrl {
  DMA_Regs *const dma;

public:
  explicit DMACtrl(DMA_Regs *const dma) : dma(dma) {}
  void init() {
    // UART3 reception happens on DMA1 channel 3
    dma->chanSel.c3s = 0b0010;
    // UART3 transmission happens on DMA1 channel 2
    dma->chanSel.c2s = 0b0010;
  }
};

class UART_DMA {
  UART_Regs *const uart;
  DMA_Regs *const dma;
  uint8_t txCh;
  uint8_t rxCh;
  RxListener *rxListener = 0;
  TxListener *txListener = 0;
  char matchChar;

public:
#ifdef TEST_MODE
  UART_DMA() : uart(0), dma(0){};
#endif
  UART_DMA(UART_Regs *const uart, DMA_Regs *const dma, uint8_t txCh,
           uint8_t rxCh, char matchChar)
      : uart(uart), dma(dma), txCh(txCh), rxCh(rxCh), matchChar(matchChar) {}

  void init(int baud);
  // Returns true if DMA TX is in progress
  bool isTxInProgress();
  // Returns true if DMA RX is in progress
  bool isRxInProgress();

  // Sets up UART3 to transfer [length] characters from [buf]
  // Returns false if DMA transmission is in progress, does not
  // interrupt previous transmission.
  // Returns true if no transmission is in progress
  bool startTX(const uint8_t *buf, uint32_t length, TxListener *txl);

  uint32_t getRxBytesLeft();

  void stopTX();

  // Sets up reception of at least [length] chars from UART3 into [buf]
  // [timeout] is the number of baudrate bits for which RX line is
  // allowed to be idle before asserting timeout error.
  // Returns false if reception is in progress, new reception is not
  // setup. Returns true if no reception is in progress and new reception
  // was setup.

  bool startRX(const uint8_t *buf, uint32_t length, uint32_t timeout,
               RxListener *rxl);
  void stopRX();
  void charMatchEnable();

  void UART_ISR();
  void DMA_RX_ISR();
  void DMA_TX_ISR();

private:
  bool tx_in_progress;
  bool rx_in_progress;
};
#endif
