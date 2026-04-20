

#define MOTOR_PIN 9
#define POT_PIN A0




void setup() {

  // Change analog write frequency of pin 9 to 31.25kHz (from default 490Hz) to avoid audible noise from the motor
  TCCR1B = TCCR1B & 0b11111000 | 0x01;
  
  
  // Set Pin 2 as an output
  pinMode(MOTOR_PIN, OUTPUT);
  pinMode(POT_PIN, INPUT_PULLUP);

  

}

void loop() {

  analogWrite(MOTOR_PIN,analogRead(POT_PIN));


}
