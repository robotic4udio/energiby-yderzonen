/* ----------------------------------------------------------------------
"Pixel dust" Protomatter library example. As written, this is
SPECIFICALLY FOR THE ADAFRUIT MATRIXPORTAL with 64x32 pixel matrix.
Change "HEIGHT" below for 64x64 matrix. Could also be adapted to other
Protomatter-capable boards with an attached LIS3DH accelerometer.

PLEASE SEE THE "simple" EXAMPLE FOR AN INTRODUCTORY SKETCH,
or "doublebuffer" for animation basics.
------------------------------------------------------------------------- */

#include <Wire.h>                 // For I2C communication
#include <Adafruit_LIS3DH.h>      // For accelerometer
#include <Adafruit_PixelDust.h>   // For sand simulation
#include <Adafruit_Protomatter.h> // For RGB matrix
#include <WiFi.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <MIDI.h>
#include <Bounce.h>
#include <elapsedMillis.h>

// ------------------------------------------------------------------------------------- //
// -------------------------------------- The Storage of Waste ------------------------- //
float storage = 1.0;
void set_storage(OSCMessage &msg)
{
  if     (msg.isFloat(0) ) storage = msg.getFloat(0);
  else if(msg.isDouble(0)) storage = msg.getDouble(0);
  Serial.print("/WasteStorage: ");
  Serial.println(storage);
}

// ---------------------------------------------------------------------------- //
// ------------------ Wifi ---------------------------------------------------- //
// ---------------------------------------------------------------------------- //
char ssid[] = "EnergibyYderZonen";   // your network SSID (name)
char pass[] = "oioioioi";            // your network password

// ------------------ Open Sound Control -------------------------------------- //
WiFiUDP Udp;                                 // A UDP instance to let us send and receive packets over UDP
const unsigned int localPort = 7134;         // local port to listen for OSC packets (actually not used for sending)
IPAddress OutIp(192,168,1,100);              // remote IP of Raspberry Pi running the main code

OSCErrorCode error;

// buffers for receiving and sending data
char str[100];  // buffer to hold incoming packet,

void sendOsc(OSCMessage& msg, const IPAddress& ip, const unsigned int port){
  Udp.beginPacket(ip, port);
  msg.send(Udp);
  Udp.endPacket();
}

bool setupWiFi(){
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println("");

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("MatrixPortal ESP32-S3 MAC Address: ");
  Serial.println(WiFi.macAddress());

  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(localPort);
  
  return true;
}


// -------------------------------------------------------------------------- //
// ------------------ Matrix ------------------------------------------------ //
// -------------------------------------------------------------------------- //
#define HEIGHT  32 // Matrix height (pixels) - SET TO 64 FOR 64x64 MATRIX!
#define WIDTH   64 // Matrix width (pixels)
#define MAX_FPS 45 // Maximum redraw rate, frames/second

// MatrixPortal ESP32-S3
uint8_t rgbPins[]  = {42, 41, 40, 38, 39, 37};
uint8_t addrPins[] = {45, 36, 48, 35, 21};
uint8_t clockPin   = 2;
uint8_t latchPin   = 47;
uint8_t oePin      = 14;

#if HEIGHT == 16
#define NUM_ADDR_PINS 3
#elif HEIGHT == 32
#define NUM_ADDR_PINS 4
#elif HEIGHT == 64
#define NUM_ADDR_PINS 5
#endif

Adafruit_Protomatter matrix(
  WIDTH, 4, 1, rgbPins, NUM_ADDR_PINS, addrPins,
  clockPin, latchPin, oePin, true);


#define N_COLORS   8
#define BOX_HEIGHT 8
uint16_t colors[N_COLORS];


uint32_t prevTime = 0; // Used for frames-per-second throttle

// SETUP - RUNS ONCE AT PROGRAM START --------------------------------------

void err(int x) {
  uint8_t i;
  pinMode(LED_BUILTIN, OUTPUT);       // Using onboard LED
  for(i=1;;i++) {                     // Loop forever...
    digitalWrite(LED_BUILTIN, i & 1); // LED on/off blink to alert user
    delay(x);
  }
}

void setup(void) {
  Serial.begin(115200);
  while (!Serial) delay(10);

  setupWiFi();


  ProtomatterStatus status = matrix.begin();
  Serial.printf("Protomatter begin() status: %d\n", status);

  colors[0] = matrix.color565(64, 64, 64);  // Dark Gray
  colors[1] = matrix.color565(120, 79, 23); // Brown
  colors[2] = matrix.color565(228,  3,  3); // Red
  colors[3] = matrix.color565(255,140,  0); // Orange
  colors[4] = matrix.color565(255,237,  0); // Yellow
  colors[5] = matrix.color565(  0,128, 38); // Green
  colors[6] = matrix.color565(  0, 77,255); // Blue
  colors[7] = matrix.color565(117,  7,135); // Purple
}

// MAIN LOOP - RUNS ONCE PER FRAME OF ANIMATION ----------------------------

// Loop OSC to check for incoming messages and dispatch
bool loopOSC(){
  bool newOSC = false;
  OSCMessage msg;
  int size = Udp.parsePacket();

  if(size > 0){
    while (size--){
      msg.fill(Udp.read());
    }
    if(!msg.hasError()){
      msg.dispatch("/WasteStorage", set_storage);
      newOSC = true;
    } 
    else {
      error = msg.getError();
      Serial.print("error: ");
      Serial.println(error);
    }
  }
  return newOSC;
}

void loop() {
  // Check for incoming OSC messages and dispatch
  loopOSC();

  // Limit the animation frame rate to MAX_FPS.  Because the subsequent sand
  // calculations are non-deterministic (don't always take the same amount
  // of time, depending on their current states), this helps ensure that
  // things like gravity appear constant in the simulation.
  uint32_t t;
  while(((t = micros()) - prevTime) < (1000000L / MAX_FPS));
  prevTime = t;


  // Update pixel data in LED driver
  dimension_t x, y;
  matrix.fillScreen(0x0);
  for(int i=0; i<HEIGHT; i++) {
    for(x=0; x<WIDTH*storage; x++) {
      uint16_t color = colors[0];
      matrix.drawPixel(x, i, color);
      //Serial.printf("(%d, %d)\n", x, y);
    }
  }

  matrix.show(); // Copy data to matrix buffers
}

