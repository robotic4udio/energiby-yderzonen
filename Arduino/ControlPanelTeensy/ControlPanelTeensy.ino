#include <NativeEthernet.h>
#include <NativeEthernetUdp.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <Adafruit_NeoPixel.h>
#include <Bounce2.h>

#define PRINT_DEBUG

// Push Button Pins
#define BU1_START_PIN 26
#define BU2_RESET_PIN 27
#define BU3_FILL_OVEN_PIN 28
#define BU4_ENABLE_WIND_PIN 29
#define BU5_ENABLE_SUN_PIN 30
#define BU6_ENABLE_PLANT_PIN 31
#define BU7_BUY_ELECTRICITY_PIN 32
#define BU8_SELL_ELECTRICITY_PIN 33

// Potentiometer Pins
#define POT1_AIR_SPEED_PIN 14    // A0
#define POT2_ENERGY_DIST_PIN 15  // A1
#define POT3_CaCO3_DOSING_PIN 16 // A2
#define POT4_NaOH_DOSING_PIN 17  // A3

// LED Pins
#define LED1_START_BUTTON_PIN 0
#define LED2_RESET_BUTTON_PIN 1
#define LED3_FILL_OVEN_PIN 2
#define LED4_ENABLE_WIND_PIN 3
#define LED5_ENABLE_SUN_PIN 4
#define LED6_ENABLE_PLANT_PIN 5
#define LED7_BUY_ELECTRICITY_PIN 6
#define LED8_SELL_ELECTRICITY_PIN 7
#define LED9_OVEN_TEMPERATURE_ALARM_PIN 25
#define LED10_ACID_EMISSIONS_ALARM_PIN 12
#define LED11_CO_EMISSIONS_ALARM_PIN 13

// LED Strip Pins
#define STRIP1_PIN 34
#define STRIP2_PIN 35

// MOSFET Pins
#define VU_METER_OVEN_TEMP_PIN 22
#define VU_METER_ACID_EMISSIONS_PIN 23
#define VU_METER_CO_EMISSIONS_PIN 24

// A variable to know how long the LED has been turned on
elapsedMillis ledOnMillis;

// NeoPixel Led Strips
#define NUM_NEOPIXEL_STRIPS 2
Adafruit_NeoPixel strip1(30 , STRIP1_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(154, STRIP2_PIN, NEO_GRB + NEO_KHZ800);

Adafruit_NeoPixel* strips[NUM_NEOPIXEL_STRIPS] = {&strip1, &strip2};

elapsedMillis pixelUpdateMillis;
unsigned long pixelUpdateInterval = 100;

#define PULSELEN 60
uint8_t pulse_l = 45;
int pulse_vec[PULSELEN] = {
  pulse_l , pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l , 
  pulse_l , pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l , 
  45      , 50     , 55     , 60     , 65     , 70     , 75     , 80      ,85     , 90      ,
  95      , 100    , 105    , 110    , 115    , 120    , 125    , 130     ,140    , 150     ,
  160     , 170    , 180    , 190    , 200    , 205    , 210    , 215     ,220    , 225     ,
  230     , 235    , 240    , 245    , 250    , 255    , 255    , 255     ,255    , 255               
};


//Time 
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
unsigned long buttonReadInterval = 5;


struct LEDButton {
  enum ButtonMode {MANUAL, PUSHBUTTON, TOGGLE, PUSHBUTTON_LED_ON};
  enum OSCMode {OSC_NONE, OSC_OFF, OSC_ON, OSC_CHANGE};

  LEDButton(const int sw_pin, const int led_pin, bool a_value=false, ButtonMode a_mode=MANUAL, const int debounceTime=20)
    :m_sw_pin(sw_pin)
    ,m_led_pin(led_pin)
    ,bounce(sw_pin,debounceTime)
    ,m_value(a_value)
    ,m_mode(a_mode)
  {

  }

  void setup(){
    bounce.attach(m_sw_pin, INPUT_PULLUP);
    pinMode(m_led_pin, OUTPUT);
    setLED(m_value);
  }

  bool update(){
    bool changed = bounce.update();

    switch(m_mode){
      case MANUAL:
        break;
      case PUSHBUTTON:
        if(pressed())       setValue(true , true);
        else if(released()) setValue(false, true);
        break;
      case TOGGLE:
        if(pressed()) toggle(true);
        break;
      case PUSHBUTTON_LED_ON:
        if(pressed())       setValue(true , false);
        else if(released()) setValue(false, false);
        setLED(true);
        break;
    }

    return changed;
  }

  bool pressed(){
    return bounce.fell();
  }

  bool released(){
    return bounce.rose();
  }
  
  void setLED(bool value){
    digitalWrite(m_led_pin, value);
  }

  void setValue(bool value, bool set_led=true){
    m_value = value;
    if(set_led) setLED(m_value);
    if((!m_value && oscMode == OSC_OFF) || (m_value && oscMode == OSC_ON) || (oscMode == OSC_CHANGE)){
      oscMsg.empty();
      oscMsg.add(m_value);
      sendOsc(oscMsg, PiIp, PiPort);
    }
  }

  bool getValue(){
    return m_value;
  }

  bool toggle(bool set_led=true){
    setValue(!m_value,set_led);
    return m_value;
  }

  void attachOSC(const char* address, OSCMode mode=OSC_CHANGE){
    oscMsg.setAddress(address);
    oscMode = mode;
  }

  const int m_sw_pin;
  const int m_led_pin;
  Bounce bounce;
  bool m_value;
  ButtonMode m_mode;

  OSCMode oscMode = OSC_NONE;
  OSCMessage oscMsg;
};


struct LEDMeter {
  LEDMeter(Adafruit_NeoPixel& strip_ref, int startPixel, int numPixels, float min_value=0, float max_value=1, uint8_t r=255, uint8_t g=0, uint8_t b=0)
    :m_strip_ref(strip_ref)
    ,m_startPixel(startPixel)
    ,m_numPixels(numPixels)
    ,m_min_value(min_value)
    ,m_max_value(max_value)
    ,m_r(r)
    ,m_g(g)
    ,m_b(b)
  {

  }

  void set_value(float value){
    m_value = value;
  }

  void set_color(uint8_t red, uint8_t green, uint8_t blue){
    m_r = red;
    m_g = green;
    m_b = blue;
  }

  void update(bool show=false){
    uint16_t numPixels = m_strip_ref.numPixels();
    if(m_startPixel + m_numPixels > numPixels) return; // Out of bounds


    if(false)
    {
      for(int i=0; i<m_numPixels; i++) {
        m_strip_ref.setPixelColor(m_startPixel+i, Adafruit_NeoPixel::Color(m_r,m_g,m_b));
      }
      if(show) m_strip_ref.show();
      return;
    }


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
          if(i < numFullOn) m_strip_ref.setPixelColor(m_startPixel+pixelIndex, Adafruit_NeoPixel::Color(m_r,m_g,m_b));
          else if(i == numFullOn && scale > 0) m_strip_ref.setPixelColor(m_startPixel+pixelIndex, Adafruit_NeoPixel::Color(m_r*scale,m_g*scale,m_b*scale));
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
          if(i < numFullOn) m_strip_ref.setPixelColor(m_startPixel+pixelIndex, Adafruit_NeoPixel::Color(m_r,m_g,m_b));
          else if(i == numFullOn && scale > 0) m_strip_ref.setPixelColor(m_startPixel+pixelIndex, Adafruit_NeoPixel::Color(m_r*scale,m_g*scale,m_b*scale));
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
        if(i < numFullOn) m_strip_ref.setPixelColor(m_startPixel+i, Adafruit_NeoPixel::Color(m_r,m_g,m_b));
        else if(i == numFullOn && scale > 0) m_strip_ref.setPixelColor(m_startPixel+i, Adafruit_NeoPixel::Color(m_r*scale,m_g*scale,m_b*scale));
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

  // Color
  uint8_t m_r = 255;
  uint8_t m_g = 0;
  uint8_t m_b = 0;

};

struct LED {
  LED(uint8_t a_pin):m_pin(a_pin){}

  void setup(){
    pinMode(m_pin, OUTPUT);
  }

  void set_value(bool value){
    digitalWrite(m_pin, value);
  }

  uint8_t m_pin;
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

  float getValue(){
    return m_value;
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
    // OSC Message
    if(changed && oscAttached){
      oscMsg.empty();
      oscMsg.add(m_value);
      sendOsc(oscMsg, PiIp, PiPort);
    }

    return changed;
  }

  float getValue(){
    return m_value;
  }

  void attachOSC(const char* address){
    oscMsg.setAddress(address);
    oscAttached = true;
  }

  int m_pin;
  float m_min_value = 0;
  float m_max_value = 1;
  float m_value = 0;
  float m_threshold = 0.01;


  OSCMessage oscMsg;
  bool oscAttached = false;

};




// Create LED Button Objects.   (pin                      , led_pin                   , initial_value , ButtonMode , debounceTime)
LEDButton startButton           (BU1_START_PIN            , LED1_START_BUTTON_PIN     , false        , LEDButton::TOGGLE            , 20);
LEDButton resetButton           (BU2_RESET_PIN            , LED2_RESET_BUTTON_PIN     , false        , LEDButton::PUSHBUTTON        , 20);
LEDButton fillButton            (BU3_FILL_OVEN_PIN        , LED3_FILL_OVEN_PIN        , false        , LEDButton::PUSHBUTTON_LED_ON , 20);
LEDButton enableWindButton      (BU4_ENABLE_WIND_PIN      , LED4_ENABLE_WIND_PIN      , true         , LEDButton::TOGGLE            , 20);
LEDButton enableSunButton       (BU5_ENABLE_SUN_PIN       , LED5_ENABLE_SUN_PIN       , true         , LEDButton::TOGGLE            , 20);
LEDButton enablePlantButton     (BU6_ENABLE_PLANT_PIN     , LED6_ENABLE_PLANT_PIN     , true         , LEDButton::TOGGLE            , 20);
LEDButton buyElectricityButton  (BU7_BUY_ELECTRICITY_PIN  , LED7_BUY_ELECTRICITY_PIN  , false        , LEDButton::PUSHBUTTON_LED_ON , 20);
LEDButton sellElectricityButton (BU8_SELL_ELECTRICITY_PIN , LED8_SELL_ELECTRICITY_PIN , false        , LEDButton::PUSHBUTTON_LED_ON , 20);

// Create LED Meter Objects
LEDMeter ovenPct                 (strip1 , 0  , 10 , 0 , 1, 255, 0, 100);   // Strip 1: Pixels 0-9
LEDMeter airFlow                 (strip1 , 10 , 10 , 0 , 1, 255, 255, 255);   // Strip 1: Pixels 10-19
LEDMeter plantPower              (strip1 , 20 , 10 , 0 , 1, 255, 100, 0);   // Strip 1: Pixels 20-29
LEDMeter turbinePct              (strip2 , 0  , 37 , 0 , 1, 255, 50, 50);   // Strip 2: Pixels 0-36 (37 pixels)
LEDMeter heatPct                 (strip2 , 37 , 37 , 0 , 1, 0, 255, 100);   // Strip 1: Pixels 37-73 (37 pixels)
LEDMeter windPower               (strip2 , 74 , 10 , 0 , 1, 0, 100, 255);   // Strip 2: Pixels 74-83 (10 pixels)
LEDMeter solarPower              (strip2 , 84 , 10 , 0 , 1, 255, 150, 0);   // Strip 2: Pixels 84-93 (10 pixels)
LEDMeter plantElectricPower      (strip2 , 94 , 10 , 0 , 1, 255, 50, 0); // Strip 2: Pixels 94-103 (10 pixels)
LEDMeter buySellElectricityMeter (strip2 , 104 , 10  ,-1 , 1, 0 , 255, 0);  // Strip 2: Pixels 104-113 (10 pixels, green for selling, red for buying)
LEDMeter dosing_meter1           (strip2 , 114 , 20 , 0 , 1, 100, 100, 100);// Strip 2: Pixels 114-133 (20 pixels)
LEDMeter dosing_meter2           (strip2 , 134 , 20 , 0, 1, 0, 50, 200);    // Strip 2: Pixels 134-153 (20 pixels)

// Create VU Meter Objects
VU_Meter ovenTempVU      (VU_METER_OVEN_TEMP_PIN      , 0 , 1);
VU_Meter acidEmissionsVU (VU_METER_ACID_EMISSIONS_PIN , 0 , 1);
VU_Meter coEmissionsVU   (VU_METER_CO_EMISSIONS_PIN   , 0 , 1);

// Create LED Objects
LED ovenTempAlarmLED     (LED9_OVEN_TEMPERATURE_ALARM_PIN);
LED acidEmissionsAlarmLED(LED10_ACID_EMISSIONS_ALARM_PIN);
LED coEmissionsAlarmLED  (LED11_CO_EMISSIONS_ALARM_PIN);

// Create Potentiometer Objects
Pot airSpeedPot    (POT1_AIR_SPEED_PIN    , 0 , 1);
Pot energyDistPot  (POT2_ENERGY_DIST_PIN  , 0 , 1);
Pot CaCO3DosingPot (POT3_CaCO3_DOSING_PIN , 0 , 1);
Pot NaOHDosingPot  (POT4_NaOH_DOSING_PIN  , 0 , 1);


void setupButtons(){
  startButton           .setup();
  resetButton           .setup();
  fillButton            .setup();
  enableWindButton      .setup();
  enableSunButton       .setup();
  enablePlantButton     .setup();
  buyElectricityButton  .setup();
  sellElectricityButton .setup();

  // Attach OSC messages to buttons
  startButton           .attachOSC("/Start"     , LEDButton::OSC_CHANGE);
  resetButton           .attachOSC("/Reset"     , LEDButton::OSC_ON);
  fillButton            .attachOSC("/FillOven"  , LEDButton::OSC_ON);
  enableWindButton      .attachOSC("/UseWind"   , LEDButton::OSC_CHANGE);
  enableSunButton       .attachOSC("/UseSun"    , LEDButton::OSC_CHANGE);
  enablePlantButton     .attachOSC("/UsePlant"  , LEDButton::OSC_CHANGE);
  buyElectricityButton  .attachOSC("/Buy"       , LEDButton::OSC_ON);
  sellElectricityButton .attachOSC("/Sell"      , LEDButton::OSC_ON);
}

void setupVUMeter(){
    ovenTempVU      .setup();
    acidEmissionsVU .setup();
    coEmissionsVU   .setup();
}

void setupLEDs(){
  ovenTempAlarmLED     .setup();
  acidEmissionsAlarmLED.setup();
  coEmissionsAlarmLED  .setup();
}

void setupPots(){
  airSpeedPot    .setup();
  energyDistPot  .setup();
  CaCO3DosingPot .setup();
  NaOHDosingPot  .setup();

  // Attach OSC messages to pots
  airSpeedPot    .attachOSC("/OvenAirFlow");
  energyDistPot  .attachOSC("/EnergyDist");
  CaCO3DosingPot .attachOSC("/CaCO3");
  NaOHDosingPot  .attachOSC("/NaOH");
}



// Analog Input Variables
elapsedMillis analogReadMillis;
unsigned long analogReadInterval = 31;


// --- Initialize ----------------------------------------------------------->
void setup() {
    Serial.begin(115200);

    delay(1000);

    
    // Setup Buttons
    Serial.print("Setting up IO channels");
    setupButtons();
    setupVUMeter();
    setupLEDs();
    setupPots();

    Serial.println("....done");

    // NeoPixel Strips
    Serial.print("Init NeoPixel Strips");
    for(int i=0; i<NUM_NEOPIXEL_STRIPS; i++){
      strips[i]->begin();
      strips[i]->show();
      strips[i]->setBrightness(255);
    }
    Serial.println("....done");

    while(false){
      Serial.print("Test NeoPixel Strips - ColorWipe1");
      colorWipe(0, Adafruit_NeoPixel::Color(255,   0,   0)     , 1); // Red
      Serial.print("Test NeoPixel Strips - ColorWipe2");
      colorWipe(0, Adafruit_NeoPixel::Color(0,   255,   0)     , 1); // Red
      colorWipe(0, Adafruit_NeoPixel::Color(0,   0,   255)     , 1); // Red


      // colorWipe(Adafruit_NeoPixel::Color(255,   0,   0)     , 2); // Red
      // colorWipe(Adafruit_NeoPixel::Color(  0, 255,   0)     , 2); // Green
      // colorWipe(Adafruit_NeoPixel::Color(  0,   0, 255)     , 2); // Blue
      // colorWipe(Adafruit_NeoPixel::Color(255, 255, 255)     , 2); // White
      // while(1) {}
      colorWipe(Adafruit_NeoPixel::Color(  0,   0,   0)     , 0); // Black

    }


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


    // Reset everything to default state
    reset();   
}

void reset(){
  // Reset Buttons
  startButton           .setValue(false);
  resetButton           .setValue(false);
  fillButton            .setValue(false);
  enableWindButton      .setValue(true);
  enableSunButton       .setValue(true);
  enablePlantButton     .setValue(true);
  buyElectricityButton  .setValue(false);
  sellElectricityButton .setValue(false);

  // Reset VU Meters
  ovenTempVU      .setValue(0);
  acidEmissionsVU .setValue(0);
  coEmissionsVU   .setValue(0);

  // Reset LEDs
  ovenTempAlarmLED      .set_value(false);
  acidEmissionsAlarmLED .set_value(false);
  coEmissionsAlarmLED   .set_value(false);

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
      // LED Meters
      msg.dispatch("/OvenAmount" , [](OSCMessage& m){
        float v = m.getFloat(0);
        ovenPct.set_value(v);
      });

      msg.dispatch("/OvenAirFlow" , [](OSCMessage& m){
        float v = m.getFloat(0);
        airFlow.set_value(v);
      });

      msg.dispatch("/PlantPower" , [](OSCMessage& m){
        float v = m.getFloat(0);
        plantPower.set_value(v);
      });

      msg.dispatch("/TurbinePct" , [](OSCMessage& m){
        float v = m.getFloat(0);
        turbinePct.set_value(v);
        heatPct.set_value(1.0f - v);
      });

      msg.dispatch("/PlantElectricPower" , [](OSCMessage& m){
        float v = m.getFloat(0);
        plantElectricPower.set_value(v);
      });

      msg.dispatch("/WindPower" , [](OSCMessage& m){
        float v = m.getFloat(0);
        windPower.set_value(v);
      });

      msg.dispatch("/SolarPower" , [](OSCMessage& m){
        float v = m.getFloat(0);
        solarPower.set_value(v);
      });

      msg.dispatch("/Buy" , [](OSCMessage& m){
        float v = m.getFloat(0);
        // Set Color
        if(v > 0) buySellElectricityMeter.set_color(255, 0, 0); // Red for buying
        else      buySellElectricityMeter.set_color(0, 255, 0); // Green for selling
        // Set Value
        buySellElectricityMeter.set_value(-v);
      });

      msg.dispatch("/CaCO3" , [](OSCMessage& m){
        float v = m.getFloat(0);
        dosing_meter1.set_value(v);
      });

      msg.dispatch("/NaOH" , [](OSCMessage& m){
        float v = m.getFloat(0);
        dosing_meter2.set_value(v);
      });

      // VU Meters
      msg.dispatch("/OvenTemp" , [](OSCMessage& m){
        float v = m.getFloat(0);
        ovenTempVU.setValue(v);
        ovenTempAlarmLED.set_value(v < 0.25f || v > 0.75f);
      });

      msg.dispatch("/Acid" , [](OSCMessage& m){
        float v = m.getFloat(0);
        acidEmissionsVU.setValue(v);
        acidEmissionsAlarmLED.set_value(v > 0.55f);
      });

      msg.dispatch("/CO" , [](OSCMessage& m){
        float v = m.getFloat(0);
        coEmissionsVU.setValue(v);
        coEmissionsAlarmLED.set_value(v > 0.55f);
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
      startButton          .update();
      resetButton          .update();
      fillButton           .update();
      enableWindButton     .update();
      enableSunButton      .update();
      enablePlantButton    .update();
      buyElectricityButton .update();
      sellElectricityButton.update();

      if(resetButton.pressed()){
        reset();
        activity = true;
      }

      // if(startButton.pressed()){
      //     startButtonElapsed = 0;
      //     activity = true;
      // }
      // else if(startButton.released()){
      //     reset();
      //     if(startButtonElapsed < 2000){
      //       sendCmd("StartButton");
      //       startButton.setValue(true);
      //     }
      //     else {
      //       sendCmd("Reset");
      //       reset();
      //     }
      //     activity = true;
      // }
      
      buttonReadMillis = 0;
    }

    return activity;
}

bool loopLEDMeters()
{
  bool activity = false;
  ovenPct                .update();
  airFlow                .update();
  plantPower             .update();
  turbinePct             .update();
  heatPct                .update();
  windPower              .update();
  solarPower             .update();
  plantElectricPower     .update();
  buySellElectricityMeter.update();
  dosing_meter1          .update();
  dosing_meter2          .update();

  strip1.show();
  strip2.show();
  return activity;
}

bool loopAnalog(){
    bool activity = false;

    if(analogReadMillis > analogReadInterval){
      activity |= airSpeedPot    .update();
      activity |= energyDistPot  .update();
      activity |= CaCO3DosingPot .update();
      activity |= NaOHDosingPot  .update();
      analogReadMillis = 0;
    }

    return activity;
}

void pixelLoop(){
  if(pixelUpdateMillis > pixelUpdateInterval){
    loopLEDMeters();
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
  }

  if(false){
    // Test Code - Set all values based on airSpeedPot for easy testing
  ovenPct.set_value(0.5);
  airFlow.set_value(airSpeedPot.getValue());
  plantPower.set_value(airSpeedPot.getValue());
  ovenTempVU.setValue(airSpeedPot.getValue());

  turbinePct.set_value(energyDistPot.getValue());
  heatPct.set_value(1.0f - energyDistPot.getValue());

  float kalk = CaCO3DosingPot .getValue();
  float naoh = NaOHDosingPot  .getValue();
  
  dosing_meter1.set_value(kalk);
  dosing_meter2.set_value(naoh);
  acidEmissionsVU.setValue(kalk);
  coEmissionsVU.setValue(naoh);

  windPower.set_value(airSpeedPot.getValue());
  solarPower.set_value(airSpeedPot.getValue());
  plantElectricPower.set_value(airSpeedPot.getValue());

  float buySellValue = (airSpeedPot.getValue() * 2.0f - 1.0f); // Map 0-1 to -1 to 1
  buySellElectricityMeter.set_value(buySellValue); 
  buySellElectricityMeter.set_color(buySellValue < 0 ? 255 : 0, buySellValue > 0 ? 255 : 0, 0); // Red for buying, Green for selling


  // Alarms
  ovenTempAlarmLED.set_value(airSpeedPot.getValue() > 0.8f);
  acidEmissionsAlarmLED.set_value(kalk > 0.8f);
  coEmissionsAlarmLED.set_value(naoh > 0.8f);

  } // Test Code

}
