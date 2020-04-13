#include <Arduino.h>

// Basic test code to test the comms protocol using a second arduino (instead of a rpi)

#include "../../../../common/include/packet_types.h"

// FIXME This has been put together very rapidly just to test some communications, it can certainly be improved

// Function prototypes
uint16_t Fletcher16_calc(uint16_t *sum1, uint16_t *sum2,  char *data, uint8_t count );
void send(enum msgType type, enum command cmd, char *data, uint8_t len);
void send_checksumError(enum msgType type, enum command cmd, char *data, uint8_t len);

enum msgType wait_packet();

void respond_alarm(enum msgType type);
void respond_alarm_checksumErr(enum msgType type);

void test_cmd();
void test_cmd_checksumErr();

void test_alarm_response();
void test_alarm_response_checksumErr();

// Test the sending and responses from the ventilator Controller
// 1) Send command to ventilator controller (check response with oscilloscope)
// 2) Send command to ventilator with checksum error (check response with oscilloscope)
// 3) Following an alarm, respond with an ack
// 4) Following an alarm, respond with a nAck
// 5) Following an alarm, respond with an ack (with checksum error)
// 6) Following an alarm, respond with a nAck (with checksum error)

void setup() {
  Serial.begin(115200, SERIAL_8N1);

  //test_cmd();
  //test_cmd_checksumErr();

  test_alarm_response();
  //test_alarm_response_checksumErr();

  while(1) {}
}

void loop() {

}

// Send test command to ventilator controller
void test_cmd() {
    char data;

    data = 0x00;

    send(msgType::cmd, command::set_periodic, &data, 1);
}

// Send test command to ventilator controller with checksum error
void test_cmd_checksumErr() {
    char data;

    data = 0x00;

    send_checksumError(msgType::cmd, command::set_periodic, &data, 1);
}

// Respond to alarm packet from ventilator controller (ack or nack)
void test_alarm_response() {

    enum msgType msg;

    do {

        msg = wait_packet();

        if(msg == msgType::alarm) {
            // Send alarm ack
            respond_alarm(msgType::ack);

            // Send alarm nack
            //respond_alarm(msgType::nAck);
        }
    } while(1);

}

// Respond to alarm packet from ventilator controller (ack or nack)
// with checksum error
void test_alarm_response_checksumErr() {
    enum msgType msg;

    do {

        msg = wait_packet();

        if(msg == msgType::alarm) {

            // Send alarm ack
            respond_alarm_checksumErr(msgType::ack);

            // Send alarm nack
            //respond_alarm_checksumErr(msgType::nAck);
        }
    } while(1);
}

// Repond to alarm  from ventilator controller
void respond_alarm(enum msgType type) {

    char metadata[1];
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    uint16_t csum;
    uint8_t c0,c1,f0,f1;

    metadata[0] = ((char) type) & 0xff; // DATA_TYPE

    csum = Fletcher16_calc(&sum1, &sum2, metadata, sizeof(metadata));

    // Calculate check bytes
    // TODO Can this be optimised?
    f0 = csum & 0xff;
    f1 = (csum >> 8) & 0xff;
    c0 = 0xff - ((f0 + f1) % 0xff);
    c1 = 0xff - ((f0 + c0) % 0xff);

    Serial.write(metadata, sizeof(metadata));  // msgType

    // Send checksum
    Serial.write(c0);
    Serial.write(c1);
}

// Repond to alarm from ventilator controller with checksum error
void respond_alarm_checksumErr(enum msgType type) {

    char metadata[1];
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    uint16_t csum;
    uint8_t c0,c1,f0,f1;

    metadata[0] = ((char) type) & 0xff; // DATA_TYPE

    csum = Fletcher16_calc(&sum1, &sum2, metadata, sizeof(metadata));

    // Calculate check bytes
    // TODO Can this be optimised?
    f0 = csum & 0xff;
    f1 = (csum >> 8) & 0xff;
    c0 = 0xff - ((f0 + f1) % 0xff);
    c1 = 0xff - ((f0 + c0) % 0xff);

    Serial.write(metadata, sizeof(metadata));  // msgType

    c0++;

    // Send checksum
    Serial.write(c0);
    Serial.write(c1);
}

// Wait for an incoming packet
enum msgType wait_packet() {

    char data[32];
    uint8_t len;
    enum msgType msg;

    while(Serial.available() != 3) {
    }

    // Get first bytes
    Serial.readBytes(data, 3);

    len = (uint8_t) data[2];
    msg = (enum msgType) data[0];

    while(Serial.available() != len) {}

    Serial.readBytes(data, len); // Get data

    while(Serial.available() != 2) {} // Wait for checksum

    Serial.readBytes(data, 2); // Get checksum

    return msg;
}

// Send packet to ventilator controller
void send(enum msgType type, enum command cmd, char *data, uint8_t len) {

    char metadata[3];
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    uint16_t csum;
    uint8_t c0,c1,f0,f1;

    metadata[0] = ((char) type) & 0xff; // DATA_TYPE
    metadata[1] = (char)  cmd; // DATA_ID
    metadata[2] = (char)  len; // LEN

    Fletcher16_calc(&sum1, &sum2, metadata, sizeof(metadata));
    csum = Fletcher16_calc(&sum1, &sum2, data, len);

    // Calculate check bytes
    // TODO Can this be optimised?
    f0 = csum & 0xff;
    f1 = (csum >> 8) & 0xff;
    c0 = 0xff - ((f0 + f1) % 0xff);
    c1 = 0xff - ((f0 + c0) % 0xff);

    Serial.write(metadata, sizeof(metadata));  // Send DATA_TYPE, DATA_ID, LEN
    Serial.write(data, len);  // Send DATA

    // Send checksum
    Serial.write(c0);
    Serial.write(c1);
}

// Send packet to ventilator controller with checksum error
void send_checksumError(enum msgType type, enum command cmd, char *data, uint8_t len) {

    char metadata[3];
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    uint16_t csum;
    uint8_t c0,c1,f0,f1;

    metadata[0] = ((char) type) & 0xff; // DATA_TYPE
    metadata[1] = (char)  cmd; // DATA_ID
    metadata[2] = (char)  len; // LEN

    Fletcher16_calc(&sum1, &sum2, metadata, sizeof(metadata));
    csum = Fletcher16_calc(&sum1, &sum2, data, len);

    // Calculate check bytes
    // TODO Can this be optimised?
    f0 = csum & 0xff;
    f1 = (csum >> 8) & 0xff;
    c0 = 0xff - ((f0 + f1) % 0xff);
    c1 = 0xff - ((f0 + c0) % 0xff);

    Serial.write(metadata, sizeof(metadata));  // Send DATA_TYPE, DATA_ID, LEN
    Serial.write(data, len);  // Send DATA

    c0++;

    // Send checksum
    Serial.write(c0);
    Serial.write(c1);
}

// Calculate checksum
uint16_t Fletcher16_calc(uint16_t *sum1, uint16_t *sum2,  char *data, uint8_t count )
{
    uint8_t index;

    for (index = 0; index < count; ++index)
    {
        *sum1 = ((*sum1) + data[index]) % 255;
        *sum2 = ((*sum2) + (*sum1)) % 255;
    }

    return ((*sum2) << 8) | *sum1;
}
