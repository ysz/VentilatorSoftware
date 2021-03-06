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

/*
 * module contributors: verityRF
 *
 * The purpose of this module is to provide unit testing for the sensors
 * controller module.  Module is to be run on an x86 host and is not to be run
 * on an Arduino platform.
 */

#include "hal.h"
#include "sensors.h"
#include "gtest/gtest.h"
#include <assert.h>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

// Maximum allowable delta between calculated sensor readings and the input
// pressure waveform [kPa]
static const float COMPARISON_TOLERANCE_PRESSURE_KPA = 0.005f;

// Maximum allowable delta between calculated and actual volumetric flow
static const float COMPARISON_TOLERANCE_FLOW_CUBIC_M_PER_SEC = 5.0e-5f;

// Maximum allowable delta between calculated and actual volume (= 1 ml)
static const float COMPARISON_TOLERANCE_VOLUME_ML = 1.0f;

//@TODO: Finish writing more specific unit tests for this module

/*
 * @brief This method models the pressure to voltage transfer function of the
 * MPXV5004 series sensors.  The raw voltage coming out of the sensor would be
 * 5 * (...), but our PCB scales it down to 3.3f * (...) so that the pressure
 * range (0-4kPa) is in the voltage range 0-3.3V.
 */
static Voltage MPXV5004_PressureToVoltage(Pressure pressure) {
  return volts(3.3f * (0.2f * pressure.kPa() + 0.2f));
}

// Function that helps change the readings by setting the pressure sensor pins,
// advancing time and then getting sensors readings
static SensorReadings update_readings(Duration dt, Pressure patient_pressure,
                                      Pressure inflow_pressure,
                                      Pressure outflow_pressure,
                                      Sensors *sensors) {
  Hal.test_setAnalogPin(AnalogPin::PATIENT_PRESSURE,
                        MPXV5004_PressureToVoltage(patient_pressure));
  Hal.test_setAnalogPin(AnalogPin::INFLOW_PRESSURE_DIFF,
                        MPXV5004_PressureToVoltage(inflow_pressure));
  Hal.test_setAnalogPin(AnalogPin::OUTFLOW_PRESSURE_DIFF,
                        MPXV5004_PressureToVoltage(outflow_pressure));
  Hal.delay(dt);
  return sensors->GetSensorReadings();
}

TEST(SensorTests, FullScaleReading) {
  // These pressure waveforms start at 0 kPa to simulate the system being in the
  // proper calibration state then they go over the sensor full ranges.
  std::vector<Pressure> pressures = {kPa(0.0f), kPa(0.5f), kPa(1.0f),
                                     kPa(1.5f), kPa(2.0f), kPa(2.5f),
                                     kPa(3.0f), kPa(3.5f), kPa(3.92f)};

  // Will pad the rest of the simulated analog signals with ambient pressure
  // readings (0 kPa) voltage equivalents
  Voltage voltage_at_0kPa = MPXV5004_PressureToVoltage(kPa(0)); //[V]

  // First set the simulated analog signals to an ambient 0 kPa corresponding
  // voltage during calibration
  Hal.test_setAnalogPin(AnalogPin::PATIENT_PRESSURE, voltage_at_0kPa);
  Hal.test_setAnalogPin(AnalogPin::INFLOW_PRESSURE_DIFF, voltage_at_0kPa);
  Hal.test_setAnalogPin(AnalogPin::OUTFLOW_PRESSURE_DIFF, voltage_at_0kPa);

  Sensors sensors;
  sensors.Calibrate();

  // Now to compare the pressure readings the sensor module is calculating
  // versus what the original pressure waveform was
  for (auto p : pressures) {
    SCOPED_TRACE("Pressure " + std::to_string(p.kPa()));
    Hal.test_setAnalogPin(AnalogPin::PATIENT_PRESSURE,
                          MPXV5004_PressureToVoltage(p));
    auto readings = sensors.GetSensorReadings();
    float patient_pressure = cmH2O(readings.patient_pressure_cm_h2o).kPa();
    EXPECT_NEAR(patient_pressure, p.kPa(), COMPARISON_TOLERANCE_PRESSURE_KPA);
  }
}

// These tests expect Venturi Diamters of 14 and 5.5 mm.
// If the Default PressureSensors::DEFAULT_VENTURI_PORT_DIAM and
// DEFAULT_VENTURI_CHOKE_DIAM are changed from this, the tests will fail unless
// you update the expected values accordingly.
// Expected values from https://www.wolframalpha.com/input/?i=Venturi+flowmeter
TEST(SensorTests, TestPositiveVolumetricFlowCalculation) {
  EXPECT_NEAR(Sensors::PressureDeltaToFlow(kPa(1.0f)).cubic_m_per_sec(),
              9.7162e-4f, COMPARISON_TOLERANCE_FLOW_CUBIC_M_PER_SEC);
  EXPECT_NEAR(Sensors::PressureDeltaToFlow(kPa(-1.0f)).cubic_m_per_sec(),
              -9.7162e-4f, COMPARISON_TOLERANCE_FLOW_CUBIC_M_PER_SEC);
  EXPECT_NEAR(Sensors::PressureDeltaToFlow(kPa(0.0f)).cubic_m_per_sec(), 0.0f,
              COMPARISON_TOLERANCE_FLOW_CUBIC_M_PER_SEC);
  EXPECT_NEAR(Sensors::PressureDeltaToFlow(kPa(1.0e-7f)).cubic_m_per_sec(),
              3.0725e-7f, COMPARISON_TOLERANCE_FLOW_CUBIC_M_PER_SEC);
  EXPECT_NEAR(Sensors::PressureDeltaToFlow(kPa(100.0f)).cubic_m_per_sec(),
              9.7162e-3f, COMPARISON_TOLERANCE_FLOW_CUBIC_M_PER_SEC);
  EXPECT_NEAR(Sensors::PressureDeltaToFlow(kPa(-100.0f)).cubic_m_per_sec(),
              -9.7162e-3f, COMPARISON_TOLERANCE_FLOW_CUBIC_M_PER_SEC);
}

TEST(SensorTests, TotalFlowCalculation) {
  // These pressure waveforms start at 0 kPa to simulate the system being in the
  // proper calibration state then they go over the sensor full ranges with
  // less samples than for FullScaleReadings.
  std::vector<Pressure> pressures = {kPa(0.0f), kPa(0.5f), kPa(1.0f), kPa(2.0f),
                                     kPa(3.5f)};

  // First set the simulated analog signals to an ambient 0 kPa corresponding
  // voltage during calibration
  Voltage voltage_at_0kPa = MPXV5004_PressureToVoltage(kPa(0)); //[V]
  Hal.test_setAnalogPin(AnalogPin::PATIENT_PRESSURE, voltage_at_0kPa);
  Hal.test_setAnalogPin(AnalogPin::INFLOW_PRESSURE_DIFF, voltage_at_0kPa);
  Hal.test_setAnalogPin(AnalogPin::OUTFLOW_PRESSURE_DIFF, voltage_at_0kPa);

  Sensors sensors;
  sensors.Calibrate();

  for (auto p_in : pressures) {
    for (auto p_out : pressures) {
      auto readings =
          update_readings(seconds(0.0f), kPa(0.0f), p_in, p_out, &sensors);

      EXPECT_NEAR(ml_per_min(readings.flow_ml_per_min).cubic_m_per_sec(),
                  (Sensors::PressureDeltaToFlow(p_in) -
                   Sensors::PressureDeltaToFlow(p_out))
                      .cubic_m_per_sec(),
                  COMPARISON_TOLERANCE_FLOW_CUBIC_M_PER_SEC);
    }
  }
}

static constexpr Time base = millisSinceStartup(10000);
static constexpr Duration sample_period = milliseconds(10);
Time ticks(int num_ticks) { return base + num_ticks * sample_period; }

TEST(SensorTests, TVIntegrator) {
  // this Hal.delay is needed to allow TV construction with proper init time
  Hal.delay(base - Hal.now());
  TVIntegrator tidal_volume;
  VolumetricFlow flow = liters_per_sec(1.0f);
  int t = 0;
  tidal_volume.AddFlow(ticks(t++), flow);
  // first call to AddFlow ==> initialization and TV is 0, even if flow is not
  EXPECT_EQ(tidal_volume.GetTV().ml(), 0.0f);
  tidal_volume.AddFlow(ticks(t++), flow);
  // integrate 1 l/s flow over 10 ms ==> 5 ml (rectangle rule with initial flow
  // set to 0)
  EXPECT_NEAR(tidal_volume.GetTV().ml(), 5.0f, COMPARISON_TOLERANCE_VOLUME_ML);

  tidal_volume.AddFlow(ticks(t++), cubic_m_per_sec(2e-3f));
  // add 2 l/s flow over 10 ms ==> 20 ml ()
  EXPECT_NEAR(tidal_volume.GetTV().ml(), 20.0f, COMPARISON_TOLERANCE_VOLUME_ML);

  tidal_volume.AddFlow(ticks(t++), ml_per_min(0.0f));
  // add 0 l/s flow over 10 ms ==> 30 ml (rectangle rule)
  EXPECT_NEAR(tidal_volume.GetTV().ml(), 30.0f, COMPARISON_TOLERANCE_VOLUME_ML);

  // integrate 0 for some time ==> still 30 ms
  while (t < 100) {
    tidal_volume.AddFlow(ticks(t++), ml_per_min(0.0f));
  }

  EXPECT_NEAR(tidal_volume.GetTV().ml(), 30.0f, COMPARISON_TOLERANCE_VOLUME_ML);

  // reverse flow
  flow = liters_per_sec(-1.0f);
  // this does not increment t in order to allow oversampling (following test)
  tidal_volume.AddFlow(ticks(t), flow);
  // remove 1 l/s flow over 10 ms ==> 25 ml (rectangle rule)
  EXPECT_NEAR(tidal_volume.GetTV().ml(), 25.0f, COMPARISON_TOLERANCE_VOLUME_ML);

  // oversampling and expect volume to not change except on multiples of 5 ms
  for (int i = 0; i < 50; i++) {
    tidal_volume.AddFlow(ticks(t) + milliseconds(i), flow);
    // remove 1l/s flow over 5 ms only when i is a multiple of 5
    EXPECT_NEAR(tidal_volume.GetTV().ml(), 25.0f - floor(i / 5) * 5,
                COMPARISON_TOLERANCE_VOLUME_ML);
  }
}

// This test checks encapsulation of TVIntegrator in getSensorReadings with
// irregular sampling
TEST(SensorTests, TidalVolume) {
  // define pressure waveforms over which integration will take place
  // This was chosen randomly
  std::vector<Duration> sampling_time = {milliseconds(8), milliseconds(2),
                                         milliseconds(5), milliseconds(4),
                                         milliseconds(6), milliseconds(7)};
  std::vector<Pressure> pressure_in = {kPa(0.0f), kPa(1.5f), kPa(0.0f),
                                       kPa(1.0f), kPa(2.0f), kPa(3.0f)};
  std::vector<Pressure> pressure_out = {kPa(1.0f), kPa(0.5f), kPa(2.0f),
                                        kPa(0.0f), kPa(1.0f), kPa(2.0f)};

  // Set the simulated analog signals to an ambient 0 kPa corresponding
  // voltage during calibration
  Voltage voltage_at_0kPa = MPXV5004_PressureToVoltage(kPa(0)); //[V]
  Hal.test_setAnalogPin(AnalogPin::PATIENT_PRESSURE, voltage_at_0kPa);
  Hal.test_setAnalogPin(AnalogPin::INFLOW_PRESSURE_DIFF, voltage_at_0kPa);
  Hal.test_setAnalogPin(AnalogPin::OUTFLOW_PRESSURE_DIFF, voltage_at_0kPa);

  // Note: sensors' contructor calls Hal.delay, which means reference
  // tidal_volume must be constructed before sensors in order to have the same
  // initialization time.
  // Note outside of tests: the sensors TVintegrator's TV has a constant bias:
  // init value = time between sensors' init and first measure * first flow / 2
  TVIntegrator tidal_volume;
  Sensors sensors;
  sensors.Calibrate();

  for (uint i = 0; i < sampling_time.size(); i++) {
    Pressure p_in = pressure_in[i];
    Pressure p_out = pressure_out[i];
    Duration dt = sampling_time[i];
    SensorReadings readings =
        update_readings(dt, kPa(0.0f), p_in, p_out, &sensors);

    tidal_volume.AddFlow(Hal.now(), Sensors::PressureDeltaToFlow(p_in) -
                                        Sensors::PressureDeltaToFlow(p_out));
    EXPECT_FLOAT_EQ(tidal_volume.GetTV().ml(), readings.volume_ml);
  }
}

TEST(SensorTests, Calibration) {
  // First set the simulated analog signals to randomly chosen signals
  // corresponding to pressure during calibration
  Pressure init_pressure = kPa(0.23f);
  Hal.test_setAnalogPin(AnalogPin::PATIENT_PRESSURE,
                        MPXV5004_PressureToVoltage(init_pressure));
  Pressure init_inflow_delta = kPa(0.15f);
  Hal.test_setAnalogPin(AnalogPin::INFLOW_PRESSURE_DIFF,
                        MPXV5004_PressureToVoltage(init_inflow_delta));
  Pressure init_outflow_delta = kPa(-0.13f);
  Hal.test_setAnalogPin(AnalogPin::OUTFLOW_PRESSURE_DIFF,
                        MPXV5004_PressureToVoltage(init_outflow_delta));

  Sensors sensors;
  sensors.Calibrate();

  // get the sensor readings for the init signals, expect 0
  SensorReadings readings = sensors.GetSensorReadings();

  EXPECT_NEAR(readings.patient_pressure_cm_h2o, 0.0f,
              COMPARISON_TOLERANCE_PRESSURE_KPA);
  EXPECT_NEAR(readings.flow_ml_per_min, 0.0f,
              COMPARISON_TOLERANCE_FLOW_CUBIC_M_PER_SEC);

  // set measured signals to 0 and expect -1*init values
  readings = update_readings(seconds(0), kPa(0), kPa(0), kPa(0), &sensors);

  EXPECT_NEAR(readings.patient_pressure_cm_h2o, -1 * init_pressure.cmH2O(),
              COMPARISON_TOLERANCE_PRESSURE_KPA);
  EXPECT_NEAR(readings.flow_ml_per_min,
              -1 * (Sensors::PressureDeltaToFlow(init_inflow_delta) -
                    Sensors::PressureDeltaToFlow(init_outflow_delta))
                       .ml_per_min(),
              COMPARISON_TOLERANCE_FLOW_CUBIC_M_PER_SEC);

  // set measured signals to some random values + init values and expect init
  // value to be removed from the readings
  readings = update_readings(seconds(0), kPa(-0.5f) + init_pressure,
                             kPa(1.1f) + init_inflow_delta,
                             kPa(0.01f) + init_outflow_delta, &sensors);

  EXPECT_NEAR(readings.patient_pressure_cm_h2o, kPa(-0.5f).cmH2O(),
              COMPARISON_TOLERANCE_PRESSURE_KPA);
  EXPECT_NEAR(ml_per_min(readings.flow_ml_per_min).cubic_m_per_sec(),
              (Sensors::PressureDeltaToFlow(kPa(1.1f)) -
               Sensors::PressureDeltaToFlow(kPa(0.01f)))
                  .cubic_m_per_sec(),
              COMPARISON_TOLERANCE_FLOW_CUBIC_M_PER_SEC);
}
