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

#ifndef VARS_H
#define VARS_H

#include "debug.h"

// This class represents a variable that you can read/write using the
// debug serial port.
//
// We give each such variable a name which the debugger command line will
// use to access it.  We can also link it with a C++ variable who's value
// it will read or write.
class DebugVar {
public:
  // 32-bit integer variable.  The default get/set functions will probably be
  // fine.
  DebugVar(const char *name, int32_t *data, const char *help = "");
  DebugVar(const char *name, uint32_t *data, const char *help = "");
  DebugVar(const char *name, float *data, const char *help = "");

#if 0
  // Gets the current value of the variable.
  // @param buff The variable's value is stored here
  // @param len Return the length (in bytes) of the value here
  // @param max Maximum number of bytes we can write to buff
  // @return An error code, 0 on success
  virtual DbgErrCode GetValue(uint8_t *buff, int *len, int max);

  // Sets the current value of the variable.
  // @param buff Holds the data to be written to the variable
  // @param len Number of valid bytes in buffer
  // @return An error code, 0 on success
  virtual DbgErrCode SetValue(uint8_t *buff, int len);

  // Returns a representation of this variable as a 32-bit integer.
  // This is used when capturing the variables value to the trace buffer.
  // Only variables that can be represented by 32-bits can be traced.
  // For floats, this returns the raw bits of the float, the Python
  // program handles converting it back into a float.
  virtual uint32_t getDataForTrace();

  const char *getName() const { return name; }
  const char *getFormat() const { return fmt; }
  const char *getHelp() const { return help; }
  VarType getType() const { return type; }

  static DebugVar *findVar(uint16_t vid) {
    if (vid >= ARRAY_CT(varList))
      return 0;
    return varList[vid];
  }

private:
  VarType type;
  const char *name;
  const char *help;
  void *addr;

  // List of all the variables in the system.
  // Increase size as necessary
  static DebugVar *varList[100];
  static int varCount;

  void RegisterVar() {
    if (varCount < static_cast<int>(ARRAY_CT(varList)))
      varList[varCount++] = this;
  }
#endif
};

#endif
