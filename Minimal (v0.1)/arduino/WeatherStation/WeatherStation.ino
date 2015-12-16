// Arduino pin number used for the communication with DHT22 sensor.
// A custom 1-Wire communication protocol is used for this module.
// See: https://www.adafruit.com/datasheets/Digital%20humidity%20and%20temperature%20sensor%20AM2302.pdf
#define DHT22_PIN 9

// pins number for WiFi enabled/disabled LEDs
#define WIFI_DISABLED_LED_PIN 7
#define WIFI_ACTIVE_LED_PIN 8
// arduino pin used to connect to CH_PD (Power Down) WiFi module pin
#define WIFI_CH_PD_PIN 2
// arduino pin used to connect to RESET WiFi module pin
#define WIFI_RESET_PIN 3
// interval-time (in ms) to check WiFi module (5s)
#define WIFI_CHECK_TIME 5000
// interval-time (in ms) to restart the WiFi ESP8266 module 
// in case that it become unresponsive (hardware reset required!)
#define WIFI_RESTART_TIMEOUT 30000

#include <dht.h>
#include <ESP8266.h>;

// keep tracking the average temperature and humidity 
// values, starting with last system reset
float avgTemperature = 0;
float avgHumidity = 0;
// used to compute the averate temperature value
unsigned long avgDhtStep = 1;

// data template for sensors
const char SENSORS_DATA_TEMPLATE[] PROGMEM = 
  "{\"temperature\": %s, \"avgTemperature\": %s, \"humidity\": %s, \"avgHumidity\": %s, \"voltage\": %s, \"freeRam\": %d}";

// DHT22 sensor controller class
dht dht22;

// ESP8266 WiFi module controller
ESP8266 esp( Serial);

enum class Command {
  GET_ALL_SENSORS_DATA = 97
};

/**
 * Check if ESP8266 (WiFi) module is active.
 */
boolean checkWiFi() {
  // try to communicate with  ESP8266 module
  if( esp.at() == ESP8266::Error::NONE) {
    digitalWrite( WIFI_DISABLED_LED_PIN, LOW);
    digitalWrite( WIFI_ACTIVE_LED_PIN, HIGH);
    return true;
  } else { 
    digitalWrite( WIFI_ACTIVE_LED_PIN, LOW);
    digitalWrite( WIFI_DISABLED_LED_PIN, HIGH);
    return false;
  }
}

/**
 * Prepare Wifi Communication:
 *  - prepare pins for the ESP8266 module
 *  - hardware reset ESP8266 module
 */
void setupWiFi() {
  // set pins used for WiFi status LEDs as OUTPUT
  pinMode( WIFI_ACTIVE_LED_PIN, OUTPUT);
  pinMode( WIFI_DISABLED_LED_PIN, OUTPUT);
  // Arduino pin connected to ESP8266 CH_PD pin is set to OUTPUT.
  // To keep the module active, this pin must be kept HIGH.
  pinMode( WIFI_CH_PD_PIN, OUTPUT);
  digitalWrite( WIFI_CH_PD_PIN, HIGH);
  // Arduino pin connected to ESP8266 RESET pin is set to OUTPUT.
  // To avoid random resents, we keep this HIGH.
  pinMode( WIFI_RESET_PIN, OUTPUT);
  digitalWrite( WIFI_RESET_PIN, HIGH);
  // perform a hardware reset (ESP8266 RESET pin goes LOW)
  digitalWrite( WIFI_RESET_PIN, LOW);
  delay( 200);
  // allow ESP8266 module to boot
  digitalWrite( WIFI_RESET_PIN, HIGH);
  // baud 115200, communication with ESP8266 module
  Serial.begin( 115200);
  // wait for the ESP8266 module to start, after the forced hardware reset.
  // We check the wifi state once a second, until the ESP8266 WiFi module responds.
  while( !checkWiFi()) {
    delay( 1000);
  };
  // start UDP connection - wait on all ports
  esp.atCipstartUdp();
}

/**
 * Read the voltage which powes the system on the 5V line.
 * This is possible by using the internal voltage reference 
 * available in some ATmega MCUs such as ATmega328p
 */
float getVcc() {
  long result;
  // read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  // wait for Vref to settle
  delay(2); 
  // convert
  ADCSRA |= _BV(ADSC); 
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  // Back-calculate AVcc in mV
  result = 1103700L / result; 
  // return response in volts
  return result / 1000.0;
}

/**
 *  Update the data (perform new reading) for 
 *  the DHT22 temperature and humidity sensor.
 *  After calling this method, the temperature
 *  and humidity values are available by reading
 *  DHT.temperature and DHT.humidity.
 */
void updateDHTData() {
  if ( dht22.read22( DHT22_PIN) == DHTLIB_OK) {
    avgTemperature = ( avgTemperature * (avgDhtStep - 1) + dht22.temperature) / (float)avgDhtStep;
    avgHumidity = ( avgHumidity * (avgDhtStep - 1) + dht22.humidity) / (float)avgDhtStep;
  }
}

/**
 * Get the data string using a pattern and the parameter values.
 */
void createSensorsDataFromTemplate( char *&data) {
  char buffTemp[7] = {0}, buffAvgTemp[7] = {0}, buffAvgHum[7] = {0},
    buffHum[7] = {0}, buffVcc[5] = {0}, tmpl[140] = {0};
  char *pTmpl = tmpl;
  uint8_t templateLen = -1;
  // read template from PROGMEM
  getPMData( SENSORS_DATA_TEMPLATE, pTmpl, templateLen);
  // create data string from template by replacing
  // parameters with their actual values from sensors
  sprintf( data, pTmpl, 
    dtostrf( dht22.temperature, 6, 1, buffTemp),
    dtostrf( avgTemperature, 6, 1, buffAvgTemp),
    dtostrf( dht22.humidity, 6, 1, buffHum), 
    dtostrf( avgHumidity, 6, 1, buffAvgHum),
    dtostrf( getVcc(), 4, 2, buffVcc),
    getFreeMCUMemory());
}

/**
 * Process requests (with or without parameters).
 * The commands are defined as constants, and 
 * have the pattern: CMD_command_name.
 * @param data 
 *    the data to be processed (+IPD:
 */
void processRequest( char *data) {
  char progmemData[150] = {0};
  char *pData = progmemData;
  // first char represents the command
  char cmd = *(data); 
  switch ( (Command)cmd) {
    case Command::GET_ALL_SENSORS_DATA:
      createSensorsDataFromTemplate( pData);
      esp.atCipsend( pData);
    break;
    default:
      // nothing to do ...
    break;
  }
}

/**
 * Prepare ths system. This method is called when the system starts.
 */
void setup() {
  // setup WiFi - ESP8266 module
  setupWiFi();
  // Get initial data (temperature & humidity) from DHT22 sensor.
  // Add a 2 seconds delay to prevent timeouts from the sensor.
  delay( 2000);
  updateDHTData();
}

/**
 * Loop method which executes again and again.
 */
void loop() {
  // Incomming real data via IPD is 1 byte (the command identifier byte).
  // The first bytes are: "+IPD,n:", followed by the real data
  char data[10] = {0}, *ipdData = data;
  // Incomming data from ESP8266 module
  // Lengt must be greater than 7 bytes ("+IPD,n:")
  if ( Serial.available() > 7) {
    // process request received from UDP port: (wait 50ms for data)
    if ( esp.ipd( ipdData) == ESP8266::Error::NONE) {
      // process the request
      processRequest( ipdData);
    } 
  }
  // a small delay before checking againd for WiFi incomming data
  delay( 25);
}
