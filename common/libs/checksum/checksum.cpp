/* Copyright 2020, Edwin Chiu

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

#include "checksum.h"
#include <stdint.h>

// Table generated using
// http://www.sunshine2k.de/coding/javascript/crc/crc_js.html
uint32_t soft_crc32_single(uint32_t crc, uint8_t data) {
  // Nibble lookup table for 0x741B8CD7 polynomial
  static const uint32_t crcTable[16] = {
      0x00000000, 0x741B8CD7, 0xE83719AE, 0x9C2C9579, 0xA475BF8B, 0xD06E335C,
      0x4C42A625, 0x38592AF2, 0x3CF0F3C1, 0x48EB7F16, 0xD4C7EA6F, 0xA0DC66B8,
      0x98854C4A, 0xEC9EC09D, 0x70B255E4, 0x04A9D933};

  // Apply all 32-bits
  crc = crc ^ static_cast<uint32_t>(data);

  // Process 32-bits, 4 at a time, or 8 rounds
  for (int i = 0; i < 8; i++) {
    crc = (crc << 4) ^ crcTable[crc >> 28];
  }
  return crc;
}

uint32_t soft_crc32(const char *data, uint32_t count) {
  if (0 == count) {
    return 0;
  }

  uint32_t crc = 0xFFFFFFFF;
  while (count--) {
    crc = soft_crc32_single(crc, *data++);
  }
  return crc;
}
