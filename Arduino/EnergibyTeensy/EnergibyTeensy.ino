#include <NativeEthernet.h>
#include <NativeEthernetUdp.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <Adafruit_NeoPixel.h>
#include <Bounce.h>

#define PRINT_DEBUG

/*
14: Fill Button - Red
15: Fill Button - Green
16: Fill Button - SW
17: Start Button - SW
18: Start Button - LED
19: Fill Button - Blue
20: Charge Button - SW
21: Use Button - SW
22: Charge Button - LED
23: Use Button - LED
24: Air Pot (A10)
26: 8x8  Matrix
27: Silo Inside Strip
33: 3x26 Left Side
35: 3x14 Vind, Sol, Affald
36: 2x15 Silo ControlPanel
37: 2x1 Led -- Byen mangler fjernvarme, Byen mangler strøm
38: Varme i Vejen
*/



// A variable to know how long the LED has been turned on
elapsedMillis ledOnMillis;

// NeoPixel Led Strips
#define NUM_NEOPIXEL_STRIPS 7
const unsigned char NeoPixelPin[NUM_NEOPIXEL_STRIPS]   = {26, 33, 36, 35, 37, 38 , 27};
const unsigned char NeoPixelCount[NUM_NEOPIXEL_STRIPS] = {64, 78, 30, 42,  2, 166, 173};
Adafruit_NeoPixel strip1(NeoPixelCount[0], NeoPixelPin[0], NEO_GRB + NEO_KHZ800); // 1 - PIN 26: 8x8  Matrix
Adafruit_NeoPixel strip2(NeoPixelCount[1], NeoPixelPin[1], NEO_GRB + NEO_KHZ800); // 2 - PIN 33: 3x26 Left Side
Adafruit_NeoPixel strip3(NeoPixelCount[2], NeoPixelPin[2], NEO_GRB + NEO_KHZ800); // 3 - PIN 36: 2x15 Silo ControlPanel
Adafruit_NeoPixel strip4(NeoPixelCount[3], NeoPixelPin[3], NEO_GRB + NEO_KHZ800); // 4 - PIN 35: 3x14 Vind, Sol, Affald
Adafruit_NeoPixel strip5(NeoPixelCount[4], NeoPixelPin[4], NEO_RGB + NEO_KHZ800); // 5 - PIN 37: 2x1 Led -- Byen mangler fjernvarme, Byen mangler strøm
Adafruit_NeoPixel strip6(NeoPixelCount[5], NeoPixelPin[5], NEO_GRB + NEO_KHZ800); // 6 - PIN 38: Varme i Vejen
Adafruit_NeoPixel strip7(NeoPixelCount[6], NeoPixelPin[6], NEO_GRB + NEO_KHZ800); // 7 - PIN 27: Silo Inside Strip

Adafruit_NeoPixel* strips[NUM_NEOPIXEL_STRIPS] = {&strip1, &strip2, &strip3, &strip4, &strip5, &strip6, &strip7};

elapsedMillis pixelUpdateMillis;
unsigned long pixelUpdateInterval = 39;

#define PULSELEN 30
int pulse_vec[PULSELEN] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 30, 45, 80, 115, 150, 185, 220, 255};


bool gameRunning = false;

// Vind Variables
auto& vind_strip = strip4;
float vind = 0.0f;
float vind_max = 35.0f;
float vind_numPixels = 14;
int vind_pixelOffset = 0;

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

// Silo Variables
auto& silo_panel_strip       = strip3;
auto& silo_strip             = strip7;
float silo_available_pct     = 0.8f;


auto& city_power_status_strip = strip5;
bool city_missing_heat = true;
bool city_missing_electricity = true;

// 
bool elActive = true;
bool heatActive = true;

float time = 11.0f;
float timeOfDay = time;

void setTime(float t){
  time = t;
  timeOfDay = time;
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
  0xDA, 0xA9, 0x1E, 0x2F, 0x1E, 0xE7
};

// buffers for receiving and sending data
char str[UDP_TX_PACKET_MAX_SIZE];  // buffer to hold incoming packet,

// An EthernetUDP instance to let us send and receive packets over UDP
EthernetUDP Udp;

// ----------------------------------------- //
// ------------ Open Sound Control --------- //
// ----------------------------------------- //
const unsigned int localPort = 7134;         // local port to listen for OSC packets (actually not used for sending)

const IPAddress PiIp(192,168,0,101);        // remote IP of your computer
const unsigned int PiPort   = 7133;         // remote port to receive OSC

const IPAddress BroadCastIp(255,255,255,255);     // remote IP of your computer
const unsigned int BroadCastPort = 7255;          // remote port to receive OSC

OSCMessage oscMsg("/airSpeed");            // Outgoing OSC Message


OSCErrorCode error;

// ------------------ Sensor --------------- //
// ----------------------------------------- //
unsigned long start = millis();
unsigned long current_millis;

elapsedMillis buttonReadMillis;
unsigned long buttonReadInterval = 23;

enum buttonEnum {
  FillOven, StartGame, ChargeSilo, UseSilo
};

struct LEDButton {
  LEDButton(const int sw_pin, const int led_pin, bool a_value=false, const int debounceTime=20)
    :m_sw_pin(sw_pin)
    ,m_led_pin(led_pin)
    ,bounce(sw_pin,debounceTime)
    ,m_value(a_value)
  {}

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

  void setup(){
    pinMode(m_sw_pin, INPUT_PULLUP);
    pinMode(m_led_pin, OUTPUT);
    setLED(m_value);
  }

  const int m_sw_pin;
  const int m_led_pin;
  Bounce bounce;
  bool m_value;
};

struct RGBButton {
  RGBButton(const int sw_pin, const int r_pin, const int g_pin, const int b_pin, const int debounceTime=20)
    :m_sw_pin(sw_pin)
    ,m_r_pin(r_pin)
    ,m_g_pin(g_pin)
    ,m_b_pin(b_pin)
    ,bounce(sw_pin,debounceTime)
  {}

  void setRGB(uint8_t r, uint8_t g, uint8_t b){
    analogWrite(m_r_pin, 255-r);
    analogWrite(m_g_pin, 255-g);
    analogWrite(m_b_pin, 255-b);
  }

  void setup(){
    pinMode(m_sw_pin, INPUT_PULLUP);
    pinMode(m_r_pin, OUTPUT);
    pinMode(m_g_pin, OUTPUT);
    pinMode(m_b_pin, OUTPUT);
    setRGB(255,0,180);
  }

  const int m_sw_pin;
  const int m_r_pin;
  const int m_g_pin;
  const int m_b_pin;
  Bounce bounce;
};


LEDButton startButton (17,18,false);
LEDButton chargeButton(20,22,false);
LEDButton useButton   (21,23,false);
RGBButton fillButton  (16,14,15,19);



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



// Windmill
const uint8_t windmill_PIN   = 9;
const uint8_t windmill_speed = 15;


// Analog Input Variables
elapsedMillis cityLightMillis;
unsigned long cityLightInterval = 29;

// --- Initialize ----------------------------------------------------------->
void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 1);    // turn *on* led

    delay(1000);

    
    pinMode(windmill_PIN, OUTPUT);
    analogWriteFrequency(windmill_PIN, 93750); // Teensy 3.0 pin 3 also changes to 375 kHz

    analogWrite(windmill_PIN, 0);

    // Setup Buttons
    Serial.print("Setting up IO channels");
    startButton .setup();
    chargeButton.setup();
    useButton   .setup();
    fillButton  .setup();

    // Setup CityLithts
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

  startButton .setValue(false);
  chargeButton.setValue(false);
  useButton   .setValue(false);
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
      msg.route   ("/Led" , oscLed);
      msg.dispatch("/ElData" , oscElData);
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
      chargeButton.bounce.update();
      useButton   .bounce.update();
      fillButton  .bounce.update();


      if(startButton.bounce.fallingEdge()){
          startButtonElapsed = 0;
          activity = true;
      }
      else if(startButton.bounce.risingEdge()){
          reset();
          sendCmd(chargeButton.getValue() ? "ChargeSiloT" : "ChargeSiloF");
          sendCmd(useButton   .getValue() ? "UseSiloT"    : "UseSiloF"   );
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
      if(useButton.bounce.fallingEdge()){
          sendCmd(useButton.toggle() ? "UseSiloT" : "UseSiloF");
          activity = true;
      }
      if(chargeButton.bounce.fallingEdge()){
          sendCmd(chargeButton.toggle() ? "ChargeSiloT" : "ChargeSiloF");
          activity = true;
      }


      cityStatusLoop();

      buttonReadMillis = 0;
    }

    return activity;
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

  if(amountInStorage) fillButton.setRGB(255,  0, 180);
  else                fillButton.setRGB(  0,  0,   0);

  startButton.setLED(gameRunning);
}

void siloPixelLoop(){
  setBarLed(silo_panel_strip, 0 , 15, silo_available_pct, 255, 0, 0, false);
  setBarLed(silo_panel_strip, 15, 15, silo_available_pct, 255, 0, 0, true );
  // setBarLed(silo_strip      , 0, silo_strip.numPixels(),silo_available_pct, 255, 0,0,true );

  uint32_t color;
  for(int i=0; i<silo_strip.numPixels(); i++){
    float pct = 1.0 - static_cast<float>(i)/static_cast<float>(silo_strip.numPixels());
    color = silo_available_pct < pct ? Adafruit_NeoPixel::Color(0,0,255) : Adafruit_NeoPixel::Color(255,0,0);  
    silo_strip.setPixelColor(i, color);
  }

  // Show the strip
  silo_strip.show();
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
    
    if(!chargeButton.getValue() && i < 11) color = 0;
    else if(!useButton.getValue() && i >= 11 && i < 14) color = 0;

    strip6.setPixelColor(i, color);
  }
  

  counter++;
  strip6.show();  
}


void pixelLoop(){
  auto tmpVind = gameRunning ? vind : 0.0f;
  if(pixelUpdateMillis > pixelUpdateInterval){
    setBarLed(vind_strip,vind_pixelOffset,vind_numPixels,tmpVind/vind_max,0,0,255,false);
    setBarLed(sol_strip ,sol_pixelOffset ,sol_numPixels ,sol/sol_max  ,255,100,0,false);
    setBarLed(bio_strip ,bio_pixelOffset ,bio_numPixels ,bio  ,0,255,0,true);
    setBarLed(strip2, 26, 26, oscAirSpeed, 0  , 100, 255, false);
    setBarLed(strip2, 52, 26, bio, 200,  55,   0, false );
    ovenPixelLoop();
    siloPixelLoop();
    heatPixelLoop();
    
    if(tmpVind > 0){
      analogWrite(windmill_PIN, 20+windmill_speed*tmpVind/vind_max);
    }
    else {
      analogWrite(windmill_PIN, 0);
    }


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
      if(x.update(time) && i < elFilter.value*NUM_cityLights){
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
 
  if(msg.isFloat(0)) vind               = msg.getFloat(0);
  if(msg.isFloat(1)) sol                = msg.getFloat(1);
  if(msg.isFloat(2)) bio                = msg.getFloat(2);
  if(msg.isFloat(3)) amountInOven       = msg.getFloat(3);
  if(msg.isFloat(4)) amountInStorage    = msg.getFloat(4);
  if(msg.isFloat(5)) production         = msg.getFloat(5);
  if(msg.isFloat(6)) productionMin      = msg.getFloat(6);
  if(msg.isInt  (7)) gameRunning        = msg.getInt  (7);
  if(msg.isFloat(8)) silo_available_pct = msg.getFloat(8);
  if(msg.isFloat(9)) setTime(msg.getFloat(9));

  Serial.print("Vind: ");               Serial.println(vind);
  Serial.print("Sol: ");                Serial.println(sol);
  Serial.print("Bio: ");                Serial.println(bio);
  Serial.print("amountInOven: ");       Serial.println(amountInOven);
  Serial.print("amountInStorage: ");    Serial.println(amountInStorage);
  Serial.print("production: ");         Serial.println(production);
  Serial.print("productionMin: ");      Serial.println(productionMin);
  Serial.print("gameRunning: ");        Serial.println(gameRunning);
  Serial.print("silo_available_pct: "); Serial.println(silo_available_pct);
  Serial.print("time: ");               Serial.print(time);
  Serial.print(", DayTime: ");          Serial.println(timeOfDay);

  productionPercent = production / productionMin;
  if(productionPercent > 1.0f) productionPercent = 1.0f;
}

void oscReset(OSCMessage& msg){

}
 

 

void oscLed(OSCMessage& msg, int addr_offset){
  Serial.println("Led Received");
}
 
