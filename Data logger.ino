
/*
150VDC ----[ 1MΩ ]----+----[ 33/40kΩ ]---- GND
                      |
                    A0 (Arduino Analog Pin)
                      |
                5.1V Zener to GND (protection)

*/
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <U8x8lib.h>

// -------- OLED --------
U8X8_SSD1306_128X32_UNIVISION_HW_I2C display(U8X8_PIN_NONE); // SDA=A4, SCL=A5

RTC_DS3231 rtc;

// Voltage divider for 150V range
const float R1 = 1000000.0f; 
//const float R2 = 33000.0f;
const float R2 = 36000.0f;   

// Pins
const uint8_t PIN_CH1   = A0;
const uint8_t PIN_CH2   = A1;
const uint8_t PIN_SD_CS = 10;
const uint8_t PIN_START = 6;
const uint8_t PIN_STOP  = 7;

// Logging
unsigned long LOG_INTERVAL_MS = 10;
unsigned long lastLog = 0;
bool logging = false;

// Interval options
const uint16_t intervals[] = {1, 10, 20, 30, 40, 50, 100, 500, 1000};
uint8_t intervalIndex = 1;  // default 10 ms

// Debounce
bool lastStartState = HIGH;
bool lastStopState  = HIGH;
unsigned long pressStartTime = 0;
const unsigned long HOLD_TIME_MS = 1000; // hold 1s to start logging

// File
File logFile;
char currentFile[16];

// Buffers
static char tsBuf[20];
static char lineBuf[64];
static char vbuf1[12], vbuf2[12];

void twoDigits(char *dst, uint8_t v) {
  dst[0] = '0' + (v / 10);
  dst[1] = '0' + (v % 10);
}

void formatTimestamp(DateTime& now, char *out) {
  int y = now.year();
  out[0] = '0' + (y / 1000) % 10;
  out[1] = '0' + (y / 100)  % 10;
  out[2] = '0' + (y / 10)   % 10;
  out[3] = '0' + (y % 10);
  out[4] = '-';
  twoDigits(out + 5, now.month());
  out[7] = '-';
  twoDigits(out + 8, now.day());
  out[10] = ',';
  twoDigits(out + 11, now.hour());
  out[13] = ':';
  twoDigits(out + 14, now.minute());
  out[16] = ':';
  twoDigits(out + 17, now.second());
  out[19] = '\0';
}

bool openNewFile() {
  for (uint16_t i = 1; i < 10000; i++) {
    snprintf(currentFile, sizeof(currentFile), "log%u.csv", i);
    if (!SD.exists(currentFile)) {
      logFile = SD.open(currentFile, FILE_WRITE);
      if (!logFile) return false;
      logFile.println(F("Date,Time,Channel1 Voltages(V),Channel2 Voltage(V)"));
      logFile.flush();
      return true;
    }
  }
  return false;
}

void startLogging() {
  if (!openNewFile()) {
    display.clear();
    display.drawString(0, 0, "  SD open fail");
    return;
  }
  logging = true;
  display.clear();
  display.drawString(0, 0, "  Logging...");
  display.drawString(2, 1, currentFile);
}

void stopLogging() {
  if (!logging) return;
  logFile.flush();
  logFile.close();
  logging = false;

  display.clear();
  display.drawString(0, 0, "  Logging: OFF");
  display.drawString(2, 1, currentFile);
  delay(1000);
}

void showIntervalMenu() {
  display.clear();
  display.drawString(0, 0, "  Set Interval:");
  char buf[12];
  snprintf(buf, sizeof(buf), "%u ms", intervals[intervalIndex]);
  display.drawString(2, 2, buf);
}

void setup() {
  pinMode(PIN_CH1,   INPUT);
  pinMode(PIN_CH2,   INPUT);
  pinMode(PIN_START, INPUT_PULLUP);
  pinMode(PIN_STOP,  INPUT_PULLUP);

  Wire.begin();
  Wire.setClock(400000);

  display.begin();
  display.setPowerSave(0);
  display.clear();
  display.setFont(u8x8_font_chroma48medium8_r);

  if (!rtc.begin()) {
    display.drawString(0, 0, "  RTC fail");
    while (1) {}
  }
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  if (!SD.begin(PIN_SD_CS)) {
    display.clear();
    display.drawString(0, 0, "  SD fail");
    while (1) {}
  }

  showIntervalMenu();
}

void loop() {
  // --- Menu Mode ---
  if (!logging) {
    bool startState = digitalRead(PIN_START);
    bool stopState = digitalRead(PIN_STOP);

    // Increment interval on START press (one-shot)
    if (lastStartState == HIGH && startState == LOW) {
      pressStartTime = millis();  // start timing hold
    }
    if (lastStartState == LOW && startState == HIGH) {
      unsigned long pressDuration = millis() - pressStartTime;
      if (pressDuration < HOLD_TIME_MS) { 
        intervalIndex = (intervalIndex + 1) % (sizeof(intervals)/sizeof(intervals[0]));
        showIntervalMenu();
      }
    }

    // Hold START to start logging
    if (lastStartState == LOW && startState == LOW && (millis() - pressStartTime > HOLD_TIME_MS)) {
      LOG_INTERVAL_MS = intervals[intervalIndex];
      startLogging();
    }

    lastStartState = startState;
    lastStopState = stopState;
    return;
  }

  // --- Logging Mode ---
  bool stopState = digitalRead(PIN_STOP);
  if (lastStopState == HIGH && stopState == LOW) {
    stopLogging();
    showIntervalMenu();
  }
  lastStopState = stopState;

  unsigned long nowMs = millis();
  if (nowMs - lastLog < LOG_INTERVAL_MS) return;
  lastLog = nowMs;

  int raw1 = analogRead(PIN_CH1);
  int raw2 = analogRead(PIN_CH2);

  float v1 = (raw1 * (5.0f / 1023.0f)) * ((R1 + R2) / R2);
  float v2 = (raw2 * (5.0f / 1023.0f)) * ((R1 + R2) / R2);

  DateTime now = rtc.now();
  formatTimestamp(now, tsBuf);

  dtostrf(v1, 0, 2, vbuf1);
  dtostrf(v2, 0, 2, vbuf2);

  snprintf(lineBuf, sizeof(lineBuf), "%s,%s,%s", tsBuf, vbuf1, vbuf2);
  logFile.println(lineBuf);

  display.clearLine(2);
  display.clearLine(3);
  display.drawString(0, 2, "  CH1:");
  display.drawString(5, 2, vbuf1);
  display.drawString(0, 3, "  CH2:");
  display.drawString(5, 3, vbuf2);
}
