#define TINY_GSM_MODEM_SIM800

#define DEBUG //enable debug mode

#include <TinyGsmClient.h>
#include <ThingerTinyGSM.h>

HardwareSerial Serial1(1); //using hardware serial

#define USERNAME “xxxxxxxx”
#define DEVICE_ID “xxxxxxxx”
#define DEVICE_CREDENTIAL “xxxxxxxxx”

// use your own APN config
#define APN_NAME “xxxxxx”
#define APN_USER “”
#define APN_PSWD “”

// set your cad pin (optional)
#define CARD_PIN “”

ThingerTinyGSM thing(USERNAME, DEVICE_ID, DEVICE_CREDENTIAL, Serial1);

// define your board pin here
#define LED_PIN 27

void setup() {
pinMode(LED_PIN, OUTPUT);

// uncomment line for debug
Serial.begin(115200);

// define hardware serial setting, make sure it is same with the sim800 module.
// usage : Serial1.begin(baudrate, SERIAL_8N1, rxPin, txPin)
Serial1.begin(9600, SERIAL_8N1, (int8_t) 17, (int8_t) 16);

// set APN (you can remove user and password from call if your apn does not require them)
thing.setAPN(APN_NAME, APN_USER, APN_PSWD);

// digital pin control example (i.e. turning on/off a light, a relay, configuring a parameter, etc)
thing[“led”] << digitalPin(LED_PIN);

// resource output example (i.e. reading a sensor value)
thing[“millis”] >> outputValue(millis());

}

void loop() {
thing.handle();
}