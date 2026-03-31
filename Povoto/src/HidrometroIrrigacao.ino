/*
 * Created by ArduinoGetStarted.com
 *
 * This example code is in the public domain
 *
 * Tutorial page: https://arduinogetstarted.com/tutorials/arduino-oled
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define INTPIN 18

#define SCREEN_WIDTH 128 // OLED display width,  in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// declare an SSD1306 display object connected to I2C
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

volatile long count = 0;

void pin_ISR() {
  count++;
}

void setup() {
  Serial.begin(115200);
  Serial.println("ola");
  
  Wire.begin(5,4);

  // initialize OLED display with address 0x3C for 128x64
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true);
  }
  Serial.println("SSD1306 allocation success");

  pinMode(INTPIN,INPUT);

  delay(2000);         // wait for initializing
  oled.clearDisplay(); // clear display

  oled.setTextSize(1);          // text size
  oled.setTextColor(WHITE);     // text color
  oled.setCursor(0, 10);        // position to display
  oled.println("hidrometro"); // text to display
  oled.display();               // show on OLED
  delay(200);
  attachInterrupt(INTPIN,pin_ISR,CHANGE);
  
  pinMode(17,OUTPUT);// alimentação
  digitalWrite(17,HIGH);
  
}

long int va=0;
long int vb=0;
long int vc=0;
void loop() {
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setTextColor(WHITE);     // text color
  oled.setCursor(5, 10);        // position to display
  oled.print(count/7055.);
  vc = vb;
  vb = va;
  va = count;
  oled.setTextSize(3);
  oled.setCursor(5, 35);        // position to display
  oled.print((va-vc)/2./7.055);
  oled.display();
  delay(1000);
  
  
}
