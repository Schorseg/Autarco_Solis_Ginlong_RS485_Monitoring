/*
	Highly based on a combination of different version of
	https://github.com/mertenats/Open-Home-Automation/tree/master/ha_mqtt_binary_sensor_pir
	Also using https://github.com/tzapu/WiFiManager


	Configuration (HA) :
	  light:
	    platform: mqtt
	    name: 'Office light'
	    state_topic: 'office/light'
	    command_topic: 'office/light/switch'
	    brightness_state_topic: 'office/light/brightness'
	    brightness_command_topic: 'office/light/brightness/set'
	    optimistic: false


   	Kolja Windeler v0.1 - untested
*/

#include <ESP8266WiFi.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <PubSubClient.h>
#include <OneWire.h>

// defines 
#define DIMM_DONE 	        0
#define DIMM_DIMMING 	      1
#define DHT                 1
#define DS                  2
#define DHT_DS_MODE         DS
#define CONFIG_SSID         "OPEN_ESP_CONFIG_AP"
#define MAX_AP_TIME         300
// pins
#define PWM_LIGHT_PIN       D2 // IC pin 16
#define SIMPLE_LIGHT_PIN    D7 // IC pin 12
#define BUTTON_INPUT_PIN    D3 // IC pin 15
#define DHT_DS_PIN          D8 // IC pin 13 ... nicht auf der mcu aber gut am sonoff .. hmm
#define PIR_PIN             D1 // IC pin 24
#define UPDATE_TEMP         60000 // update temperature once per minute
#define BUTTON_TIMEOUT      1500 // max 1500ms timeout between each button press to count to 5 (start of config)
#define MSG_BUFFER_SIZE     60 // mqtt messages max size
//Temperature chip i/o
#if DHT_DS_MODE == DHT
  #include "DHT.h"
  DHT dht(DHT_DS_PIN, DHT22); // DHT22
#else
  OneWire ds(DHT_DS_PIN);  // on digital pin DHT_DS_PIN
#endif

// Buffer to hold data from the WiFi manager for mqtt login
char mqtt_user[16];
char mqtt_pw[16];
char mqtt_dev_short[6];
char mqtt_server_cli_id[20];
char char_buffer[35];

// MQTT: topics, constants, etc
const PROGMEM char*     MQTT_PWM_LIGHT_STATE_TOPIC              = "/PWM_light/status";		      // publish state here
const PROGMEM char*     MQTT_PWM_LIGHT_COMMAND_TOPIC	  	      = "/PWM_light/switch"; 		      // get command here

const PROGMEM char*     MQTT_SIMPLE_LIGHT_STATE_TOPIC		        = "/simple_light";			        // publish state here
const PROGMEM char*     MQTT_SIMPLE_LIGHT_COMMAND_TOPIC		      = "/simple_light/switch"; 	    // get command here

const PROGMEM char* 	  MQTT_MOTION_STATUS_TOPIC		            = "/motion/status";			        // publish
const PROGMEM char* 	  MQTT_TEMPARATURE_TOPIC		              = "/temperature";			          // publish
const PROGMEM char* 	  MQTT_HUMIDITY_TOPIC			                = "/humidity";			            // publish

const PROGMEM char*     MQTT_PWM_LIGHT_BRIGHTNESS_STATE_TOPIC   = "/PWM_light/brightness";		  // publish
const PROGMEM char*     MQTT_PWM_LIGHT_BRIGHTNESS_COMMAND_TOPIC = "/PWM_light/brightness/set";	// set value
const PROGMEM char*     MQTT_PWM_DIMM_DELAY_COMMAND_TOPIC 	    = "/PWM_dimm/delay/set";		    // set value
const PROGMEM char*     MQTT_PWM_DIMM_BRIGHTNESS_COMMAND_TOPIC 	= "/PWM_dimm/brightness/set";	  // set value

const PROGMEM char*     MQTT_RSSI_STATE_TOPIC                   = "/rssi";                      // publish

const PROGMEM char*     STATE_ON          		= "ON";
const PROGMEM char*     STATE_OFF         		= "OFF";

// variables used to store the statea and the brightness of the light
boolean 		m_pwm_light_state 			          = false;
boolean     m_published_pwm_light_state       = true; // to force instant publish once we are online
boolean			m_simple_light_state 		          = false;
boolean     m_published_simple_light_state    = true; // to force instant publish once we are online
uint8_t 		m_light_brightness			          = 255;
uint8_t     m_published_light_brightness      = 0; // to force instant publish once we are online

uint16_t		m_pwm_dimm_time				            = 10; // 10ms per Step, 255*0.01 = 2.5 sec
uint8_t			m_pwm_dimm_state			            = DIMM_DONE;
uint8_t			m_pwm_dimm_target			            = 0;

uint8_t 		m_pir_state 				              = LOW; // no motion detected
uint8_t 		m_published_pir_state             = LOW;


// buffer used to send/receive data with MQTT, can not be done with the char_buffer, as both as need simultaniously 
char            m_msg_buffer[MSG_BUFFER_SIZE];

WiFiClient      wifiClient;
WiFiManager 		wifiManager;
PubSubClient    client(wifiClient);

uint32_t 		    timer		= 0;
uint32_t 		    timer_dimmer	= 0;
uint32_t        timer_button  = 0;
uint8_t         counter_button = 0;

///////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////// PUBLISHER ///////////////////////////////////////
// function called to publish the state of the led (on/off)
void publishPWMLightState() {
  Serial.println("publish PWM state");
  if (m_pwm_light_state) {
    client.publish(build_topic(MQTT_PWM_LIGHT_STATE_TOPIC), STATE_ON, true);
  } else {
    client.publish(build_topic(MQTT_PWM_LIGHT_STATE_TOPIC), STATE_OFF, true);
  }
  m_published_pwm_light_state = m_pwm_light_state;
}

// function called to publish the brightness of the led
void publishPWMLightBrightness() {
  Serial.println("publish PWM brightness");
  snprintf(m_msg_buffer, MSG_BUFFER_SIZE, "%d", m_light_brightness);
  client.publish(build_topic(MQTT_PWM_LIGHT_BRIGHTNESS_STATE_TOPIC), m_msg_buffer, true);
  m_published_light_brightness = m_light_brightness;
}

// function called to publish the state of the led (on/off)
void publishSimpleLightState() {
  Serial.println("publish simple light state");
  if (m_simple_light_state) {
    client.publish(build_topic(MQTT_SIMPLE_LIGHT_STATE_TOPIC), STATE_ON, true);
  } else {
    client.publish(build_topic(MQTT_SIMPLE_LIGHT_STATE_TOPIC), STATE_OFF, true);
  }
  m_published_simple_light_state = m_simple_light_state;
}

// function called to publish the state of the PIR (on/off)
void publishPirState() {
  Serial.println("publish pir state");
  if (m_pir_state) {
    client.publish(build_topic(MQTT_MOTION_STATUS_TOPIC), STATE_ON, true);
  } else {
    client.publish(build_topic(MQTT_MOTION_STATUS_TOPIC), STATE_OFF, true);
  }
  m_published_pir_state = m_pir_state;
}

// function called to publish the brightness of the led
void publishTemperature(float temp) {
  Serial.println("publish temp");
  dtostrf(temp, 3, 2, m_msg_buffer);
  client.publish(build_topic(MQTT_TEMPARATURE_TOPIC), m_msg_buffer, true);
}

#if DHT_DS_MODE == DHT
// function called to publish the brightness of the led
void publishHumidity(float hum) {
  Serial.println("publish humidiy");
  dtostrf(hum, 3, 2, m_msg_buffer);
  client.publish(build_topic(MQTT_HUMIDITY_TOPIC), m_msg_buffer, true);
}
#endif

void publishRssi(float rssi) {
  Serial.println("publish rssi");
  dtostrf(rssi, 3, 2, m_msg_buffer);
  client.publish(build_topic(MQTT_RSSI_STATE_TOPIC), m_msg_buffer, true);
}
//////////////////////////////////// PUBLISHER ///////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////
///////////////////// function called when a MQTT message arrived /////////////////////
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
  // concat the payload into a string
  String payload;
  for (uint8_t i = 0; i < p_length; i++) {
    payload.concat((char)p_payload[i]);
  }
  // handle message topic
  ////////////////////////// SET LIGHT ON/OFF ////////////////////////
  if (String(build_topic(MQTT_PWM_LIGHT_COMMAND_TOPIC)).equals(p_topic)) {
    // test if the payload is equal to "ON" or "OFF"
    if (payload.equals(String(STATE_ON))) {
      if (m_pwm_light_state != true) {
        m_pwm_light_state = true;
        setPWMLightState();
      }
    } else if (payload.equals(String(STATE_OFF))) {
      if (m_pwm_light_state != false) {
        m_pwm_light_state = false;
        setPWMLightState();
      }
    }
  }
  else if (String(build_topic(MQTT_SIMPLE_LIGHT_COMMAND_TOPIC)).equals(p_topic)) {
    // test if the payload is equal to "ON" or "OFF"
    if (payload.equals(String(STATE_ON))) {
      if (m_simple_light_state != true) {
        m_simple_light_state = true;
        setSimpleLightState();
      }
    } else if (payload.equals(String(STATE_OFF))) {
      if (m_simple_light_state != false) {
        m_simple_light_state = false;
        setSimpleLightState();
      }
    }
  }
  ////////////////////////// SET LIGHT ON/OFF ////////////////////////
  ////////////////////////// SET LIGHT BRIGHTNESS ////////////////////////
  else if (String(build_topic(MQTT_PWM_LIGHT_BRIGHTNESS_COMMAND_TOPIC)).equals(p_topic)) {
    uint8_t brightness = payload.toInt();
    if (brightness < 0 || brightness > 255) {
      // do nothing...
      return;
    } else {
      m_light_brightness = brightness;
      setPWMLightState();
    }
  }
  else if (String(build_topic(MQTT_PWM_DIMM_BRIGHTNESS_COMMAND_TOPIC)).equals(p_topic)) {
    uint8_t brightness = payload.toInt();
    if (brightness < 0 || brightness > 255) {
      // do nothing...
      return;
    } else {
      pwmDimmTo(brightness);
    }
  }
  else if (String(build_topic(MQTT_PWM_DIMM_DELAY_COMMAND_TOPIC)).equals(p_topic)) {
    m_pwm_dimm_time = payload.toInt();
  }
  ////////////////////////// SET LIGHT BRIGHTNESS ////////////////////////
}
///////////////////// function called when a MQTT message arrived /////////////////////
///////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////
////////////////////// peripheral function ///////////////////////////////////
// function called to adapt the brightness and the state of the led
void setPWMLightState() {
  if (m_pwm_light_state) {
    analogWrite(PWM_LIGHT_PIN, m_light_brightness);
    Serial.print("INFO: PWM Brightness: ");
    Serial.println(m_light_brightness);
  } else {
    analogWrite(PWM_LIGHT_PIN, 0);
    Serial.println("INFO: Turn PWM light off");
  }
}

// function called to adapt the state of the led
void setSimpleLightState() {
  if (m_simple_light_state) {
    digitalWrite(SIMPLE_LIGHT_PIN, HIGH);
    Serial.println("INFO: Simple pin on");
  } else {
    digitalWrite(SIMPLE_LIGHT_PIN, LOW);
    Serial.println("INFO: Simple light off");
  }
}

void pwmDimmTo(uint16_t dimm_to) {
  // target value:  dimm_to
  // current value: m_light_brightness
  m_pwm_dimm_target = dimm_to;

  if (!m_pwm_light_state) {
    m_pwm_light_state = true;
  }
  m_pwm_dimm_state = DIMM_DIMMING; // enabled dimming
  if (millis() - m_pwm_dimm_time > 0) {
    timer_dimmer = millis() - m_pwm_dimm_time;
  } else {
    timer_dimmer = 0;
  }
}

#if DHT_DS_MODE == DS
float getTemp() { // https://blog.silvertech.at/arduino-temperatur-messen-mit-1wire-sensor-ds18s20ds18b20ds1820/
  //returns the temperature from one DS18S20 in DEG Celsius

  byte data[12];
  byte addr[8];

  if ( !ds.search(addr)) {
    //no more sensors on chain, reset search
    ds.reset_search();
    return -1000;
  }

  if ( OneWire::crc8( addr, 7) != addr[7]) {
    Serial.println("CRC is not valid!");
    return -1000;
  }

  if ( addr[0] != 0x10 && addr[0] != 0x28) {
    Serial.print("Device is not recognized");
    return -1000;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1); // start conversion, with parasite power on at the end

  byte present = ds.reset();
  ds.select(addr);
  ds.write(0xBE); // Read Scratchpad


  for (int i = 0; i < 9; i++) { // we need 9 bytes
    data[i] = ds.read();
  }

  ds.reset_search();

  byte MSB = data[1];
  byte LSB = data[0];

  float tempRead = ((MSB << 8) | LSB); //using two's compliment
  float TemperatureSum = tempRead / 16;

  return TemperatureSum;
}
#else
float getTemp() {
  return dht.readTemperature();
}

float getHumidity() {
  return dht.readHumidity();
}
#endif

void updatePIRstate() {
  m_pir_state = digitalRead(PIR_PIN);
}

// external button push
void updateBUTTONstate() {
  // toggle, write to pin, publish to server
  m_simple_light_state = !m_simple_light_state;
  setSimpleLightState();

  // 5 button pushes within 1.5 sec each to enter wifi management mode
  if (millis() - timer_button > BUTTON_TIMEOUT) {
    counter_button = 0;
  } else {
    counter_button++;
    if(counter_button>=5){
      wifiManager.startConfigPortal(CONFIG_SSID); // needs to be tested!
    }
  }
  timer_button = millis();
}

/////////////////////////////// peripheral function ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////// network function ///////////////////////////////////
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("INFO: Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqtt_server_cli_id, mqtt_user, mqtt_pw)) {
      Serial.println("\nINFO: connected");

      // ... and resubscribe
      client.subscribe(build_topic(MQTT_PWM_LIGHT_COMMAND_TOPIC));
      client.subscribe(build_topic(MQTT_PWM_LIGHT_BRIGHTNESS_COMMAND_TOPIC));
      client.subscribe(build_topic(MQTT_SIMPLE_LIGHT_COMMAND_TOPIC));
    } else {
      Serial.print("ERROR: failed, rc=");
      Serial.print(client.state());
      Serial.println("DEBUG: try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

// build topics with device id on the fly
char* build_topic(const char *topic) {
  sprintf(char_buffer, "%s%s", mqtt_dev_short, topic);
  return char_buffer;
}
////////////////////////////////// network function ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////// SETUP ///////////////////////////////////
void setup() {
  // init the serial
  Serial.begin(115200);
  
  // init the led
  pinMode(PWM_LIGHT_PIN, OUTPUT);
  analogWriteRange(255);
  setPWMLightState();

  pinMode(SIMPLE_LIGHT_PIN, OUTPUT);
  setSimpleLightState();

  // prepare wifimanager variables
  char mqtt_server_ip[16];
  sprintf(mqtt_server_ip, "192.168.2.84");
  WiFiManagerParameter WiFiManager_mqtt_server_ip("ip", "mqtt server ip", mqtt_server_ip, 15);
  wifiManager.addParameter(&WiFiManager_mqtt_server_ip);

  char mqtt_server_port[6];
  sprintf(mqtt_server_port, "1883");
  WiFiManagerParameter WiFiManager_mqtt_server_port("port", "mqtt server port", mqtt_server_port, 5);
  wifiManager.addParameter(&WiFiManager_mqtt_server_port);

  sprintf(mqtt_server_cli_id, "office_light");
  WiFiManagerParameter WiFiManager_mqtt_client_id("cli_id", "mqtt client id", mqtt_server_cli_id, 19);
  wifiManager.addParameter(&WiFiManager_mqtt_client_id);

  sprintf(mqtt_dev_short, "dev1");
  WiFiManagerParameter WiFiManager_mqtt_client_short("sid", "mqtt short id", mqtt_dev_short, 5);
  wifiManager.addParameter(&WiFiManager_mqtt_client_short);

  sprintf(mqtt_user, "user");
  WiFiManagerParameter WiFiManager_mqtt_server_login("login", "mqtt login", mqtt_user, 15);
  wifiManager.addParameter(&WiFiManager_mqtt_server_login);

  sprintf(mqtt_pw, "pw");
  WiFiManagerParameter WiFiManager_mqtt_server_pw("pw", "mqtt pw", mqtt_pw, 15);
  wifiManager.addParameter(&WiFiManager_mqtt_server_pw);
  // prepare wifimanager variables

  // start wifi manager
  Serial.println();
  Serial.println();
  Serial.print("INFO: Connecting to Wifi ");
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(MAX_AP_TIME);
  if(!wifiManager.autoConnect(CONFIG_SSID)){
    // possible situataion: Main power out, ESP went to config mode as the routers wifi wasn available on time .. 
    Serial.println("failed to connect and hit timeout, restarting ..");
    delay(1000); // time for serial to print
    ESP.reset(); // reset loop if not only or configured after 5min .. 
  }

  sprintf(mqtt_server_ip, "%s", WiFiManager_mqtt_server_ip.getValue());
  sprintf(mqtt_user, "%s", WiFiManager_mqtt_server_login.getValue());
  sprintf(mqtt_pw, "%s", WiFiManager_mqtt_server_pw.getValue());
  sprintf(mqtt_server_port, "%s", WiFiManager_mqtt_server_port.getValue());
  sprintf(mqtt_server_cli_id, "%s", WiFiManager_mqtt_client_id.getValue());
  sprintf(mqtt_dev_short, "%s", WiFiManager_mqtt_client_short.getValue());

  Serial.println(("=== Loaded parameters: ==="));
  Serial.print(("mqtt ip: ")); Serial.println(mqtt_server_ip);
  Serial.print(("mqtt port: ")); Serial.println(mqtt_server_port);
  Serial.print(("mqtt user: ")); Serial.println(mqtt_user);
  Serial.print(("mqtt pw: ")); Serial.println(mqtt_pw);
  Serial.print(("mqtt client id: ")); Serial.println(mqtt_server_cli_id);
  Serial.print(("mqtt dev short: ")); Serial.println(mqtt_dev_short);
  Serial.println(("=== End of parameters ==="));
  // start wifi manager

#if DHT_DS_MODE == DHT
  // dht
  dht.begin();
#endif

  Serial.println("");
  Serial.println("INFO: WiFi connected");
  Serial.print("INFO: IP address: ");
  Serial.println(WiFi.localIP());

  // init the MQTT connection
  client.setServer(mqtt_server_ip, atoi(mqtt_server_port));
  client.setCallback(callback);

  // attache interrupt code for PIR
  pinMode(PIR_PIN, INPUT);
  digitalWrite(PIR_PIN, HIGH); // pull up to avoid interrupts without sensor
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), updatePIRstate, CHANGE);

  // attache interrupt code for button
  pinMode(BUTTON_INPUT_PIN, INPUT);
  digitalWrite(BUTTON_INPUT_PIN, HIGH); // pull up to avoid interrupts without sensor
  attachInterrupt(digitalPinToInterrupt(BUTTON_INPUT_PIN), updateBUTTONstate, FALLING);

}
///////////////////////////////////////////// SETUP ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////// LOOP ///////////////////////////////////
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  //// send periodic updates of temperature ////
  if (millis() - timer > UPDATE_TEMP) {
    timer = millis();
    // request temp here
    publishTemperature(getTemp());
#if DHT_DS_MODE == DHT
    publishHumidity(getHumidity());
#endif
    publishRssi(WiFi.RSSI());
  }
  //// send periodic updates of temperature ////

  //// dimming active?  ////
  if (m_pwm_dimm_state == DIMM_DIMMING) {
    // check timestep (255ms to .. forever)
    if (timer_dimmer + m_pwm_dimm_time >= millis()) {
      m_pwm_dimm_time = millis(); // save for next round

      // set new value
      if (m_light_brightness < m_pwm_dimm_target) {
        m_light_brightness++;
      } else {
        m_light_brightness--;
      }

      // stop once you reach target
      if (m_light_brightness == m_pwm_dimm_target) {
        m_pwm_dimm_state = DIMM_DONE;
      }

      // set and publish
      setPWMLightState();
    }
  }
  //// dimming end ////

  //// publish all state - ONLY after being connected for sure ////
  if (m_pir_state != m_published_pir_state) {
    publishPirState();
  }

  if (m_simple_light_state != m_published_simple_light_state) {
    publishSimpleLightState();
  }

  if (m_pwm_light_state != m_published_pwm_light_state) {
    publishPWMLightState();
  }

  if (m_published_light_brightness != m_light_brightness) {
    publishPWMLightBrightness();
  }
  //// publish all state - ONLY after being connected for sure ////
}
////////////////////////////////////////////// LOOP ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
