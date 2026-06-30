#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define PIN_NEOPIXEL 48  
#define NUMPIXELS    1

Adafruit_NeoPixel pixel(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  delay(1500); 

  pinMode(PIN_NEOPIXEL, OUTPUT);
  digitalWrite(PIN_NEOPIXEL, LOW);
  delay(100);
  
  pixel.begin();
  pixel.setBrightness(40); 
  pixel.clear();
  pixel.show(); 
  delay(100);
  Serial.println("System Booted Successfully - Starting LED Loop!");
}

void loop() {
  // Cycle Green
  pixel.setPixelColor(0, pixel.Color(0, 255, 0));
  pixel.show();
  delay(1000);

  // Cycle Blue
  pixel.setPixelColor(0, pixel.Color(0, 0, 255));
  pixel.show();
  delay(1000);
}
