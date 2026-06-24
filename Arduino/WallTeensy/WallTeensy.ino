#include <NativeEthernet.h>
#include <NativeEthernetUdp.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <Adafruit_NeoPixel.h>
#include <Bounce.h>

// #define PRINT_DEBUG

// MOSFET Pins
#define LAMP_BAG_FILTER 22
#define LAMP_SCRUBBER 23
#define LAMP_CACO3 24
#define LAMP_NAOH 25
struct LED {
  LED(uint8_t a_pin):m_pin(a_pin){}

  void setup(){
    pinMode(m_pin, OUTPUT);
    analogWrite(m_pin, 0);
    analogWriteFrequency(m_pin, 40000); // Set PWM frequency to 40 kHz
  }

  void set_value(uint8_t value){
    analogWrite(m_pin, value);
  }

  uint8_t m_pin;
};

LED lamp_bag_filter(LAMP_BAG_FILTER);
LED lamp_scrubber(LAMP_SCRUBBER);
LED lamp_NaOH(LAMP_NAOH);
LED lamp_CaCO3(LAMP_CACO3);

void setup_lamps(){
  lamp_bag_filter.setup();
  lamp_scrubber.setup();
  lamp_NaOH.setup();
  lamp_CaCO3.setup();
}


// A variable to know how long the LED has been turned on
elapsedMillis ledOnMillis;

// NeoPixel Led Strips
#define NUM_NEOPIXEL_STRIPS 8
#define N1_1 245 // Boiler
#define N1_2 183 // HeatEx_to_Boiler
#define N2_1 91  // HeatEx1
#define N2_2 91  // HeatEx2
#define N2_3 148 // HeatEx_to_House
#define N2_4 17  // Radiator
#define N2_5 148 // House_to_HeatEx

#define N3_1 283 // Boiler_to_HeatEx
#define N4_1 46  // Pulse_to_House
#define N4_2 10 // House Light

const unsigned char NeoPixelPin[NUM_NEOPIXEL_STRIPS]   = { 34   , 35          , 36 , 37, 38, 39, 40, 41};
const unsigned long NeoPixelCount[NUM_NEOPIXEL_STRIPS] = {N1_1+N1_2 , N2_1+N2_2+N2_3+N2_4+N2_5, N3_1, N4_1+N4_2, 99, 100, 101, 102};
Adafruit_NeoPixel strip1(NeoPixelCount[0], NeoPixelPin[0], NEO_GRB + NEO_KHZ800); // 1 - PIN 34: Boiler + HeatEx_to_Boiler
Adafruit_NeoPixel strip2(NeoPixelCount[1], NeoPixelPin[1], NEO_GRB + NEO_KHZ800); // 2 - PIN 35: HeatEx + HeatEx_to_House + Radiator + House_to_HeatEx
Adafruit_NeoPixel strip3(NeoPixelCount[2], NeoPixelPin[2], NEO_GRB + NEO_KHZ800); // 3 - PIN 36: Boiler_to_HeatEx
Adafruit_NeoPixel strip4(NeoPixelCount[3], NeoPixelPin[3], NEO_GRB + NEO_KHZ800); // 4 - PIN 37: House Light
Adafruit_NeoPixel strip5(NeoPixelCount[4], NeoPixelPin[4], NEO_GRB + NEO_KHZ800); // 5 - PIN 38: Turb
Adafruit_NeoPixel strip6(NeoPixelCount[5], NeoPixelPin[5], NEO_GRB + NEO_KHZ800); // 6 - PIN 39: Wind
Adafruit_NeoPixel strip7(NeoPixelCount[6], NeoPixelPin[6], NEO_GRB + NEO_KHZ800); // 7 - PIN 40: Solar
Adafruit_NeoPixel strip8(NeoPixelCount[7], NeoPixelPin[7], NEO_GRB + NEO_KHZ800); // 8 - PIN 41: Europa

Adafruit_NeoPixel* strips[NUM_NEOPIXEL_STRIPS] = {&strip1, &strip2, &strip3, &strip4, &strip5, &strip6, &strip7, &strip8};


elapsedMillis pixelUpdateMillis;
unsigned long pixelUpdateInterval = 1;

#define PULSELEN 60
float pulse_l = 0.12f;
float pulse_vec[PULSELEN] = {
  pulse_l , pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l , 
  pulse_l , pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l , 
  0.18f, 0.20f, 0.22f, 0.24f, 0.25f, 0.27f, 0.29f, 0.31f, 0.33f, 0.35f,
  0.37f, 0.39f, 0.39f, 0.39f, 0.39f, 0.39f, 0.39f, 0.39f, 0.39f, 0.43f,
  0.47f, 0.51f, 0.55f, 0.59f, 0.63f, 0.67f, 0.71f, 0.75f, 0.78f, 0.80f,
  0.82f, 0.84f, 0.86f, 0.88f, 0.90f, 0.92f, 0.94f, 0.96f, 0.98f, 1.00f
};

// #define NEO_GRB  ((1 << 6) | (1 << 4) | (0 << 2) | (2)) // 0x52
uint32_t mixColor(uint32_t c1, uint32_t c2, float t, float gain = 1.0f){
  float r1 = (c1 >> 16) & 0xFF;
  float g1 = (c1 >>  8) & 0xFF;
  float b1 = (c1 >>  0) & 0xFF;

  float r2 = (c2 >> 16) & 0xFF;
  float g2 = (c2 >>  8) & 0xFF;
  float b2 = (c2 >>  0) & 0xFF;

  float r = r1 * (1.0f - t) + r2 * t;
  float g = g1 * (1.0f - t) + g2 * t;
  float b = b1 * (1.0f - t) + b2 * t;

  return Adafruit_NeoPixel::Color(gain*r, gain*g, gain*b);
};

uint32_t gainColor(uint32_t c, float gain){
  uint8_t r = (c >> 16) & 0xFF;
  uint8_t g = (c >>  8) & 0xFF;
  uint8_t b = (c >>  0) & 0xFF;

  r = r * gain;
  g = g * gain;
  b = b * gain;

  return Adafruit_NeoPixel::Color(r, g, b);
};



const uint32_t COLOR_OFF = Adafruit_NeoPixel::Color(0, 0, 0);
const uint32_t COLOR_RED  = Adafruit_NeoPixel::Color(255, 0, 0);
const uint32_t COLOR_GREEN= Adafruit_NeoPixel::Color(0, 255, 0);
const uint32_t COLOR_BLUE = Adafruit_NeoPixel::Color(0, 0, 255);
const uint32_t COLOR_WHITE= Adafruit_NeoPixel::Color(255, 255, 255);
const uint32_t COLOR_YELLOW= Adafruit_NeoPixel::Color(255, 128, 0);

uint32_t colorFromHeat(float heat){
  if(heat < 0.4f){
    float mix = heat*2.5f;
    return Adafruit_NeoPixel::Color(235*mix+20, 0, 255*(1.0f-mix));
  }

  return COLOR_RED;
}

float colorGainFromEl(float value){
  if(value < 0.4f){
    return value*2.5f;
  }
  return 1.0f;
}


class LEDPulse {
public:
  LEDPulse(Adafruit_NeoPixel& strip, bool reverse, int startPixel = 0, int numPixels = -1):m_strip(strip),m_reverse(reverse),m_startPixel(startPixel),m_numPixels(numPixels == -1 ? strip.numPixels()-startPixel : numPixels){}

  void update(bool show=false){
    if(!m_run) return;
    auto N = m_numPixels;
    
    for(int i=0; i<N; i++){
      int index = i;
      int l = (i-m_counter) % PULSELEN;

      if(m_reverse){
        index = N-i-1;
        l = (i-m_counter) % PULSELEN;
      }

      // Get pulse intensity based on position in pulse vector and distance from center of pulse
      float a = pulse_vec[l] * m_gain;

      if(color1 == color2){
        auto color = gainColor(color1, a);
        m_strip.setPixelColor(index+m_startPixel, color);
      }
      else {
        // Get position based on position in strip
        float p_index = index;
        float Ngrad = N - pixelsBeforeGradient - pixelsAfterGradient;
        if(index < pixelsBeforeGradient) p_index = 0;
        else if(index > N - pixelsAfterGradient) p_index = Ngrad;
        else p_index = index - pixelsBeforeGradient;
        float pos = p_index / Ngrad;
        uint32_t color = mixColor(color1, color2, pos, a);
        m_strip.setPixelColor(index+m_startPixel, color);
      }
    }

    m_counter++;
    if(show) m_strip.show();

  }

  void setReverse(bool reverse){
    m_reverse = reverse;
  }

  void setRun(bool run, bool show=false){
    m_run = run;
    if(m_off_when_not_running && !run){
      for(int i=0; i<m_numPixels; i++){
        m_strip.setPixelColor(i+m_startPixel, color0);
      }
      if(show) m_strip.show();
    }
  }

  void setColor1(uint8_t r, uint8_t g, uint8_t b){
    color1 = Adafruit_NeoPixel::Color(r, g, b);
  }
  void setColor2(uint8_t r, uint8_t g, uint8_t b){
    color2 = Adafruit_NeoPixel::Color(r, g, b);
  }
  void setColors(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2){
    color1 = Adafruit_NeoPixel::Color(r1, g1, b1);
    color2 = Adafruit_NeoPixel::Color(r2, g2, b2);
  }
  void setColor(uint8_t r, uint8_t g, uint8_t b){
    color1 = Adafruit_NeoPixel::Color(r, g, b);
    color2 = Adafruit_NeoPixel::Color(r, g, b);
  }
  void setColors(uint32_t c1, uint32_t c2){
    color1 = c1;
    color2 = c2;
  }
  void setColor(uint32_t c){
    color1 = c;
    color2 = c;
  }    
  void setColor1(uint32_t c){
    color1 = c;
  }
  void setColor2(uint32_t c){
    color2 = c;
  }

  void setGradient(uint16_t before, uint16_t after){
    pixelsBeforeGradient = before;
    pixelsAfterGradient = after;
  }

  void setGain(float gain){
    m_gain = gain;
  }

  Adafruit_NeoPixel& m_strip;
  bool m_reverse;
  int m_startPixel;
  int m_numPixels;
  unsigned long m_counter = 0;
  bool m_run = true;
  uint32_t color0 = COLOR_OFF;
  uint32_t color1 = COLOR_RED;
  uint32_t color2 = COLOR_BLUE;
  bool m_off_when_not_running = false;

  uint16_t pixelsBeforeGradient = 0;
  uint16_t pixelsAfterGradient = 0;

  float m_gain = 1.0f;

};

// Strip 1: Boiler + HeatEx_to_Boiler
LEDPulse pulse_boiler          (strip1, true,  0, N1_1);
LEDPulse pulse_heatEx_to_boiler(strip1, true, N1_1, N1_2);

// Strip 2: HeatEx + HeatEx_to_House + Radiator
LEDPulse pulse_heatEx1        (strip2, false, 0                  , N2_1);
LEDPulse pulse_heatEx2        (strip2, false, N2_1               , N2_2);
LEDPulse pulse_heatEx_to_house(strip2, false, N2_1+N2_2          , N2_3);
LEDPulse pulse_radiator       (strip2, true , N2_1+N2_2+N2_3     , N2_4);
LEDPulse pulse_house_to_heatEx(strip2, false, N2_1+N2_2+N2_3+N2_4, N2_5);

// Strip 3: Boiler_to_HeatEx
LEDPulse pulse_boiler_to_heatEx(strip3, true, 0, N3_1);

auto& strip_house = strip4;
LEDPulse pulse_turb    (strip5, true);
LEDPulse pulse_wind    (strip6, true);
LEDPulse pulse_solar   (strip7, true);
LEDPulse pulse_europa  (strip8, true);
LEDPulse pulse_to_house(strip4, false, 0, N4_1);

bool gameRunning = false;


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

OnePole elFilter  (0.05 ,1.0);
OnePole heatFilter(0.01 ,1.0);

float elAmount = 1.0;
float heatAmount = 1.0;

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = {
  0xDC, 0xB6, 0x2F, 0x2F, 0x1E, 0xE8
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


OSCErrorCode error;

// ------------------ Sensor --------------- //
// ----------------------------------------- //
unsigned long start = millis();




// --- Initialize ----------------------------------------------------------->
void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 1);    // turn *on* led

    delay(1000);

    setup_lamps();

  

    // NeoPixel Strips
    Serial.print("Init NeoPixel Strips");
    for(int i=0; i<NUM_NEOPIXEL_STRIPS; i++){
      strips[i]->begin();
      strips[i]->clear();
      strips[i]->show();
      strips[i]->setBrightness(255);
    }


    Serial.println("....done");

    // Serial.print("Test NeoPixel Strips - ColorWipe");
    // colorWipe(Adafruit_NeoPixel::Color(  0, 0,   255)     , 2); // Green
    // colorWipe(Adafruit_NeoPixel::Color(  0,   0, 255)     , 2); // Blue
    // colorWipe(Adafruit_NeoPixel::Color(255, 255, 255)     , 2); // White
    // while(1) {}
    // colorWipe(Adafruit_NeoPixel::Color(  0,   0,   0)     , 2); // Black
    // Serial.println("....done");



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
      msg.getAddress(str);
      msg.dispatch("/Wall" , [](OSCMessage& m){
        float ovenTemp = m.getFloat(0);
        float turbinePct = m.getFloat(1);
        float useWind = m.getInt(2);
        float useSolar = m.getInt(3);
        float usePlant = m.getInt(4);
        float buy = m.getFloat(5);
        float caco3 = m.getFloat(6);
        float naoh = m.getFloat(7);

        bool power_available = useWind || useSolar || (turbinePct > 0.05 && usePlant) || buy > 0.0f;
        heatAmount = ovenTemp;
        elAmount = power_available ? 1.0f : 0.0f;
        pulse_turb.setRun(turbinePct > 0.05 && usePlant);
        pulse_wind.setRun(useWind);
        pulse_solar.setRun(useSolar);
        pulse_europa.setRun(buy != 0.0f);
        pulse_europa.setReverse(buy > 0.0f);
        // pulse_to_house.setRun(power_available);
        lamp_NaOH.set_value(naoh * 255);
        lamp_CaCO3.set_value(caco3 * 255);
      });
      msg.dispatch("/Reset" , [](OSCMessage& m){
        reset();
      });
      
      activity = true;
    }
    else {
      error = msg.getError();
      // sSerial.print("error: ");
      // sSerial.println(error);
    }
  }
  return activity;
}


void reset(){

  heatAmount = 1.0f;
  heatFilter.setValue(heatAmount);
  elAmount = 1.0f;
  elFilter.setValue(elAmount);

  pulse_heatEx_to_boiler.setColors(COLOR_BLUE, COLOR_BLUE);
  pulse_boiler.setColors(COLOR_RED, COLOR_BLUE);
  pulse_boiler.setGradient(50, 0);
  pulse_boiler_to_heatEx.setColors(COLOR_RED, COLOR_RED);
  pulse_heatEx1.setColors(COLOR_RED, COLOR_BLUE);
  pulse_heatEx1.setGradient(5, 20);
  pulse_heatEx2.setColors(COLOR_BLUE, COLOR_RED);
  pulse_heatEx2.setGradient(3, 35);
  pulse_heatEx_to_house.setColors(COLOR_RED, COLOR_RED);
  pulse_radiator.setColors(COLOR_BLUE, COLOR_RED);
  pulse_radiator.setGradient(0, 9);

  pulse_house_to_heatEx.setColors(COLOR_BLUE, COLOR_BLUE);

  strip_house .fill(COLOR_WHITE, N4_1, N4_1+N4_2);
  pulse_turb    .setColors(COLOR_YELLOW, COLOR_YELLOW);
  pulse_wind    .setColors(COLOR_YELLOW, COLOR_YELLOW);
  pulse_solar   .setColors(COLOR_YELLOW, COLOR_YELLOW);
  pulse_europa  .setColors(COLOR_YELLOW, COLOR_YELLOW);
  pulse_to_house.setColors(COLOR_YELLOW , COLOR_WHITE);

  pulse_turb    .m_off_when_not_running = true;
  pulse_wind    .m_off_when_not_running = true;
  pulse_solar   .m_off_when_not_running = true;
  pulse_europa  .m_off_when_not_running = true;
  pulse_to_house.m_off_when_not_running = true;

  lamp_bag_filter.set_value(0);
  lamp_scrubber  .set_value(0);
  lamp_NaOH      .set_value(0);
  lamp_CaCO3     .set_value(0);
}

void pixelLoop(){
  if(pixelUpdateMillis < pixelUpdateInterval) return;
  
  auto heat = heatFilter.process(heatAmount);
  auto hot_color = colorFromHeat(heat);
  auto el   = elFilter  .process(elAmount);
  pulse_boiler          .setColor1(hot_color);
  pulse_boiler_to_heatEx.setColor(hot_color);
  pulse_heatEx1         .setColor1(hot_color);
  pulse_heatEx2         .setColor2(hot_color);
  pulse_heatEx_to_house .setColor(hot_color);
  pulse_radiator        .setColor2(hot_color);

  // Heat Flow Meters
  pulse_heatEx_to_boiler.update();
  pulse_boiler.update();
  pulse_boiler_to_heatEx.update();
  pulse_heatEx1.update();
  pulse_heatEx2.update();
  pulse_heatEx_to_house.update();
  pulse_radiator.update();
  pulse_house_to_heatEx.update();

  // Electricity Meters
  float house_color_gain = colorGainFromEl(el);
  strip_house .fill(gainColor(COLOR_WHITE,house_color_gain), N4_1, N4_1+N4_2);
  pulse_to_house.setGain(house_color_gain);
  pulse_turb    .update();
  pulse_wind    .update();
  pulse_solar   .update();
  pulse_europa  .update();
  pulse_to_house.update();

  // strip show
  strip1.show();
  strip2.show();
  strip3.show();
  strip4.show();
  strip5.show();
  strip6.show();
  strip7.show();
  strip8.show();

  float bag_filter_fade = sin(millis() * 0.002f) * 0.25f + 0.5f;
  lamp_bag_filter.set_value(bag_filter_fade * 255);
  float scrubber_fade = sin(millis() * 0.003f) * 0.25f + 0.5f;
  lamp_scrubber.set_value(scrubber_fade * 255);

  // update
  pixelUpdateMillis = 0;
}


void loop(){
  bool oscActivity = loopOsc();
  bool activity = oscActivity;

  if(!oscActivity){
    pixelLoop();
  }
  
  // blink the LED when any activity has happened
  // if(activity){
  //   digitalWriteFast(LED_BUILTIN, HIGH); // LED on
  //   ledOnMillis = 0;
  // }
  // if(ledOnMillis > 15){
  //   digitalWriteFast(LED_BUILTIN, LOW);  // LED off
  // }

}

 

 
