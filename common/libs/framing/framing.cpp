#include "framing.h"

#include <stdint.h>

inline bool shouldEscape(uint8_t b) {
  return FRAMING_MARK == b || FRAMING_ESC == b;
}

uint32_t EncodeFrame(uint8_t *source, uint32_t sourceLength, uint8_t *dest,
                     uint32_t destLength) {
  uint32_t i = 0;
  dest[i++] = FRAMING_MARK;
  for (uint32_t j = 0; j < sourceLength; j++) {
    if (shouldEscape(source[j])) {
      dest[i++] = FRAMING_ESC;
      dest[i++] = source[j] ^ 0x20;
    } else {
      dest[i++] = source[j];
    }
    if (i >= destLength) {
      return 0;
    }
  }
  dest[i++] = FRAMING_MARK;
  return i;
}

uint32_t DecodeFrame(uint8_t *source, uint32_t sourceLength, uint8_t *dest,
                     uint32_t destLength) {
  uint32_t i = 0;
  bool isEsc = false;
  for (uint32_t j = 0; j < sourceLength; j++) {
    switch (source[j]) {
    case FRAMING_MARK:
      break;
    case FRAMING_ESC:
      isEsc = true;
      break;
    default:
      if (i >= destLength) {
        return 0;
      }
      if (isEsc) {
        isEsc = false;
        dest[i++] = source[j] ^ 0x20;
      } else {
        dest[i++] = source[j];
      }
      break;
    }
  }
  return i;
}
