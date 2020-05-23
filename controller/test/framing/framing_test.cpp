#include "framing.h"

#include "checksum.h"
#include "network_protocol.pb.h"
#include "gtest/gtest.h"
#include <stdint.h>
#include <stdio.h>

void randomConversion() {
  constexpr uint32_t SLENGTH = 150;
  constexpr uint32_t DLENGTH = (SLENGTH + 4) * 2 + 2 + 1;
  uint8_t source_buf[SLENGTH];
  uint8_t dest_buf[DLENGTH];
  uint8_t decoded_buf[SLENGTH];
  for (unsigned int i = 0; i < SLENGTH; i++) {
    source_buf[i] = static_cast<uint8_t>(rand() % 255);
  }

  uint32_t frameLength = EscapeFrame(source_buf, SLENGTH, dest_buf, DLENGTH);
  ASSERT_GT(frameLength, SLENGTH);
  uint32_t decodedLength =
      UnescapeFrame(dest_buf, frameLength, decoded_buf, SLENGTH);
  ASSERT_EQ(decodedLength, SLENGTH);
  int n = memcmp(source_buf, decoded_buf, SLENGTH);
  ASSERT_EQ(n, 0);
}

TEST(FramingTests, RandomBuffer) {
  for (int i = 0; i < 1; i++) {
    randomConversion();
  }
}

TEST(FramingTests, EncodingDestTooSmall) {
  uint8_t source_buf[] = {0, 1, 2, 3};
  uint8_t dest_buf[20];

  uint32_t frameLength =
      EscapeFrame(source_buf, sizeof(source_buf), dest_buf, 5);
  ASSERT_EQ(frameLength, (uint32_t)0);
  frameLength = EscapeFrame(source_buf, sizeof(source_buf), dest_buf, 6);
  ASSERT_GT(frameLength, (uint32_t)0);

  uint8_t source_buf2[] = {0, FRAMING_ESC, 1, FRAMING_MARK, 2, 3};

  frameLength = EscapeFrame(source_buf2, sizeof(source_buf2), dest_buf, 7);
  ASSERT_EQ(frameLength, (uint32_t)0);
  frameLength = EscapeFrame(source_buf2, sizeof(source_buf2), dest_buf, 10);
  ASSERT_GT(frameLength, (uint32_t)0);
}

TEST(FramingTests, DecodingDestTooSmall) {
  uint8_t source_buf[] = {FRAMING_MARK, 0, 1, 2, 3, FRAMING_MARK};
  uint8_t dest_buf[10];

  uint32_t frameLength =
      UnescapeFrame(source_buf, sizeof(source_buf), dest_buf, 3);
  ASSERT_EQ(frameLength, (uint32_t)0);
  frameLength = UnescapeFrame(source_buf, sizeof(source_buf), dest_buf, 4);
  ASSERT_GT(frameLength, (uint32_t)0);
}

static constexpr auto EncodeGuiStatusFrame =
    EncodeFrame<GuiStatus, GuiStatus_fields, GuiStatus_size, &soft_crc32>;

static auto DecodeGuiStatusFrame =
    DecodeFrame<GuiStatus, GuiStatus_fields, &soft_crc32>;

static auto EncodeControllerStatusFrame =
    EncodeFrame<ControllerStatus, ControllerStatus_fields,
                ControllerStatus_size, &soft_crc32>;

static constexpr auto DecodeControllerStatusFrame =
    DecodeFrame<ControllerStatus, ControllerStatus_fields, &soft_crc32>;

TEST(FramingTests, ControllerStatusCoding) {
  ControllerStatus s = ControllerStatus_init_zero;
  s.uptime_ms = 42;
  s.active_params.mode = VentMode_PRESSURE_CONTROL;
  s.active_params.peep_cm_h2o = 10;
  s.active_params.breaths_per_min = 15;
  s.active_params.pip_cm_h2o = 1;
  s.active_params.inspiratory_expiratory_ratio = 2;
  s.active_params.rise_time_ms = 100;
  s.active_params.inspiratory_trigger_cm_h2o = 5;
  s.active_params.expiratory_trigger_ml_per_min = 9;
  // Set very large values here because they take up more space in the encoded
  // proto, and our goal is to make it big.
  s.active_params.alarm_lo_tidal_volume_ml =
      std::numeric_limits<uint32_t>::max();
  s.active_params.alarm_hi_tidal_volume_ml =
      std::numeric_limits<uint32_t>::max();
  s.active_params.alarm_lo_breaths_per_min =
      std::numeric_limits<uint32_t>::max();
  s.active_params.alarm_hi_breaths_per_min =
      std::numeric_limits<uint32_t>::max();
  s.sensor_readings.patient_pressure_cm_h2o = 11;
  s.sensor_readings.volume_ml = 800;
  s.sensor_readings.flow_ml_per_min = 1000;

  uint8_t buf[(ControllerStatus_size + 4) * 2 + 2];
  uint32_t encoded_length = EncodeControllerStatusFrame(s, buf, sizeof(buf));
  ASSERT_GT(encoded_length, (uint32_t)0);
  ControllerStatus decoded = ControllerStatus_init_zero;
  DecodeResult r = DecodeControllerStatusFrame(buf, encoded_length, &decoded);
  ASSERT_EQ(r, 0);
  EXPECT_EQ(s.uptime_ms, decoded.uptime_ms);
  EXPECT_EQ(s.active_params.mode, decoded.active_params.mode);
  EXPECT_EQ(s.active_params.peep_cm_h2o, decoded.active_params.peep_cm_h2o);
  EXPECT_EQ(s.sensor_readings.patient_pressure_cm_h2o,
            decoded.sensor_readings.patient_pressure_cm_h2o);
}

TEST(FramingTests, GuiStatusCoding) {
  GuiStatus s = GuiStatus_init_zero;
  s.uptime_ms = std::numeric_limits<uint32_t>::max() / 2;
  s.desired_params.mode = VentMode_PRESSURE_CONTROL;
  s.desired_params.peep_cm_h2o = 10;
  s.desired_params.breaths_per_min = 15;
  s.desired_params.pip_cm_h2o = 1;
  s.desired_params.inspiratory_expiratory_ratio = 2;
  s.desired_params.rise_time_ms = 100;
  s.desired_params.inspiratory_trigger_cm_h2o = 5;
  s.desired_params.expiratory_trigger_ml_per_min = 9;
  // Set very large values here because they take up more space in the encoded
  // proto, and our goal is to make it big.
  s.desired_params.alarm_lo_tidal_volume_ml =
      std::numeric_limits<uint32_t>::max();
  s.desired_params.alarm_hi_tidal_volume_ml =
      std::numeric_limits<uint32_t>::max();
  s.desired_params.alarm_lo_breaths_per_min =
      std::numeric_limits<uint32_t>::max();
  s.desired_params.alarm_hi_breaths_per_min =
      std::numeric_limits<uint32_t>::max();

  uint8_t buf[(GuiStatus_size + 4) * 2 + 2];
  uint32_t encoded_length = EncodeGuiStatusFrame(s, buf, sizeof(buf));
  ASSERT_GT(encoded_length, (uint32_t)0);
  GuiStatus decoded = GuiStatus_init_zero;
  DecodeResult r = DecodeGuiStatusFrame(buf, encoded_length, &decoded);
  ASSERT_EQ(r, 0);

  EXPECT_EQ(s.uptime_ms, decoded.uptime_ms);
  EXPECT_EQ(s.desired_params.mode, decoded.desired_params.mode);
}
