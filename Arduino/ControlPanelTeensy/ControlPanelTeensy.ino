#include <NativeEthernet.h>
#include <NativeEthernetUdp.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <Adafruit_NeoPixel.h>
#include <Bounce.h>

#define PRINT_DEBUG

// Push Button Pins
#define BU1_START_PIN 26
#define BU2_FILL_OVEN_PIN 27
#define BU3_ENABLE_WIND_PIN 28
#define BU4_ENABLE_SUN_PIN 29
#define BU5_ENABLE_PLANT_PIN 30
#define BU6_BUY_ELECTRICITY_PIN 31
#define BU7_SELL_ELECTRICITY_PIN 32
#define BU8_SPARE_PIN 33

// Potentiometer Pins
#define POT1_AIR_SPEED_PIN 14    // A0
#define POT2_ENERGY_DIST_PIN 15  // A1
#define POT3_CaCO3_DOSING_PIN 16 // A2
#define POT4_NaOH_DOSING_PIN 17  // A3
#define POT1_VCC_PIN 18 // A4 : 3.3V -- remember to connect GND
#define POT2_VCC_PIN 19 // A5 : 3.3V -- remember to connect GND
#define POT3_VCC_PIN 20 // A6 : 3.3V -- remember to connect GND
#define POT4_VCC_PIN 21 // A7 : 3.3V -- remember to connect GND

// LED Pins
#define LED1_START_BUTTON_PIN 0
#define LED2_FILL_OVEN_PIN 1
#define LED3_ENABLE_WIND_PIN 2
#define LED4_ENABLE_SUN_PIN 3
#define LED5_ENABLE_PLANT_PIN 4
#define LED6_BUY_ELECTRICITY_PIN 5
#define LED7_SELL_ELECTRICITY_PIN 6
#define LED8_OVEN_TEMPERATURE_ALARM_PIN 7
#define LED9_ACID_EMISSIONS_ALARM_PIN 8
#define LED10_CO_EMISSIONS_ALARM_PIN 9
#define LED11_SPARE_PIN 10
#define LED12_SPARE_PIN 11

// LED Strip Pins
#define STRIP1_PIN 34
#define STRIP2_PIN 35
#define STRIP3_PIN 36
#define STRIP4_PIN 37
#define STRIP5_PIN 38
#define STRIP6_PIN 39
#define STRIP7_PIN 40
#define STRIP8_PIN 41

// MOSFET Pins
#define VU_METER_OVEN_TEMP_PIN 22
#define VU_METER_ACID_EMISSIONS_PIN 23
#define VU_METER_CO_EMISSIONS_PIN 24

// A variable to know how long the LED has been turned on
elapsedMillis ledOnMillis;

// NeoPixel Led Strips
#define NUM_NEOPIXEL_STRIPS 7
const unsigned char NeoPixelPin[NUM_NEOPIXEL_STRIPS]   = {34, 35, 36, 37, 38, 39, 40};
const unsigned char NeoPixelCount[NUM_NEOPIXEL_STRIPS] = {20, 20, 20, 20, 20, 20, 20};
Adafruit_NeoPixel strip1(NeoPixelCount[0], NeoPixelPin[0], NEO_GRB + NEO_KHZ800); // 1 - PIN 26: 8x8  Matrix
Adafruit_NeoPixel strip2(NeoPixelCount[1], NeoPixelPin[1], NEO_GRB + NEO_KHZ800); // 2 - PIN 33: 3x26 Left Side
Adafruit_NeoPixel strip3(NeoPixelCount[2], NeoPixelPin[2], NEO_GRB + NEO_KHZ800); // 3 - PIN 36:
Adafruit_NeoPixel strip4(NeoPixelCount[3], NeoPixelPin[3], NEO_GRB + NEO_KHZ800); // 4 - PIN 35: 3x14 Vind, Sol, Affald
Adafruit_NeoPixel strip5(NeoPixelCount[4], NeoPixelPin[4], NEO_RGB + NEO_KHZ800); // 5 - PIN 37: 2x1 Led -- Byen mangler fjernvarme, Byen mangler strøm
Adafruit_NeoPixel strip6(NeoPixelCount[5], NeoPixelPin[5], NEO_GRB + NEO_KHZ800); // 6 - PIN 38: Varme i Vejen
Adafruit_NeoPixel strip7(NeoPixelCount[6], NeoPixelPin[6], NEO_GRB + NEO_KHZ800); // 7 - PIN 27: 

Adafruit_NeoPixel* strips[NUM_NEOPIXEL_STRIPS] = {&strip1, &strip2, &strip3, &strip4, &strip5, &strip6, &strip7};

elapsedMillis pixelUpdateMillis;
unsigned long pixelUpdateInterval = 39;

#define PULSELEN 30
int pulse_vec[PULSELEN] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 30, 45, 80, 115, 150, 185, 220, 255};


bool gameRunning = false;


// Sol Variables
auto& sol_strip = strip4;
float sol = 0.0f;
float sol_max = 5.0f;
float sol_numPixels = 14;
int sol_pixelOffset = 14;

// Bio Variables
auto& bio_strip = strip4;
float bio_numPixels = 14;
int bio_pixelOffset = 28;
float bio = 0;

// Ilt
auto& ilt_strip = strip2;
float ilt_numPixels = 26;
int ilt_pixelOffset = 26;

// Ovn
int amountInOven = 0;
int amountInOven_ok_min = 8;
int amountInOven_ok_max = 18;
int amountInOven_max = 26;
int amountInStorage = 64;

// Production
float production = 1.0;
float productionMin = 1.0f;
float productionPercent = 1.0f;


auto& city_power_status_strip = strip5;
bool city_missing_heat = true;
bool city_missing_electricity = true;

// 
bool elActive = true;
bool heatActive = true;

float g_time = 11.0f;
float timeOfDay = g_time;

void setTime(float t){
  g_time = t;
  timeOfDay = g_time;
  while(timeOfDay > 24.0f) timeOfDay -= 24.0f;
}

// A Lowpass Filter
struct OnePole {
  OnePole(float aAlpha = 0.1, float aValue = 0.0f):alpha(aAlpha),value(aValue),in(aValue){}

  float alpha;
  float value;
  float in;

  void  setValue(float v){ 
    value = v; 
  }

  float getValue(){ 
    return value; 
  }

  float process(float aIn){
    in = aIn;
    value = in*alpha + value*(1.0-alpha);
    return value;
  }

};

inline int wrap(int i, int N){
  while(i >= N) i -= N;
  while(i < 0 ) i += N;
  return i;
}

OnePole elFilter  (0.02 ,1.0);
OnePole heatFilter(0.01 ,1.0);

float elAmount = 1.0;
float heatAmount = 1.0;


elapsedMillis startButtonElapsed;

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = {
  0xDA, 0xB6, 0x2F, 0x2F, 0x1E, 0xE7
};

// buffers for receiving and sending data
char str[UDP_TX_PACKET_MAX_SIZE];  // buffer to hold incoming packet,

// An EthernetUDP instance to let us send and receive packets over UDP
EthernetUDP Udp;

// ----------------------------------------- //
// ------------ Open Sound Control --------- //
// ----------------------------------------- //
const unsigned int localPort = 7134;         // local port to listen for OSC packets (actually not used for sending)

const IPAddress PiIp(192,168,0,100);        // remote IP of your computer
const unsigned int PiPort   = 7133;         // remote port to receive OSC


OSCMessage oscMsg("/airSpeed");            // Outgoing OSC Message


OSCErrorCode error;

// ------------------ Sensor --------------- //
// ----------------------------------------- //
unsigned long start = millis();
unsigned long current_millis;

elapsedMillis buttonReadMillis;
unsigned long buttonReadInterval = 23;


struct LEDButton  {
  LEDButton(const int sw_pin, const int led_pin, bool a_value=false, const int debounceTime=20)
    :m_sw_pin(sw_pin)
    ,m_led_pin(led_pin)
    ,bounce(sw_pin,debounceTime)
    ,m_value(a_value)
  {}

  void setup(){
    pinMode(m_sw_pin, INPUT_PULLUP);
    pinMode(m_led_pin, OUTPUT);
    setLED(m_value);
  }

  void setLED(bool value){
    digitalWrite(m_led_pin, value);
  }

  void setValue(bool value, bool set_led=true){
    m_value = value;
    if(set_led) setLED(m_value);
  }

  bool getValue(){
    return m_value;
  }

  bool toggle(bool set_led=true){
    setValue(!m_value,set_led);
    return m_value;
  }

  const int m_sw_pin;
  const int m_led_pin;
  Bounce bounce;
  bool m_value;
};


struct LEDMeter {
  LEDMeter(Adafruit_NeoPixel& strip_ref, int startPixel, int numPixels, float min_value=0, float max_value=1)
    :m_strip_ref(strip_ref)
    ,m_startPixel(startPixel)
    ,m_numPixels(numPixels)
    ,m_min_value(min_value)
    ,m_max_value(max_value)
  {

  }

  void set_value(float value){
    m_value = value;
  }

  void update(bool show=false){
    if(m_max_value == m_min_value){
      for(int i=0; i<m_numPixels; i++) {
        m_strip_ref.setPixelColor(m_startPixel+i, 0);
      }
      if(show) m_strip_ref.show();
      return;
    }

    for(int i=0; i<m_numPixels; i++) {
      m_strip_ref.setPixelColor(m_startPixel+i, 0);
    }

    if(m_min_value < 0 && m_max_value > 0){
      int negativePixels = m_numPixels / 2;
      int positivePixels = m_numPixels / 2;
      int positiveStart = negativePixels + (m_numPixels % 2);

      if(m_value < 0 && negativePixels > 0){
        float clampedValue = m_value < m_min_value ? m_min_value : m_value;
        float pct = clampedValue / m_min_value;
        float numOn = negativePixels * pct;
        int numFullOn = static_cast<int>(numOn);
        float scale = numOn - numFullOn;

        for(int i=0; i<negativePixels; i++) {
          int pixelIndex = negativePixels - 1 - i;
          if(i < numFullOn) m_strip_ref.setPixelColor(m_startPixel+pixelIndex, Adafruit_NeoPixel::Color(255,0,0));
          else if(i == numFullOn && scale > 0) m_strip_ref.setPixelColor(m_startPixel+pixelIndex, Adafruit_NeoPixel::Color(255*scale,0,0));
        }
      }
      else if(m_value > 0 && positivePixels > 0){
        float clampedValue = m_value > m_max_value ? m_max_value : m_value;
        float pct = clampedValue / m_max_value;
        float numOn = positivePixels * pct;
        int numFullOn = static_cast<int>(numOn);
        float scale = numOn - numFullOn;

        for(int i=0; i<positivePixels; i++) {
          int pixelIndex = positiveStart + i;
          if(i < numFullOn) m_strip_ref.setPixelColor(m_startPixel+pixelIndex, Adafruit_NeoPixel::Color(255,0,0));
          else if(i == numFullOn && scale > 0) m_strip_ref.setPixelColor(m_startPixel+pixelIndex, Adafruit_NeoPixel::Color(255*scale,0,0));
        }
      }
    }
    else {
      float clampedValue = m_value;
      if(clampedValue < m_min_value) clampedValue = m_min_value;
      if(clampedValue > m_max_value) clampedValue = m_max_value;

      float pct = (clampedValue - m_min_value) / (m_max_value - m_min_value);
      float numOn = m_numPixels * pct;
      int numFullOn = static_cast<int>(numOn);
      float scale = numOn-numFullOn;

      for(int i=0; i<m_numPixels; i++) {
        if(i < numFullOn) m_strip_ref.setPixelColor(m_startPixel+i, Adafruit_NeoPixel::Color(255,0,0));
        else if(i == numFullOn && scale > 0) m_strip_ref.setPixelColor(m_startPixel+i, Adafruit_NeoPixel::Color(255*scale,0,0));
      }
    }
    if(show) m_strip_ref.show();                          
  }

  Adafruit_NeoPixel& m_strip_ref;
  int m_startPixel;
  int m_numPixels;

  float m_min_value = 0;
  float m_max_value = 1;
  float m_value = 0;
};

struct VU_Meter {
  VU_Meter(int pin, float min_value=0, float max_value=1)
    :m_pin(pin)
    ,m_min_value(min_value)
    ,m_max_value(max_value)
  {
  }

  void setup(){
    pinMode(m_pin, OUTPUT);
  }

  void setValue(float value){
    m_value = value;
    float pct = (m_value - m_min_value) / (m_max_value - m_min_value);
    int pwm = static_cast<int>(pct * 255);
    analogWrite(m_pin, pwm);
  }

  int m_pin;
  float m_value = 0;
  float m_min_value = 0;
  float m_max_value = 1;

};

struct Pot {
  Pot(int pin, float min_value=0, float max_value=1):m_pin(pin), m_min_value(min_value), m_max_value(max_value){}

  void setup(){
    pinMode(m_pin, INPUT);
  }

  bool update(){
    bool changed = false;
    float value = analogRead(m_pin);
    value = (value / 1023.0f) * (m_max_value - m_min_value) + m_min_value;
    if(value - m_value > m_threshold || value - m_value < -m_threshold){
      changed = true;    
      m_value = value;
    }
    return changed;
  }

  float getValue(){
    return m_value;
  }

  int m_pin;
  float m_min_value = 0;
  float m_max_value = 1;
  float m_value = 0;
  float m_threshold = 0.01;
};




// Create LED Button Objects.   (pin                      , led_pin                   , initial_value , debounceTime)
LEDButton startButton           (BU1_START_PIN            , LED1_START_BUTTON_PIN     , false         , 20);
LEDButton fillButton            (BU2_FILL_OVEN_PIN        , LED2_FILL_OVEN_PIN        , false         , 20);
LEDButton enableWindButton      (BU3_ENABLE_WIND_PIN      , LED3_ENABLE_WIND_PIN      , true          , 20);
LEDButton enableSunButton       (BU4_ENABLE_SUN_PIN       , LED4_ENABLE_SUN_PIN       , true          , 20);
LEDButton enablePlantButton     (BU5_ENABLE_PLANT_PIN     , LED5_ENABLE_PLANT_PIN     , true          , 20);
LEDButton buyElectricityButton  (BU6_BUY_ELECTRICITY_PIN  , LED6_BUY_ELECTRICITY_PIN  , false         , 20);
LEDButton sellElectricityButton (BU7_SELL_ELECTRICITY_PIN , LED7_SELL_ELECTRICITY_PIN , false         , 20);

// Create LED Meter Objects
LEDMeter ovenPctMeter            (strip1 , 0  , 20 , 0 , 1);
LEDMeter airSpeedMeter           (strip1 , 20 , 20 , 0 , 1);
LEDMeter ovenEffectMeter         (strip1 , 40 , 20 , 0 , 1);
LEDMeter turbinePctMeter         (strip1 , 60 , 20 , 0 , 1);
LEDMeter heatPctMeter            (strip1 , 80 , 20 , 0 , 1);
LEDMeter windProductionMeter     (strip2 , 0  , 26 , 0 , 1);
LEDMeter solarProductionMeter    (strip2 , 26 , 26 , 0 , 1);
LEDMeter plantProductionMeter    (strip2 , 52 , 26 , 0 , 1);
LEDMeter buySellElectricityMeter (strip2 , 78 , 8  ,-1 , 1);
LEDMeter dosing_meter1           (strip3 , 0  , 20 , 0 , 1);
LEDMeter dosing_meter2           (strip3 , 20 , 20 , 0 , 1);

// Create VU Meter Objects
VU_Meter ovenTempVU      (VU_METER_OVEN_TEMP_PIN      , 0 , 500);
VU_Meter acidEmissionsVU (VU_METER_ACID_EMISSIONS_PIN , 0 , 100);
VU_Meter coEmissionsVU   (VU_METER_CO_EMISSIONS_PIN   , 0 , 100);

// Create Potentiometer Objects
Pot airSpeedPot    (POT1_AIR_SPEED_PIN    , 0 , 1);
Pot energyDistPot  (POT2_ENERGY_DIST_PIN  , 0 , 1);
Pot CaCO3DosingPot (POT3_CaCO3_DOSING_PIN , 0 , 1);
Pot NaOHDosingPot  (POT4_NaOH_DOSING_PIN  , 0 , 1);


void setupButtons(){
  startButton           .setup();
  fillButton            .setup();
  enableWindButton      .setup();
  enableSunButton       .setup();
  enablePlantButton     .setup();
  buyElectricityButton  .setup();
  sellElectricityButton .setup();
}

void setupVUMeter(){
    ovenTempVU      .setup();
    acidEmissionsVU .setup();
    coEmissionsVU   .setup();
}

void setupPots(){
  // Power the pots with 3.3V from the Teensy, remember to connect GND as well
  // I do this because I forgot to break 3.3V out on the circuit board and I don't want to redesign the PCB
  pinMode(POT1_VCC_PIN, OUTPUT);
  pinMode(POT2_VCC_PIN, OUTPUT);
  pinMode(POT3_VCC_PIN, OUTPUT);
  pinMode(POT4_VCC_PIN, OUTPUT);

  digitalWrite(POT1_VCC_PIN, HIGH);
  digitalWrite(POT2_VCC_PIN, HIGH);
  digitalWrite(POT3_VCC_PIN, HIGH);
  digitalWrite(POT4_VCC_PIN, HIGH);

  // Setup the pot pins as input and read initial value
  airSpeedPot    .setup();
  energyDistPot  .setup();
  CaCO3DosingPot .setup();
  NaOHDosingPot  .setup();
}



// Analog Input Variables
elapsedMillis analogReadMillis;
unsigned long analogReadInterval = 31;
const uint8_t airSpeedPIN = 24;
int lastAirSpeed = 0;

// Output for lights in city

struct CityLight {
  CityLight(uint8_t a_pin, uint8_t a_pwm, bool a_on, float a_onTime, float a_offTime, float a_onTime2=666, float a_offTime2=667)
  :pin(a_pin), pwm(a_pwm), on(a_on), onTime(a_onTime), offTime(a_offTime), onTime2(a_onTime+24), offTime2(a_offTime+24), initOn(a_on){}

  bool update(float t){
    return on = (t >= onTime && t <= offTime) || (t >= onTime2 && t <= offTime2);
  }

  void init(){
    on = initOn;
  }

  uint8_t pin;
  uint8_t pwm;
  bool on;
  float onTime;
  float offTime;
  float onTime2;
  float offTime2;

  bool initOn;
};

#define NUM_cityLights 8
CityLight cityLights[NUM_cityLights] = {
  CityLight( 3, 255, true , 0.00, 48.00, 1000, 10000),   // Kraftværk Kontor
  CityLight( 4, 255, true , 10.0, 17.00, 10.0, 17.00),   // Vandtårn              
  CityLight( 5, 255, true , 0.00, 48.00, 1000, 10000),   // Kraftværk Hovedbygning
  CityLight( 6, 255, true , 6.00, 21.30, 6.00, 21.30),   // Røde Huse 
  CityLight( 8, 255, true , 7.00, 23.00, 7.00, 23.00),   // Blå / Store Hvide byhuse
  CityLight(10, 255, true , 7.70, 26.00, 7.70, 26.00),   // Stor boligblok / lille hvidt hus
  CityLight(11, 255, true , 7.40, 23.90, 7.40, 23.90),   // Firkant Blok / byhus
  CityLight(12, 255, true , 5.00, 21.00, 5.00, 21.00),   // Fabrik / Bondehus
};


// Analog Input Variables
elapsedMillis cityLightMillis;
unsigned long cityLightInterval = 29;

// --- Initialize ----------------------------------------------------------->
void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 1);    // turn *on* led

    delay(1000);

    
    // Setup Buttons
    Serial.print("Setting up IO channels");
    setupButtons();
    setupVUMeter();
    setupPots();

    // Setup CityLights
    for(int i=0; i<NUM_cityLights; i++){
      pinMode(cityLights[i].pin, OUTPUT);
      analogWrite(cityLights[i].pin, 0);
    }

    for(int i=0; i<NUM_cityLights; i++){
      analogWrite(cityLights[i].pin, cityLights[i].pwm);
      delay(300);
    }

    Serial.println("....done");

    // NeoPixel Strips
    Serial.print("Init NeoPixel Strips");
    for(int i=0; i<NUM_NEOPIXEL_STRIPS; i++){
      strips[i]->begin();
      strips[i]->show();
      strips[i]->setBrightness(50);
    }
    strip5.setBrightness(255);
    strip6.setBrightness(255);
    strip7.setBrightness(255);
    Serial.println("....done");

    Serial.print("Test NeoPixel Strips - ColorWipe");
    colorWipe(Adafruit_NeoPixel::Color(255,   0,   0)     , 2); // Red
    // colorWipe(Adafruit_NeoPixel::Color(  0, 255,   0)     , 2); // Green
    // colorWipe(Adafruit_NeoPixel::Color(  0,   0, 255)     , 2); // Blue
    // colorWipe(Adafruit_NeoPixel::Color(255, 255, 255)     , 2); // White
    // while(1) {}
    colorWipe(Adafruit_NeoPixel::Color(  0,   0,   0)     , 2); // Black
    Serial.println("....done");



    // Init Ethernet
    Serial.print("Starting Ethernet");
    Ethernet.begin(mac);
    
    // Check for Ethernet hardware present
    if(Ethernet.linkStatus() == LinkOFF) {
      Serial.println("...Ethernet cable is not connected.");
    }
    else {
        Serial.println("....done");
    }

    // start UDP
    Serial.print("Starting UDP: ");
    Udp.begin(localPort);
    Serial.print("Local port: ");
    Serial.print(localPort);
    Serial.println("....done");

    digitalWrite(LED_BUILTIN, 0);    // turn *off* led

    reset();   


}

void reset(){
  elActive   = true;
  heatActive = true;
  for(int i=0; i<NUM_cityLights; i++) cityLights[i].init();

  // Reset Buttons
  startButton.setValue(false);
  fillButton .setValue(false);
  enableWindButton.setValue(true);
  enableSunButton.setValue(true);
  enablePlantButton.setValue(true);
  buyElectricityButton.setValue(false);
  sellElectricityButton.setValue(false);

  // Reset VU Meters
  ovenTempVU.setValue(0);
  acidEmissionsVU.setValue(0);
  coEmissionsVU.setValue(0);

  setTime(12.0f);
}


// Fill strip pixels one after another with a color. Strip is NOT cleared
// first; anything there will be covered pixel by pixel. Pass in color
// (as a single 'packed' 32-bit value, which you can get by calling
// strip.Color(red, green, blue) as shown in the loop() function above),
// and a delay time (in milliseconds) between pixels.
void colorWipe(uint8_t strip, uint32_t color, int wait) {
  for(int i=0; i<strips[strip]->numPixels(); i++) { // For each pixel in strip...
    strips[strip]->setPixelColor(i, color);         //  Set pixel's color (in RAM)
    strips[strip]->show();                          //  Update strip to match
    delay(wait);                                    //  Pause for a moment
  }
}

void colorWipe(uint32_t color, int wait) {
  for(int i=0; i<NUM_NEOPIXEL_STRIPS; i++){
    colorWipe(i, color, wait);
  }
}

void sendOsc(OSCMessage& msg, const IPAddress& ip, const unsigned int port){
  Udp.beginPacket(ip, port);
  msg.send(Udp);
  Udp.endPacket();
}

/*  
  buySellElectricityMeter.update();
  dosing_meter1          .update();
  dosing_meter2          .update();
*/


bool loopOsc(){
  bool activity = false;
  // Parse Incomming OSC
  OSCMessage msg;
  int packetSize = Udp.parsePacket();

  if (packetSize) {
    #ifdef PRINT_DEBUG
    Serial.print("Received packet of size ");
    Serial.println(packetSize);
    Serial.print("From ");
    IPAddress remote = Udp.remoteIP();
    for (int i=0; i < 4; i++) {
      Serial.print(remote[i], DEC);
      if (i < 3) {
        Serial.print(".");
      }
    }
    Serial.print(", port ");
    Serial.println(Udp.remotePort());
    #endif
    
    while(packetSize--){
      msg.fill(Udp.read());
    }
    if(!msg.hasError()){
      msg.route   ("/Led" , oscLed);
      msg.dispatch("/ElData" , oscElData);

      msg.dispatch("/OvenAmount" , [](OSCMessage& m){
        float v = m.getFloat(0);
        ovenPctMeter.set_value(v);
      });

      msg.dispatch("/OvenAirFlow" , [](OSCMessage& m){
        float v = m.getFloat(0);
        airSpeedMeter.set_value(v);
      });

      msg.dispatch("/PlantPower" , [](OSCMessage& m){
        float v = m.getFloat(0);
        ovenEffectMeter.set_value(v);
      });

      msg.dispatch("/TurbinePct" , [](OSCMessage& m){
        float v = m.getFloat(0);
        turbinePctMeter.set_value(v);
        heatPctMeter.set_value(1.0f - v);
      });

      msg.dispatch("/PlantElectricPower" , [](OSCMessage& m){
        float v = m.getFloat(0);
        plantProductionMeter.set_value(v);
      });

      msg.dispatch("/WindPower" , [](OSCMessage& m){
        float v = m.getFloat(0);
        windProductionMeter.set_value(v);
      });

      msg.dispatch("/SunPower" , [](OSCMessage& m){
        float v = m.getFloat(0);
        solarProductionMeter.set_value(v);
      });
      // msg.getAddress(str);
      // Serial.println(str);
      activity = true;
    }
    else {
      error = msg.getError();
      Serial.print("error: ");
      Serial.println(error);
    }
  }
  return activity;
}

void sendCmd(const char* cmd){
    Serial.println(cmd);
    oscMsg.setAddress("/cmd");
    oscMsg.empty();
    oscMsg.add(cmd);
    sendOsc(oscMsg, PiIp, PiPort);
}

bool loopButtons(){
    bool activity = false;
    
    if(buttonReadMillis > buttonReadInterval){

      // Update Buttons
      startButton .bounce.update();
      fillButton  .bounce.update();


      if(startButton.bounce.fallingEdge()){
          startButtonElapsed = 0;
          activity = true;
      }
      else if(startButton.bounce.risingEdge()){
          reset();
          sendAirSpeed();
          if(startButtonElapsed < 2000){
            sendCmd("StartButton");
          }
          else {
            sendCmd("Reset");
            reset();
          }
          activity = true;
      }
      if(fillButton.bounce.fallingEdge()){
          sendCmd("FillButton");
          activity = true;
      }


      cityStatusLoop();

      buttonReadMillis = 0;
    }

    return activity;
}

bool loopLEDMeters()
{
  bool activity = false;
  ovenPctMeter           .update();
  airSpeedMeter          .update();
  ovenEffectMeter        .update();
  turbinePctMeter        .update();
  heatPctMeter           .update();
  windProductionMeter    .update();
  solarProductionMeter   .update();
  plantProductionMeter   .update();
  buySellElectricityMeter.update();
  dosing_meter1          .update();
  dosing_meter2          .update();
}






float oscAirSpeed = 0.0f;

void sendAirSpeed(){
  oscMsg.setAddress("/value");
  oscMsg.empty();
  oscMsg.add(oscAirSpeed);
  sendOsc(oscMsg, PiIp, PiPort);
}

bool loopAnalog(){
    bool activity = false;

    if(analogReadMillis > analogReadInterval){
      airSpeedPot    .update();
      energyDistPot  .update();
      CaCO3DosingPot .update();
      NaOHDosingPot  .update();


        int airSpeed = analogRead(airSpeedPIN);
        int diff = abs(lastAirSpeed - airSpeed);
        if(airSpeed < 4)  airSpeed = 0;
        else if(diff < 8) airSpeed = lastAirSpeed;
        if(airSpeed != lastAirSpeed){
            lastAirSpeed = airSpeed;
            int airSpeedMidi = airSpeed/8;
            oscAirSpeed = (airSpeedMidi / 128.);
            sendAirSpeed();
            activity = true;
            Serial.print("Air: ");
            Serial.println(oscAirSpeed);
        }
        analogReadMillis = 0;
    }
    return activity;
}

void ovenPixelLoop(){
  for(int i=0; i<26; i++){
    if(i < amountInOven){
      uint32_t color = Adafruit_NeoPixel::Color(255,0,0);
      if(i > amountInOven_ok_min && i < amountInOven_ok_max) color = Adafruit_NeoPixel::Color(0,255,0);
      strip2.setPixelColor(i, color);
    }
    else {
      strip2.setPixelColor(i, 0);
    }
  }
  strip2.show();

  for(int i=0; i<strip1.numPixels(); i++){
    if(i < amountInStorage){
      uint32_t color = Adafruit_NeoPixel::Color(255,0,128);
      strip1.setPixelColor(strip1.numPixels()-1-i, color);
    }
    else {
      strip1.setPixelColor(i, 0);
    }
  }
  strip1.show();
  
  fillButton .setLED(amountInStorage);
  startButton.setLED(gameRunning);
}


void heatPixelLoop(){
  static unsigned long counter = 0;
  float h = heatFilter.value;
  auto N = strip6.numPixels();
  float a_min = 1;
  float a = a_min;

  for(int i=0; i<N; i++){
    uint32_t color = Adafruit_NeoPixel::Color(a_min*h,0,a_min*(1.0-h));

    a = pulse_vec[(i-counter)%PULSELEN];
    color = Adafruit_NeoPixel::Color(a*h,0,a*(1.0-h));
    
    strip6.setPixelColor(i, color);
  }
  

  counter++;
  strip6.show();  
}


void pixelLoop(){
  if(pixelUpdateMillis > pixelUpdateInterval){
    setBarLed(sol_strip ,sol_pixelOffset ,sol_numPixels ,sol/sol_max  ,255,100,0,false);
    setBarLed(bio_strip ,bio_pixelOffset ,bio_numPixels ,bio  ,0,255,0,true);
    setBarLed(strip2, 26, 26, oscAirSpeed, 0  , 100, 255, false);
    setBarLed(strip2, 52, 26, bio, 200,  55,   0, false );
    ovenPixelLoop();
    heatPixelLoop();
    
    pixelUpdateMillis = 0;
  }
}

void cityStatusLoop(){
    bool productionOk = productionPercent >= 1.0f;
    elAmount   = elActive   && productionPercent > 0.70f ? 1.0f : 0.0f;
    heatAmount = heatActive && productionPercent > 0.95f ? 1.0f : 0.0f;

    heatFilter.process(heatAmount);
    elFilter  .process(elAmount);

    city_missing_heat        = heatFilter.getValue() < 0.9;
    city_missing_electricity = elFilter  .getValue() < 0.01;

    city_power_status_strip.setPixelColor(0, city_missing_heat        ? 255 : 0, 0, 0);
    city_power_status_strip.setPixelColor(1, city_missing_electricity ? 255 : 0, 0, 0);
    city_power_status_strip.show();
}

void cityLightsLoop(){
  if(cityLightMillis > cityLightInterval){

    for(int i=0; i<NUM_cityLights; i++){
      auto& x = cityLights[i];
      if(x.update(g_time) && i < elFilter.value*NUM_cityLights){
        analogWrite(x.pin, x.pwm * elFilter.value);
      }
      else {
        analogWrite(x.pin, 0);
      }
    }

    cityLightMillis = 0;
  }
}

void loop(){
  current_millis = millis();    
  bool oscActivity    = loopOsc();
  bool buttonActivity = loopButtons();
  bool analogActivity = loopAnalog();
  bool activity = oscActivity || buttonActivity || analogActivity;

  if(!oscActivity){
    pixelLoop();
    cityLightsLoop();


  }



  // blink the LED when any activity has happened
  if(activity){
    digitalWriteFast(LED_BUILTIN, HIGH); // LED on
    ledOnMillis = 0;
  }
  if(ledOnMillis > 15){
    digitalWriteFast(LED_BUILTIN, LOW);  // LED off
  }

}

void setBarLed(Adafruit_NeoPixel& strip, int offset, int numPixels, float value, uint8_t r, uint8_t g, uint8_t b, bool show){
  float numOn = numPixels * value;
  int numFullOn = static_cast<int>(numOn);
  float scale = numOn-numFullOn;

  for(int i=0; i<numPixels; i++) { 
    if(i < numFullOn) strip.setPixelColor(offset+i, Adafruit_NeoPixel::Color(r,g,b));         
    else if (i < ceil(numOn)) strip.setPixelColor(offset+i, Adafruit_NeoPixel::Color(r*scale,g*scale,b*scale));         
    else strip.setPixelColor(offset+i,0);   
  }
  if(show) strip.show();                          
}

void oscElData(OSCMessage& msg){
  // Serial.print("ElData Received, Size: ");
  // Serial.print(msg.size());
  // Serial.print(", tags: ");
  // for(int i=0; i<msg.size(); i++){
  //   Serial.print(msg.getType(i));
  // }
  // Serial.println();
 
  if(msg.isFloat(1)) sol                = msg.getFloat(1);
  if(msg.isFloat(2)) bio                = msg.getFloat(2);
  if(msg.isFloat(3)) amountInOven       = msg.getFloat(3);
  if(msg.isFloat(4)) amountInStorage    = msg.getFloat(4);
  if(msg.isFloat(5)) production         = msg.getFloat(5);
  if(msg.isFloat(6)) productionMin      = msg.getFloat(6);
  if(msg.isInt  (7)) gameRunning        = msg.getInt  (7);
  if(msg.isFloat(8)) setTime(msg.getFloat(9));

  Serial.print("Sol: ");                Serial.println(sol);
  Serial.print("Bio: ");                Serial.println(bio);
  Serial.print("amountInOven: ");       Serial.println(amountInOven);
  Serial.print("amountInStorage: ");    Serial.println(amountInStorage);
  Serial.print("production: ");         Serial.println(production);
  Serial.print("productionMin: ");      Serial.println(productionMin);
  Serial.print("gameRunning: ");        Serial.println(gameRunning);
  Serial.print("time: ");               Serial.print(g_time);
  Serial.print(", DayTime: ");          Serial.println(timeOfDay);

  productionPercent = production / productionMin;
  if(productionPercent > 1.0f) productionPercent = 1.0f;
}

void oscReset(OSCMessage& msg){

}
 

 

void oscLed(OSCMessage& msg, int addr_offset){
  Serial.println("Led Received");
}
 
