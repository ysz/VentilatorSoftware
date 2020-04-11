#include <Arduino.h>

// Basic test code to test the comms protocol using a second arduino (instead of a rpi)

// FIXME Use enums from packet_types.h instead of redefining them here
// FIXME This has been put together very rapidly just to test some communications, it can certainly be improved
/*
* Stores all command numbers that can sent by the GUI
*/
enum class command {

    /* Medical mode commands */

    set_rr          = 0x00,
    get_rr          = 0x01,
    set_tv          = 0x02,
    get_tv          = 0x03,
    set_peep        = 0x04,
    get_peep        = 0x05,
    set_pip         = 0x06,
    get_pip         = 0x07,
    set_dwell       = 0x08,
    get_dwell       = 0x09,
    set_id          = 0x0a,   /* Inspiration duration */
    get_id          = 0x0b,
    set_ed          = 0x0c,   /* Expiration duration */
    get_ed          = 0x0d,

    get_pressure    = 0x0e,
    get_flow        = 0x0f,
    get_volume      = 0x10,

    /* Engineering mode commands */

    set_kp          = 0x20,     /* PID constant Kp */
    get_Kp          = 0x21,
    set_Ki          = 0x22,     /* PID constant Ki */
    get_Ki          = 0x23,
    set_Kd          = 0x24,     /* PID constant Kd */
    get_Kd          = 0x25,

    set_blower      = 0x26,     /* Turn blower ON/OFF */
    reset_vc        = 0x27,     /* Reset Ventilation Controller */

    /* Mixed Engineering/Medical mode commands */

    set_periodic    = 0x40,     /* Periodic data transmission mode (Pressure, Flow, Volume) */
    get_periodic    = 0x41,
    set_mode        = 0x42,     /* Engineering or medical mode */
    get_mode        = 0x43,
    comms_check     = 0x44,     /* Communications check command */

    count                       /* Sentinel */
};

/*
* Stores all message types that can sent by the Ventilator controller and the GUI
*/
enum class msgType {
    cmd             = 0x00,     /* Command */
    ack             = 0x01,     /* Ventilator Controller alarm Ack */
    nAck            = 0x02,     /* Ventilator Controller alarm Fail */

    rAck            = 0x10,     /* Response Ack */
    rErrChecksum    = 0x11,     /* Response checksum error */
    rErrMode        = 0x12,     /* Response mode error */
    rErrCmd         = 0x13,     /* Response cmd error */

    status          = 0x20,     /* Status */
    alarm           = 0x30,     /* Alarm */
    data            = 0x40,     /* Data */

    count                       /* Sentinel */
};

enum class dataID {
    /* DataID values should start at a value higher than the higest command number */

    /* Status */
    vc_boot      = 0x80,    /* Status sent when arduino boots (includes software version) */


    /* Alarms */
    alarm_1     = 0xA0,
    alarm_2     = 0xA1,

    /* Data */
    data_1      = 0xC0,

    count                   /* Sentinel */
};

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

void test_cmd() {
    char data;

    data = 0x00;

    send(msgType::cmd, command::set_periodic, &data, 1);
}

void test_cmd_checksumErr() {
    char data;

    data = 0x00;

    send_checksumError(msgType::cmd, command::set_periodic, &data, 1);
}

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
