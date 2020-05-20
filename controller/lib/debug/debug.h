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
*/

#ifndef DEBUG_H
#define DEBUG_H

#include "circular_buffer.h"
#include "debug.pb.h"
#include <stdint.h>

// Singleton class which implements the debug serial port handler.
// TODO: Rename to 'Debug'.
// TODO: Make constructor private.
class DebugSerial {
public:
  // Called from the main loop to handle debug commands.
  void Poll();

  // printf style function to print data to a "virtual console".
  //
  // Data printed this way can be retrieved via the DebugReadPrintBuf command.
  //
  // Returns number of bytes written.
  int Print(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

private:
  void HandlePeek(const DebugPeekRequest &);
  void HandlePoke(const DebugPokeRequest &);
  void HandleReadPrintBuf(const DebugReadPrintBufRequest &);
  void HandleReadVars(const DebugReadVarsRequest &);
  void HandleWriteVar(const DebugWriteVarRequest &);
  void HandleTrace(const DebugTraceRequest &);

  CircBuff<uint8_t, 2048> printBuf;
};
inline DebugSerial debug;

#if 0
// States for the internal state machine
enum class DbgPollState { WAIT_CMD, PROCESS_CMD, SEND_RESP };

// The binary serial interface uses two special characters
// These values are pretty arbitrary.
// See debug.cpp for a detailed description of how this works
enum class DbgSpecial { ESC = 0xf1, TERM = 0xf2 };

enum class DbgCmdCode {
  MODE = 0x00,            // Return the current firmware mode
  PEEK = 0x01,            // Peek into RAM
  POKE = 0x02,            // Poke values into RAM
  PRINT_BUFF_READ = 0x03, // Read strings from the print buffer
  VAR = 0x04,             // Variable access
  TRACE = 0x05,           // Data trace commands
};

enum class DbgErrCode {
  OK = 0x00,           // success
  CRC_ERR = 0x01,      // CRC error on command
  BAD_CMD = 0x02,      // Unknown command code received
  MISSING_DATA = 0x03, // Not enough data passed with command
  NO_MEMORY = 0x04,    // Insufficient memory
  INTERNAL = 0x05,     // Some type of interal error (aka bug)
  BAD_VARID = 0x06,    // The requested variable ID is invalid
  RANGE = 0x07,        // data out of range
};

// Each debug command is represented by an instance of this
// virtual class
class DebugCmd {
  friend class DebugSerial;
  static DebugCmd *cmdList[256];

public:
  DebugCmd(DbgCmdCode opcode);

  // Command handler.
  //   data - Buffer holding command data on entry.
  //   The response data should be written here.
  //
  //   len - Holds the data length on entry and response
  //   length on return.
  //
  //   max - Maximum number of bytes that can be written to data
  //
  //   Returns an error code.  For any non-zero error, the values
  //   returned in len and data will be ignored.
  virtual DbgErrCode HandleCmd(uint8_t *data, int *len, int max) {
    return DbgErrCode::BAD_CMD;
  }
};

// Singleton class which implements the debug serial port handler.
class DebugSerial {
public:
  DebugSerial();

  // This function is called from the main loop to handle
  // debug commands
  void Poll();

  // Printf style function to print data to a virtual
  // console.
  int Print(const char *fmt, ...);

  // Read a byte from the print buffer.
  // This is only intended to be called from the command that returns
  // print buffer data.
  bool PrintBuffGet(uint8_t *ch) { return printBuff.Get(ch); }

private:
  CircBuff<uint8_t, 2000> printBuff;

  uint8_t cmdBuff[500];
  int buffNdx;
  int respLen;
  DbgPollState pollState;
  bool prevCharEsc;

  bool ReadNextByte();
  void ProcessCmd();
  bool SendNextByte();

  uint16_t CalcCRC(uint8_t *buff, int len);
  void SendError(DbgErrCode err);
};
extern DebugSerial debug;
#endif

#endif
