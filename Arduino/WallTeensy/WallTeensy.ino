#include <NativeEthernet.h>
#include <NativeEthernetUdp.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <Adafruit_NeoPixel.h>
#include <Bounce.h>

#define PRINT_DEBUG


// A variable to know how long the LED has been turned on
elapsedMillis ledOnMillis;

// NeoPixel Led Strips
#define NUM_NEOPIXEL_STRIPS 8
const unsigned char NeoPixelPin[NUM_NEOPIXEL_STRIPS]   = { 34     ,  35   , 36 , 37, 38, 39, 40, 41};
const unsigned long NeoPixelCount[NUM_NEOPIXEL_STRIPS] = {245+183 , 182   , 283, 20, 99, 100, 101, 102};
Adafruit_NeoPixel strip1(NeoPixelCount[0], NeoPixelPin[0], NEO_GRB + NEO_KHZ800); // 1 - PIN 34: Boiler + HeatEx_to_Boiler
Adafruit_NeoPixel strip2(NeoPixelCount[1], NeoPixelPin[1], NEO_GRB + NEO_KHZ800); // 2 - PIN 35: HeatExchanger
Adafruit_NeoPixel strip3(NeoPixelCount[2], NeoPixelPin[2], NEO_GRB + NEO_KHZ800); // 3 - PIN 36: Boiler_to_HeatEx
Adafruit_NeoPixel strip4(NeoPixelCount[3], NeoPixelPin[3], NEO_GRB + NEO_KHZ800); // 4 - PIN 37
Adafruit_NeoPixel strip5(NeoPixelCount[4], NeoPixelPin[4], NEO_GRB + NEO_KHZ800); // 5 - PIN 38: Turb
Adafruit_NeoPixel strip6(NeoPixelCount[5], NeoPixelPin[5], NEO_GRB + NEO_KHZ800); // 6 - PIN 39: Wind
Adafruit_NeoPixel strip7(NeoPixelCount[6], NeoPixelPin[6], NEO_GRB + NEO_KHZ800); // 7 - PIN 40: Solar
Adafruit_NeoPixel strip8(NeoPixelCount[7], NeoPixelPin[7], NEO_GRB + NEO_KHZ800); // 8 - PIN 41: Europa

Adafruit_NeoPixel* strips[NUM_NEOPIXEL_STRIPS] = {&strip1, &strip2, &strip3, &strip4, &strip5, &strip6, &strip7, &strip8};

elapsedMillis pixelUpdateMillis;
unsigned long pixelUpdateInterval = 1;

#define PULSELEN 60
uint8_t pulse_l = 30;
int pulse_vec[PULSELEN] = {
  pulse_l , pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l , 
  pulse_l , pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l, pulse_l , 
  45      , 50     , 55     , 60     , 65     , 70     , 75     , 80      ,85     , 90      ,
  95      , 100    , 100    , 100    , 100    , 100    , 100    , 100     ,100    , 110     ,
  120     , 130    , 140    , 150    , 160    , 170    , 180    , 190     ,200    , 205     ,
  210     , 215    , 220    , 225    , 230    , 235    , 240    , 245     ,250    , 255               
};


class LEDPulse {
public:
  LEDPulse(Adafruit_NeoPixel& strip, bool reverse, int startPixel = 0, int numPixels = -1):m_strip(strip),m_reverse(reverse),m_startPixel(startPixel),m_numPixels(numPixels == -1 ? strip.numPixels() : numPixels){}

  void update(float h_in, float h_out, float gain = 1.0f){
    auto N = m_numPixels;
    
    for(int i=0; i<N; i++){
      int index = i;
      int l = (i-m_counter) % PULSELEN;

      if(m_reverse){
        index = N-i-1;
        l = (i-m_counter) % PULSELEN;
      }

      float pos = (float)index / (float)N;
      float h = h_in * (1.0f - pos) + h_out * pos;

      float a = pulse_vec[l];

      uint32_t color = Adafruit_NeoPixel::Color(gain*h*a,0,gain*a*(1.0-h));      
      m_strip.setPixelColor(index+m_startPixel, color);
    }
    m_counter++;
    m_strip.show();
  }

  void updateRGB(float r_in, float g_in, float b_in, float r_out, float g_out, float b_out, float gain = 1.0f){
    auto N = m_numPixels;
    
    for(int i=0; i<N; i++){
      int index = i;
      int l = (i-m_counter) % PULSELEN;

      if(m_reverse){
        index = N-i-1;
        l = (i-m_counter) % PULSELEN;
      }

      float pos = (float)index / (float)N;
      float r = r_in * (1.0f - pos) + r_out * pos;
      float g = g_in * (1.0f - pos) + g_out * pos;
      float b = b_in * (1.0f - pos) + b_out * pos;

      float a = pulse_vec[l];

      uint32_t color = Adafruit_NeoPixel::Color(gain*r*a, gain*g*a, gain*b*a);      
      m_strip.setPixelColor(index+m_startPixel, color);
    }
    m_counter++;
    m_strip.show();
  }


private:
  Adafruit_NeoPixel& m_strip;
  bool m_reverse;
  int m_startPixel;
  int m_numPixels;
  unsigned long m_counter = 0;
};


LEDPulse pulse_boiler(strip1, true, 0, 245);
LEDPulse pulse_heatEx_to_boiler(strip1, true, 245, 183);


LEDPulse pulse_heatEx1(strip2, false, 0 , 91);
LEDPulse pulse_heatEx2(strip2, false, 91, 91);

LEDPulse pulse_boiler_to_heatEx(strip3, true);


LEDPulse pulse_turb   (strip5, true);
LEDPulse pulse_wind   (strip6, true);
LEDPulse pulse_solar  (strip7, true);
LEDPulse pulse_europa (strip8, true);


bool gameRunning = false;







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
unsigned long current_millis;




// --- Initialize ----------------------------------------------------------->
void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 1);    // turn *on* led

    delay(1000);

    

    // NeoPixel Strips
    Serial.print("Init NeoPixel Strips");
    for(int i=0; i<NUM_NEOPIXEL_STRIPS; i++){
      strips[i]->begin();
      strips[i]->show();
      strips[i]->setBrightness(255);
    }


    Serial.println("....done");

    Serial.print("Test NeoPixel Strips - ColorWipe");
    colorWipe(Adafruit_NeoPixel::Color(  0, 0,   255)     , 2); // Green
    // colorWipe(Adafruit_NeoPixel::Color(  0,   0, 255)     , 2); // Blue
    // colorWipe(Adafruit_NeoPixel::Color(255, 255, 255)     , 2); // White
    // while(1) {}
    // colorWipe(Adafruit_NeoPixel::Color(  0,   0,   0)     , 2); // Black
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
      msg.getAddress(str);
      Serial.println(str);
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





void pixelLoop(){
  if(pixelUpdateMillis < pixelUpdateInterval) return;
  

  pulse_boiler .update(1.0f, 0.0f);
  pulse_boiler_to_heatEx.update(1.0f, 1.0f);
  pulse_heatEx1.update(1.0f, 0.0f);
  pulse_heatEx2.update(0.0f, 1.0f);
  pulse_heatEx_to_boiler.update(0.0f, 0.0f);

  pulse_turb  .updateRGB(1.0f, 0.5f, 0.0f, 1.0f, 0.5f, 0.0f);
  pulse_wind  .updateRGB(1.0f, 0.5f, 0.0f, 1.0f, 0.5f, 0.0f);
  pulse_solar .updateRGB(1.0f, 0.5f, 0.0f, 1.0f, 0.5f, 0.0f);
  pulse_europa.updateRGB(1.0f, 0.5f, 0.0f, 1.0f, 0.5f, 0.0f);

  pixelUpdateMillis = 0;
}


void loop(){
  current_millis = millis();    
  bool oscActivity    = loopOsc();
  bool activity = oscActivity;

  if(!oscActivity){
    pixelLoop();
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

 

 
