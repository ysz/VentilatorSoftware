/* Copyright 2020, RespiraWorks

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

This file implements the HAL (Hardware Abstraction Layer) for the
STM32L452 processor used on the controller.  Details of the processor's
peripherals can be found in the reference manual for that processor:
   https://www.st.com/resource/en/reference_manual/dm00151940-stm32l41xxx42xxx43xxx44xxx45xxx46xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf

Details specific to the ARM processor used in this chip can be found in
the programmer's manual for the processor available here:
   https://www.st.com/resource/en/programming_manual/dm00046982-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf

*/

#if defined(BARE_STM32)

#include "hal_stm32.h"
#include "checksum.h"
#include "circular_buffer.h"
#include "hal.h"
#include "stepper.h"
#include "uart_dma.h"
#include <optional>
#include <stdarg.h>
#include <stdio.h>

#define SYSTEM_STACK_SIZE 2500

// This is the main stack used in our system.
__attribute__((aligned(8))) uint32_t systemStack[SYSTEM_STACK_SIZE];

// local data
static volatile int64_t msCount;

// local static functions.  I don't want to add any private
// functions to the Hal class to avoid complexity with other
// builds
static void Timer6ISR();
static void Timer15ISR();
void UART3_ISR();
void DMA1_CH2_ISR();
void DMA1_CH3_ISR();

// This function is called from the libc initialization code
// before any static constructors are called.
//
// It calls the Hal function used to initialize the processor.
extern "C" void _init() { Hal.EarlyInit(); }

// This function is called _init() above.  It does some basic
// chip initialization.
//
// The main things done here are to enable the FPU because if
// we don't do that then we'll get a fatal exception if any
// constructor uses any floating point math, and to enable
// the PLL so we can run at full speed (80MHz) rather then the
// default speed of 4MHz.
void HalApi::EarlyInit() {
  // Enable the FPU.  This allows floating point to be used without
  // generating a hard fault.
  // The system control registers are documented in the programmers
  // manual (not the reference manual) chapter 4.
  // Details on enabling the FPU are in section 4.6.6.
  SysCtrl_Reg *sysCtl = SYSCTL_BASE;
  sysCtl->cpac = 0x00F00000;

  // Reset caches and set latency for 80MHz opperation
  // See chapter 3 of the reference manual for details
  // on the embedded flash module
  EnableClock(FLASH_BASE);
  FlashReg *flash = FLASH_BASE;
  flash->access = 0x00000004;
  flash->access = 0x00001804;
  flash->access = 0x00001804;
  flash->access = 0x00000604;

  // Enable the PLL.
  // We use the MSI clock as the source for the PLL
  // The MSI clock is running at its default frequency of
  // 4MHz.
  //
  // The PLL can generate several clocks with somewhat
  // less then descriptive names in the reference manual.
  // These clocks are:
  //   P clock - Used for the SAI peripherial.  Not used here
  //   Q clock - 48MHz output clock used for USB.  Not used here.
  //   R clock - This is the main system clock.  We care about this one.
  //
  // When configuring the PLL there are several constants programmed
  // into the PLL register to set the frequency of the internal VCO
  // These constants are called N and M in the reference manual:
  //
  // Fin = 4MHz
  // Fvco = Fin * (N/M)
  //
  // Legal range for Fvco is 96MHz to 344MHz according to the
  // data sheet.  I'll use 160MHz for Fvco and divide by 2
  // to get an 80MHz output clock
  //
  // See chapter 6 of the reference manual
  int N = 40;
  int M = 1;
  RCC_Regs *rcc = RCC_BASE;
  rcc->pllCfg = 0x01000001 | (N << 8) | ((M - 1) << 4);

  // Turn on the PLL
  rcc->clkCtrl |= 0x01000000;

  // Wait for the PLL ready indication
  while (!(rcc->clkCtrl & 0x02000000)) {
  }

  // Set PLL as system clock
  rcc->clkCfg = 0x00000003;

  // Use system clock as the A/D clock
  rcc->indClkCfg = 0x30000000;
}

/*
 * One time init of HAL.
 */
void HalApi::init() {
  // Init various components needed by the system.
  InitGPIO();
  InitSysTimer();
  InitADC();
  InitPwmOut();
  InitUARTs();
  watchdog_init();
  crc32_init();
  StepperMotorInit();
  Hal.enableInterrupts();
}

// Reset the processor
[[noreturn]] void HalApi::reset_device() {
  // Note that the system control registers are a standard ARM peripherial
  // they aren't documented in the normal STM32 reference manual, rather
  // they're in the processor programming manual.
  // The register we use to reset the system is called the
  // "Application interrupt and reset control register (AIRCR)"
  SysCtrl_Reg *sysCtl = SYSCTL_BASE;
  sysCtl->apInt = 0x05FA0004;

  // We promised we wouldn't return, so...
  while (true) {
  }
}

/******************************************************************
 * General Purpose I/O support.
 *
 * The following pins are used as GPIO on the rev-1 PCB
 *
 * Please refer to the PCB schematic as the ultimate source of which
 * pin is used for which function.  A less definitive, but perhaps
 * easier to read spreadsheet is availabe here:
 * https://docs.google.com/spreadsheets/d/1JOSQKxkQxXJ6MCMDI9PwUQ6kiuGdujR4D6EJN9u2LWg/edit#gid=0
 *
 * ID inputs.  These can be used to identify the PCB revision
 * we're running on.
 *  PB1  - ID0
 *  PA12 - ID1
 *
 * LED outputs.
 *  PC13 - red
 *  PC14 - yellow
 *  PC15 - green
 *
 * Solenoid
 *  PA11 - Note, this is also a timer pin so we may want to
 *         PWM it to reduce the solenoid voltage.
 *         For no I'm treating it as a digital output.
 *****************************************************************/
void HalApi::InitGPIO() {
  // See chapter 8 of the reference manual for details on GPIO

  // Enable all the GPIO clocks
  EnableClock(GPIO_A_BASE);
  EnableClock(GPIO_B_BASE);
  EnableClock(GPIO_C_BASE);
  EnableClock(GPIO_D_BASE);
  EnableClock(GPIO_E_BASE);
  EnableClock(GPIO_H_BASE);

  // Configure PCB ID pins as inputs.
  GPIO_PinMode(GPIO_B_BASE, 1, GPIO_PinMode::IN);
  GPIO_PinMode(GPIO_A_BASE, 12, GPIO_PinMode::IN);

  // Configure LED pins as outputs
  GPIO_PinMode(GPIO_C_BASE, 13, GPIO_PinMode::OUT);
  GPIO_PinMode(GPIO_C_BASE, 14, GPIO_PinMode::OUT);
  GPIO_PinMode(GPIO_C_BASE, 15, GPIO_PinMode::OUT);

  // Turn all three LEDs off initially
  GPIO_ClrPin(GPIO_C_BASE, 13);
  GPIO_ClrPin(GPIO_C_BASE, 14);
  GPIO_ClrPin(GPIO_C_BASE, 15);

  // Configure the solenoid and turn it off
  GPIO_PinMode(GPIO_A_BASE, 11, GPIO_PinMode::OUT);
  GPIO_ClrPin(GPIO_A_BASE, 11);
}

// Set or clear the specified digital output
void HalApi::digitalWrite(BinaryPin pin, VoltageLevel value) {
  auto [base, bit] = [&]() -> std::pair<GPIO_Regs *, int> {
    switch (pin) {
    case BinaryPin::SOLENOID:
      return {GPIO_A_BASE, 11};
    }
    // All cases covered above (and GCC checks this).
    __builtin_unreachable();
  }();

  switch (value) {
  case VoltageLevel::HIGH:
    GPIO_SetPin(base, bit);
    break;

  case VoltageLevel::LOW:
    GPIO_ClrPin(base, bit);
    break;
  }
}

/******************************************************************
 * System timer
 *
 * I use one of the basic timers (timer 6) for general system timing.
 * I configure it to count every 100ns and generate an interrupt
 * every millisecond
 *
 * The basic timers (like timer 6) are documented in chapter 29 of
 * the reference manual
 *****************************************************************/
void HalApi::InitSysTimer() {
  // Enable the clock to the timer
  EnableClock(TIMER6_BASE);

  // Just set the timer up to count every microsecond.
  TimerRegs *tmr = TIMER6_BASE;

  // The reload register gives the numer of clock ticks (100ns in our case) -1
  // until the clock wraps back to zero and generates an interrupt
  // This setting will cause an interrupt every 10,000 clocks or 1 millisecond
  tmr->reload = 9999;
  tmr->prescale = (CPU_FREQ_MHZ / 10 - 1);
  tmr->event = 1;
  tmr->ctrl[0] = 1;
  tmr->intEna = 1;

  EnableInterrupt(InterruptVector::TIMER6, IntPriority::STANDARD);
}

// Just spin for a specified number of microseconds
void HalApi::BusyWaitUsec(uint16_t usec) {
  constexpr uint16_t one_ms = 1000;
  while (usec > one_ms) {
    BusyWaitUsec(one_ms);
    usec = static_cast<uint16_t>(usec - one_ms);
  }

  TimerRegs *tmr = TIMER6_BASE;
  uint16_t start = static_cast<uint16_t>(tmr->counter);
  while (true) {
    uint16_t dt = static_cast<uint16_t>(tmr->counter - start);
    if (dt >= usec * 10)
      return;
  }
}

static void Timer6ISR() {
  TIMER6_BASE->status = 0;
  msCount++;
}

void HalApi::delay(Duration d) {
  int64_t start = msCount;
  while (msCount - start < d.milliseconds()) {
  }
}

Time HalApi::now() { return millisSinceStartup(msCount); }

/******************************************************************
 * Loop timer
 *
 * I use one of the timers (timer 15) to generate the interrupt
 * from which the control loop callback function is called.
 * This function runs at a higher priority then normal code,
 * but not as high as the hardware interrupts.
 *****************************************************************/
static void (*controller_callback)(void *);
static void *controller_arg;
void HalApi::startLoopTimer(const Duration &period, void (*callback)(void *),
                            void *arg) {
  controller_callback = callback;
  controller_arg = arg;

  // Find the loop period in clock cycles
  int32_t reload = static_cast<int32_t>(CPU_FREQ * period.seconds());
  int prescale = 1;

  // Adjust the prescaler so that my reload count will fit in the 16-bit
  // timer.
  if (reload > 65536) {
    prescale = static_cast<int>(reload / 65536.0) + 1;
    reload /= prescale;
  }

  // Enable the clock to the timer
  EnableClock(TIMER15_BASE);

  // Just set the timer up to count every microsecond.
  TimerRegs *tmr = TIMER15_BASE;
  tmr->reload = reload - 1;
  tmr->prescale = prescale - 1;
  tmr->event = 1;
  tmr->ctrl[0] = 1;
  tmr->intEna = 1;

  // Enable the interrupt that will call the controller
  // function callback periodically.
  // I'm using a lower priority then that which I use
  // for normal hardware interrupts.  This means that other
  // interrupts can be serviced while controller functions
  // are running.
  EnableInterrupt(InterruptVector::TIMER15, IntPriority::LOW);
}

static void Timer15ISR() {
  TIMER15_BASE->status = 0;

  // Call the function
  controller_callback(controller_arg);

  // Start sending any queued commands to the stepper motor
  StepMotor::StartQueuedCommands();
}

/******************************************************************
 * PWM outputs
 *
 * The following four outputs could be driven
 * as PWM outputs:
 *
 * PA8  - Timer 1 Channel 1 - heater control
 * PA11 - Timer 1 Channel 4 - solenoid
 * PB3  - Timer 2 Channel 2 - blower control
 * PB4  - Timer 3 Channel 1 - buzzer
 *
 * For now I'll just set up the blower since that's the only
 * one called out in the HAL
 *
 * These timers are documented in chapters 26 and 27 of the reference
 * manual.
 *****************************************************************/
void HalApi::InitPwmOut() {
  // The PWM frequency isn't mentioned anywhere that I can find, so
  // I'm just picking a reasonable number.  This can be refined later
  //
  // The selection of PWM frequency is a trade off between latency and
  // resolution.  Higher frequencies give lower latency and lower resoution.
  //
  // Latency is the time between setting the value and it taking effect,
  // this is essentially the PWM period (1/frequency).  For example, a
  // 20kHz frequency would give a latency of up to 50 usec.
  //
  // Resultion is based on the ratio of the clock frequency (80MHz) to the
  // PWM frequency.  For example, a 20kHz PWM would have a resolution of one
  // part in 4000 (80000000/20000) or about 12 bits.
  const int pwmFreqHz = 20000;

  EnableClock(TIMER2_BASE);

  // Connect PB3 to timer 2
  GPIO_PinAltFunc(GPIO_B_BASE, 3, 1);

  TimerRegs *tmr = TIMER2_BASE;

  // Set the frequency
  tmr->reload = (CPU_FREQ / pwmFreqHz) - 1;

  // Configure channel 2 in PWM output mode 1
  // with preload enabled.  The preload means that
  // the new PWM duty cycle gets written to a shadow
  // register and copied to the active register
  // at the start of the next cycle.
  tmr->ccMode[0] = 0x6800;

  tmr->ccEnable = 0x10;

  // Start with 0% duty cycle
  tmr->compare[1] = 0;

  // Load the shadow registers
  tmr->event = 1;

  // Start the counter
  tmr->ctrl[0] = 0x81;
}

// Set the PWM period.
void HalApi::analogWrite(PwmPin pin, float duty) {
  auto [tmr, chan] = [&]() -> std::pair<TimerRegs *, int> {
    switch (pin) {
    case PwmPin::BLOWER:
      return {TIMER2_BASE, 1};
    }
    // All cases covered above (and GCC checks this).
    __builtin_unreachable();
  }();

  tmr->compare[chan] = static_cast<REG>(static_cast<float>(tmr->reload) * duty);
}

/******************************************************************
 * Serial port to GUI
 * Chapter 38 of the reference manual defines the USART registers.
 *****************************************************************/

class UART {
  CircBuff<uint8_t, 128> rxDat;
  CircBuff<uint8_t, 128> txDat;
  UART_Regs *const reg;

public:
  explicit UART(UART_Regs *const r) : reg(r) {}

  void Init(int baud) {
    // Set baud rate register
    reg->baud = CPU_FREQ / baud;

    reg->ctrl1.s.rxneie = 1; // enable receive interrupt
    reg->ctrl1.s.te = 1;     // enable transmitter
    reg->ctrl1.s.re = 1;     // enable receiver
    reg->ctrl1.s.ue = 1;     // enable uart
  }

  // This is the interrupt handler for the UART.
  void ISR() {
    // Check for overrun error and framing errors.  Clear those errors if
    // they're set to avoid further interrupts from them.
    if (reg->status.s.fe) {
      reg->intClear.s.fecf = 1;
    }
    if (reg->status.s.ore) {
      reg->intClear.s.orecf = 1;
    }

    // See if we received a new byte.
    if (reg->status.s.rxne) {
      // Add the byte to rxDat.  If the buffer is full, we'll drop it -- what
      // else can we do?
      //
      // TODO: Perhaps log a warning here so we have an idea of whether this
      // buffer is hitting capacity frequently.
      (void)rxDat.Put(static_cast<uint8_t>(reg->rxDat));
    }

    // Check for transmit data register empty
    if (reg->status.s.txe && reg->ctrl1.s.txeie) {
      std::optional<uint8_t> ch = txDat.Get();

      // If there's nothing left in the transmit buffer,
      // just disable further transmit interrupts.
      if (ch == std::nullopt) {
        reg->ctrl1.s.txeie = 0;
      } else {
        // Otherwise, Send the next byte.
        reg->txDat = *ch;
      }
    }
  }

  // Read up to len bytes and store them in the passed buffer.
  // This function does not block, so if less then len bytes
  // are available it will only return the available bytes
  // Returns the number of bytes actually read.
  uint16_t read(char *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
      std::optional<uint8_t> ch = rxDat.Get();
      if (ch == std::nullopt) {
        return i;
      }
      *buf++ = *ch;
    }

    // Note that we don't need to enable the rx interrupt
    // here.  That one is always enabled.
    return len;
  }

  // Write up to len bytes to the buffer.
  // This function does not block, so if there isn't enough
  // space to write len bytes, then only a partial write
  // will occur.
  // The number of bytes actually written is returned.
  uint16_t write(const char *buf, uint16_t len) {
    uint16_t i;
    for (i = 0; i < len; i++) {
      if (!txDat.Put(*buf++))
        break;
    }

    // Enable the tx interrupt.  If there was already anything
    // in the buffer this will already be enabled, but enabling
    // it again doesn't hurt anything.
    reg->ctrl1.s.txeie = 1;
    return i;
  }

  // Return the number of bytes currently in the
  // receive buffer and ready to be read.
  uint16_t RxFull() { return static_cast<uint16_t>(rxDat.FullCt()); }

  // Returns the number of free locations in the
  // transmit buffer.
  uint16_t TxFree() { return static_cast<uint16_t>(txDat.FreeCt()); }
};

static UART rpUART(UART3_BASE);
static UART dbgUART(UART2_BASE);
#ifdef UART_VIA_DMA
extern UART_DMA dmaUART;
#endif
// The UART that talks to the rPi uses the following pins:
//    PB10 - TX
//    PB11 - RX
//    PB13 - RTS
//    PB14 - CTS
//
// The Nucleo board also includes a secondary serial port that's
// indirectly connected to its USB connector.  This port is
// connected to the STM32 UART2 at pins:
//    PA2 - TX
//    PA3 - RX
//
// Please refer to the PCB schematic as the ultimate source of which
// pin is used for which function.  A less definitive, but perhaps
// easier to read spreadsheet is availabe here:
// https://docs.google.com/spreadsheets/d/1JOSQKxkQxXJ6MCMDI9PwUQ6kiuGdujR4D6EJN9u2LWg/edit#gid=0
//
// These pins are connected to UART3
// The UART is described in chapter 38 of the reference manual
void HalApi::InitUARTs() {
  // NOTE - The UART functionality hasn't been tested due to lack of hardware!
  //        Need to do that as soon as the boards are available.
  EnableClock(UART2_BASE);
  EnableClock(UART3_BASE);
#ifdef UART_VIA_DMA
  EnableClock(DMA1_BASE);
#endif
  GPIO_PinAltFunc(GPIO_A_BASE, 2, 7);
  GPIO_PinAltFunc(GPIO_A_BASE, 3, 7);

  GPIO_PinAltFunc(GPIO_B_BASE, 10, 7);
  GPIO_PinAltFunc(GPIO_B_BASE, 11, 7);
  GPIO_PinAltFunc(GPIO_B_BASE, 13, 7);
  GPIO_PinAltFunc(GPIO_B_BASE, 14, 7);

#ifdef UART_VIA_DMA
  dmaUART.init(115200);
#else
  rpUART.Init(115200);
#endif
  dbgUART.Init(115200);

  EnableInterrupt(InterruptVector::DMA1_CH2, IntPriority::STANDARD);
  EnableInterrupt(InterruptVector::DMA1_CH3, IntPriority::STANDARD);
  EnableInterrupt(InterruptVector::UART2, IntPriority::STANDARD);
  EnableInterrupt(InterruptVector::UART3, IntPriority::STANDARD);
}

static void UART2_ISR() { dbgUART.ISR(); }

#ifndef UART_VIA_DMA
void UART3_ISR() { rpUART.ISR(); }
#endif

uint16_t HalApi::serialRead(char *buf, uint16_t len) {
  return rpUART.read(buf, len);
}

uint16_t HalApi::serialBytesAvailableForRead() { return rpUART.RxFull(); }

uint16_t HalApi::serialWrite(const char *buf, uint16_t len) {
  return rpUART.write(buf, len);
}

uint16_t HalApi::serialBytesAvailableForWrite() { return rpUART.TxFree(); }

uint16_t HalApi::debugWrite(const char *buf, uint16_t len) {
  return dbgUART.write(buf, len);
}

uint16_t HalApi::debugRead(char *buf, uint16_t len) {
  return dbgUART.read(buf, len);
}

/******************************************************************
 * Watchdog timer (see chapter 32 of reference manual).
 *
 * The watchdog timer will reset the system if it hasn't been
 * re-initialized within a specific amount of time.  It's used
 * to catch bugs that would otherwise hang the system.  When
 * the watchdog is enabled such a bug will reset the system
 * rather then let it hang indefinitely.
 *****************************************************************/
void HalApi::watchdog_init() {
  Watchdog_Regs *wdog = WATCHDOG_BASE;

  // Enable the watchdog timer by writing the appropriate value to its key
  // register
  wdog->key = 0xCCCC;

  // Enable register access
  wdog->key = 0x5555;

  // Set the pre-scaler to 0.  That setting will cause the watchdog
  // clock to be updated at approximately 8KHz.
  wdog->prescale = 0;

  // The reload value gives the number of clock cycles before the
  // watchdog timer times out.  I'll set it to 2000 which gives
  // us about 250ms before a reset.
  wdog->reload = 2000;

  // Since the watchdog timer runs off its own clock which is pretty
  // slow, it takes a little time for the registers to actually get
  // updated.  I wait for the status register to go to zero which
  // means its done.
  while (wdog->status) {
  }

  // Reset the timer.  This also locks the registers again.
  wdog->key = 0xAAAA;
}

// Pet the watchdog so it doesn't bite us.
void HalApi::watchdog_handler() {
  Watchdog_Regs *wdog = WATCHDOG_BASE;
  wdog->key = 0xAAAA;
}

void HalApi::crc32_init() {
  RCC_Regs *rcc = RCC_BASE;
  // Enable clock to CRC32
  rcc->periphClkEna[0] |= (1 << 12);
  // Pull CRC32 peripheral out of reset if it ever was
  rcc->periphReset[0] &= ~(1 << 12);

  CRC_Regs *crc = CRC_BASE;
  crc->init = 0xFFFFFFFF;
  crc->poly = CRC32_POLYNOMIAL;
  crc->ctrl = 1;
}

void HalApi::crc32_accumulate(uint8_t d) {
  CRC_Regs *crc = CRC_BASE;
  crc->data = static_cast<uint32_t>(d);
}

uint32_t HalApi::crc32_get() {
  // CRC32 peripheral takes 4 clock cycles to produce a result after the last
  // write to it.  These nops are just in case of some spectacular compiler
  // optimization that would result in querying too early.
  //
  // TODO(jlebar): I think the nops likely are not necessary. The chip should
  // stall the processor if the data isn't available.  Moreover if it *is*
  // necessary, asm volatile may not be enough of a memory barrier; we may need
  // to say that these instructions also clobber "memory".
  asm volatile("nop");
  asm volatile("nop");
  asm volatile("nop");
  asm volatile("nop");
  CRC_Regs *crc = CRC_BASE;
  return crc->data;
}

void HalApi::crc32_reset() {
  CRC_Regs *crc = CRC_BASE;
  crc->ctrl = 1;
}

uint32_t HalApi::crc32(uint8_t *data, uint32_t length) {
  crc32_reset();
  while (length--) {
    crc32_accumulate(*data++);
  }
  return crc32_get();
}

// Enable clocks to a specific peripherial.
// On the STM32 the clocks going to various peripherials on the chip
// are individually selectable and for the most part disabled on startup.
// Clocks to the specific peripherials need to be enabled through the
// RCC (Reset and Clock Controller) module before the peripherial can be
// used.
// Pass in the base address of the peripherial to enable its clock
void HalApi::EnableClock(void *ptr) {
  static struct {
    void *base;
    int ndx;
    int bit;
  } rccInfo[] = {
      {DMA1_BASE, 0, 0},     {DMA2_BASE, 0, 1},   {FLASH_BASE, 0, 8},
      {GPIO_A_BASE, 1, 0},   {GPIO_B_BASE, 1, 1}, {GPIO_C_BASE, 1, 2},
      {GPIO_D_BASE, 1, 3},   {GPIO_E_BASE, 1, 4}, {GPIO_H_BASE, 1, 7},
      {ADC_BASE, 1, 13},     {TIMER2_BASE, 4, 0}, {TIMER6_BASE, 4, 4},
      {UART2_BASE, 4, 17},   {UART3_BASE, 4, 18}, {SPI1_BASE, 6, 12},
      {TIMER15_BASE, 6, 16},

      // The following entries are probably correct, but have
      // not been tested yet.  When adding support for one of
      // these peripherials just comment out the line.  And
      // test of course.
      //      {CRC_BASE, 0, 12},
      //      {TIMER3_BASE, 4, 1},
      //      {SPI2_BASE, 4, 14},
      //      {SPI3_BASE, 4, 15},
      //      {UART4_BASE, 4, 19},
      //      {I2C1_BASE, 4, 21},
      //      {I2C2_BASE, 4, 22},
      //      {I2C3_BASE, 4, 23},
      //      {I2C4_BASE, 5, 1},
      //      {TIMER1_BASE, 6, 11},
      //      {UART1_BASE, 6, 14},
      //      {TIMER16_BASE, 6, 17},
  };

  // I don't include all the peripherials here, just the ones that we currently
  // use or seem likely to be used in the future.  To add more peripherials,
  // just look up the appropriate bit in the reference manual RCC chapter.
  int ndx = -1;
  int bit = 0;
  for (auto &info : rccInfo) {
    if (ptr == info.base) {
      ndx = info.ndx;
      bit = info.bit;
      break;
    }
  }

  // If the input address wasn't found then its definitly
  // a bug.  I'll just loop forever here causing the code
  // to crash.  That should make it easier to find the
  // bug during development.
  if (ndx < 0) {
    Hal.disableInterrupts();
    while (true) {
    }
  }

  // Enable the clock of the requested peripherial
  RCC_Regs *rcc = RCC_BASE;
  rcc->periphClkEna[ndx] |= (1 << bit);
}

static void StepperISR() { StepMotor::DMA_ISR(); }

/******************************************************************
 * Interrupt vector table.  The interrupt vector table is a list of
 * pointers to the various interrupt functions.  It is stored at the
 * very start of the flash memory.
 *****************************************************************/

// Fault handlers
static void fault() {
  while (true) {
  }
}

static void NMI() { fault(); }
static void FaultISR() { fault(); }
static void MPUFaultISR() { fault(); }
static void BusFaultISR() { fault(); }
static void UsageFaultISR() { fault(); }
static void BadISR() { fault(); }

extern "C" void Reset_Handler();
__attribute__((used))
__attribute__((section(".isr_vector"))) void (*const vectors[101])() = {
    // The first entry of the ISR holds the initial value of the
    // stack pointer.  The ARM processor initializes the stack
    // pointer based on this address.
    reinterpret_cast<void (*)()>(&systemStack[SYSTEM_STACK_SIZE]),

    // The second ISR entry is the reset vector which is an
    // assembly language routine that does some basic memory
    // initilization and then calls main().
    // Note that the LSB of the reset vector needs to be set
    // (hence the +1 below).  This tells the ARM that this is
    // thumb code.  The cortex m4 processor only supports
    // thumb code, so this will always be set or we'll get
    // a hard fault.
    reinterpret_cast<void (*)()>(reinterpret_cast<uintptr_t>(Reset_Handler) +
                                 1),

    // The rest of the table is a list of exception and
    // interrupt handlers.  Chapter 12 (NVIC) of the reference
    // manual gives a listing of the vector table offsets.
    NMI,           //   2 - 0x008 The NMI handler
    FaultISR,      //   3 - 0x00C The hard fault handler
    MPUFaultISR,   //   4 - 0x010 The MPU fault handler
    BusFaultISR,   //   5 - 0x014 The bus fault handler
    UsageFaultISR, //   6 - 0x018 The usage fault handler
    BadISR,        //   7 - 0x01C Reserved
    BadISR,        //   8 - 0x020 Reserved
    BadISR,        //   9 - 0x024 Reserved
    BadISR,        //  10 - 0x028 Reserved
    BadISR,        //  11 - 0x02C SVCall handler
    BadISR,        //  12 - 0x030 Debug monitor handler
    BadISR,        //  13 - 0x034 Reserved
    BadISR,        //  14 - 0x038 The PendSV handler
    BadISR,        //  15 - 0x03C SysTick
    BadISR,        //  16 - 0x040
    BadISR,        //  17 - 0x044
    BadISR,        //  18 - 0x048
    BadISR,        //  19 - 0x04C
    BadISR,        //  20 - 0x050
    BadISR,        //  21 - 0x054
    BadISR,        //  22 - 0x058
    BadISR,        //  23 - 0x05C
    BadISR,        //  24 - 0x060
    BadISR,        //  25 - 0x064
    BadISR,        //  26 - 0x068
    BadISR,        //  27 - 0x06C
#ifdef UART_VIA_DMA
    DMA1_CH2_ISR, //  28 - 0x070 DMA1 CH2
    DMA1_CH3_ISR, //  29 - 0x074 DMA1 CH3
#else
    BadISR, //  28 - 0x070
    BadISR, //  29 - 0x074
#endif
    BadISR,     //  30 - 0x078
    BadISR,     //  31 - 0x07C
    BadISR,     //  32 - 0x080
    BadISR,     //  33 - 0x084
    BadISR,     //  34 - 0x088
    BadISR,     //  35 - 0x08C
    BadISR,     //  36 - 0x090
    BadISR,     //  37 - 0x094
    BadISR,     //  38 - 0x098
    BadISR,     //  39 - 0x09C
    Timer15ISR, //  40 - 0x0A0
    BadISR,     //  41 - 0x0A4
    BadISR,     //  42 - 0x0A8
    BadISR,     //  43 - 0x0AC
    BadISR,     //  44 - 0x0B0
    BadISR,     //  45 - 0x0B4
    BadISR,     //  46 - 0x0B8
    BadISR,     //  47 - 0x0BC
    BadISR,     //  48 - 0x0C0
    BadISR,     //  49 - 0x0C4
    BadISR,     //  50 - 0x0C8
    BadISR,     //  51 - 0x0CC
    BadISR,     //  52 - 0x0D0
    BadISR,     //  53 - 0x0D4
    UART2_ISR,  //  54 - 0x0D8
    UART3_ISR,  //  55 - 0x0DC
    BadISR,     //  56 - 0x0E0
    BadISR,     //  57 - 0x0E4
    BadISR,     //  58 - 0x0E8
    BadISR,     //  59 - 0x0EC
    BadISR,     //  60 - 0x0F0
    BadISR,     //  61 - 0x0F4
    BadISR,     //  62 - 0x0F8
    BadISR,     //  63 - 0x0FC
    BadISR,     //  64 - 0x100
    BadISR,     //  65 - 0x104
    BadISR,     //  66 - 0x108
    BadISR,     //  67 - 0x10C
    BadISR,     //  68 - 0x110
    BadISR,     //  69 - 0x114
    Timer6ISR,  //  70 - 0x118
    BadISR,     //  71 - 0x11C
    BadISR,     //  72 - 0x120
    BadISR,     //  73 - 0x124
    StepperISR, //  74 - 0x128
    BadISR,     //  75 - 0x12C
    BadISR,     //  76 - 0x130
    BadISR,     //  77 - 0x134
    BadISR,     //  78 - 0x138
    BadISR,     //  79 - 0x13C
    BadISR,     //  80 - 0x140
    BadISR,     //  81 - 0x144
    BadISR,     //  82 - 0x148
    BadISR,     //  83 - 0x14C
    BadISR,     //  84 - 0x150
    BadISR,     //  85 - 0x154
    BadISR,     //  86 - 0x158
    BadISR,     //  87 - 0x15C
    BadISR,     //  88 - 0x160
    BadISR,     //  89 - 0x164
    BadISR,     //  90 - 0x168
    BadISR,     //  91 - 0x16C
    BadISR,     //  92 - 0x170
    BadISR,     //  93 - 0x174
    BadISR,     //  94 - 0x178
    BadISR,     //  95 - 0x17C
    BadISR,     //  96 - 0x180
    BadISR,     //  97 - 0x184
    BadISR,     //  98 - 0x188
    BadISR,     //  99 - 0x18C
    BadISR,     // 100 - 0x190
};

// Enable an interrupt with a specified priority (0 to 15)
// See the NVIC chapter of the manual for more information.
void HalApi::EnableInterrupt(InterruptVector vec, IntPriority pri) {
  IntCtrl_Regs *nvic = NVIC_BASE;

  int addr = static_cast<int>(vec);

  int id = addr / 4 - 16;

  nvic->setEna[id >> 5] = 1 << (id & 0x1F);

  // The STM32 processor implements bits 4-7 of the NVIM priority register.
  int p = static_cast<int>(pri);
  nvic->priority[id] = static_cast<BREG>(p << 4);
}

#endif
