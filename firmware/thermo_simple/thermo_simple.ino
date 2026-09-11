/*
  thermo_simple.ino
  ECE:4880 Lab 1. Read two DS18B20 probes once a second and show them on the
  16x2 LCD and the serial monitor (115200 baud). No Wi-Fi, no buttons.

  Board: ESP32 Dev Module. Libraries: OneWire, DallasTemperature, LiquidCrystal.

  Wiring:
    DS18B20 x2  DQ -> GPIO32, 4.7k pull-up to 3V3
    LCD         RS=25  E=26  D4=18  D5=19  D6=23  D7=27, R/W to ground
*/

#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal.h>

OneWire bus(32);
DallasTemperature probes(&bus);
LiquidCrystal lcd(25, 26, 18, 19, 23, 27);

void setup() {
  Serial.begin(115200);
  lcd.begin(16, 2);
  probes.begin();
  Serial.print("Probes found: ");
  Serial.println(probes.getDeviceCount());
}

void loop() {
  probes.requestTemperatures();   // waits about 750 ms for the conversion

  for (int i = 0; i < 2; i++) {
    float t = probes.getTempCByIndex(i);
    lcd.setCursor(0, i);
    lcd.print("S");
    lcd.print(i + 1);
    lcd.print("  ");
    Serial.print("S");
    Serial.print(i + 1);
    Serial.print(": ");
    if (t == DEVICE_DISCONNECTED_C) {   // -127 means no probe answered
      lcd.print("unplugged     ");
      Serial.print("unplugged");
    } else {
      lcd.print(t, 1);
      lcd.print((char)223);             // degree sign in the LCD font
      lcd.print("C      ");
      Serial.print(t, 2);
      Serial.print(" C");
    }
    Serial.print(i == 0 ? "   " : "\n");
  }

  delay(250);
}
