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

#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <stdint.h>

// The polynomial 0x741B8CD7 has Hamming distance 6 up to 16360 bits
// and Hamming distance 4 up to 114663 bits.
//[Philip Koopman, 32-Bit Cyclic Redundancy Codes for Internet Applications
// 2002.] https://users.ece.cmu.edu/~koopman/crc/
inline constexpr uint32_t CRC32_POLYNOMIAL = 0x741B8CD7;

uint32_t soft_crc32_single(uint32_t crc, uint8_t data);
uint32_t soft_crc32(const char *data, uint32_t count);

#endif // CHECKSUM_H
