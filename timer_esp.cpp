#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// Display
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// WiFi credentials
const char* ssid = "";  
const char* password = "";

// NTP Client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// Encoder pins
#define CLK 19
#define DT  18
#define SW  23

// Timer options
const int timerOptions[] = {1, 5, 10, 30, 60, 120, 240}; // minutes
const int numOptions = sizeof(timerOptions) / sizeof(timerOptions[0]);
int selection = 0;

// Timer states
bool countdownActive = false;
bool finishedState = false;
unsigned long countdownStartMillis = 0;
unsigned long countdownDurationMillis = 0;

// Encoder states
int lastCLKState;
bool lastSWState = HIGH;
bool buttonPressed = false;

// Finished state blinking
bool finishedBlinkState = false;
unsigned long lastBlinkMillis = 0;
const unsigned long blinkInterval = 500;

// Standby mode
bool standbyMode = true;
unsigned long lastButtonPressTime = 0;
const unsigned long doubleClickTime = 400;

// Time keeping
unsigned long timeOffset = 0;
int currentHour = 12;
int currentMinute = 0;
int currentSecond = 0;

// Timezone settings for Austria
const int standardTimeOffset = 3600;    // UTC+1 (Normalzeit)
const int daylightTimeOffset = 7200;    // UTC+2 (Sommerzeit)
bool isDaylightSavingTime = false;

void setup() {
  Serial.begin(115200);
  Serial.println("Timer Ready");

  pinMode(CLK, INPUT_PULLUP);
  pinMode(DT, INPUT_PULLUP);
  pinMode(SW, INPUT_PULLUP);

  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);

  lastCLKState = digitalRead(CLK);

  // Connect to WiFi and sync time
  connectToWiFi();
  syncTimeWithNTP();
}

void loop() {
  if (!standbyMode && WiFi.status() != WL_CONNECTED) {
    if (standbyMode) {
      connectToWiFi();
      syncTimeWithNTP();
    }
  }

  handleStandbyButton();
  
  if (standbyMode) {
    updateInternalTime();
    showDateTime();
    return;
  }

  if (!countdownActive && !finishedState) {
    handleEncoder();
    handleButtonStart();
    showSelection();
  } else if (countdownActive) {
    handleButtonReset();
    updateCountdown();
  } else if (finishedState) {
    handleFinishedScreen();
    handleButtonReset();
  }
}

// Automatische Sommerzeit-Erkennung für Österreich
bool checkForDaylightSavingTime(time_t timestamp) {
  struct tm *timeinfo = localtime(&timestamp);
  int year = timeinfo->tm_year + 1900;
  
  // Berechnung für EU Sommerzeitregelung:
  // Letzter Sonntag im März 1:00 UTC bis letzter Sonntag im Oktober 1:00 UTC
  
  // März
  time_t marchLastSunday = getLastSundayOfMonth(year, 3);
  time_t octoberLastSunday = getLastSundayOfMonth(year, 10);
  
  // Sommerzeit beginnt am letzten Sonntag im März um 1:00 UTC
  time_t dstStart = marchLastSunday + (1 * 3600);
  
  // Sommerzeit endet am letzten Sonntag im Oktober um 1:00 UTC
  time_t dstEnd = octoberLastSunday + (1 * 3600);
  
  return (timestamp >= dstStart && timestamp < dstEnd);
}

time_t getLastSundayOfMonth(int year, int month) {
  struct tm tm;
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = 31;
  tm.tm_hour = 0;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;
  
  // Gehe zum letzten Tag des Monats
  mktime(&tm);
  
  // Gehe zurück zum letzten Sonntag
  while (tm.tm_wday != 0) { // 0 = Sonntag
    tm.tm_mday--;
    mktime(&tm);
  }
  
  return mktime(&tm);
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi...");
  display.clearBuffer();
  display.setCursor(0, 15);
  display.print("Verbinde WiFi...");
  display.sendBuffer();

  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nVerbunden!");
    display.clearBuffer();
    display.setCursor(0, 15);
    display.print("WiFi verbunden");
    display.sendBuffer();
    delay(1000);
  } else {
    Serial.println("\nVerbindung fehlgeschlagen!");
    display.clearBuffer();
    display.setCursor(0, 15);
    display.print("WiFi Fehler");
    display.sendBuffer();
    delay(1000);
    setInternalTime(12, 0, 0);
  }
}

void syncTimeWithNTP() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Synchronisiere Zeit...");
    display.clearBuffer();
    display.setCursor(0, 15);
    display.print("Synchronisiere Zeit...");
    display.sendBuffer();

    timeClient.begin();
    if (timeClient.update()) {
      time_t epochTime = timeClient.getEpochTime();
      isDaylightSavingTime = checkForDaylightSavingTime(epochTime);
      
      // Setze den richtigen Offset basierend auf Sommerzeit
      timeClient.setTimeOffset(isDaylightSavingTime ? daylightTimeOffset : standardTimeOffset);
      
      // Hole die aktualisierte Zeit
      timeClient.forceUpdate();
      
      int hour = timeClient.getHours();
      int minute = timeClient.getMinutes();
      int second = timeClient.getSeconds();
      
      setInternalTime(hour, minute, second);
      
      Serial.printf("Zeit synchronisiert: %02d:%02d:%02d (%s)\n", 
                   hour, minute, second, 
                   isDaylightSavingTime ? "Sommerzeit" : "Normalzeit");
      
      display.clearBuffer();
      display.setCursor(0, 15);
      display.print(isDaylightSavingTime ? "Sommerzeit" : "Normalzeit");
      display.sendBuffer();
      delay(1000);
    } else {
      Serial.println("Zeitsynchronisation fehlgeschlagen!");
      display.clearBuffer();
      display.setCursor(0, 15);
      display.print("Zeitsync fehlgeschlagen");
      display.sendBuffer();
      delay(1000);
      setInternalTime(12, 0, 0);
    }
  }
}

void setInternalTime(int hour, int minute, int second) {
  currentHour = hour;
  currentMinute = minute;
  currentSecond = second;
  timeOffset = millis() - ((hour * 3600UL + minute * 60UL + second) * 1000UL);
}

void updateInternalTime() {
  unsigned long elapsedMillis = millis() - timeOffset;
  unsigned long totalSeconds = elapsedMillis / 1000;
  
  currentSecond = totalSeconds % 60;
  currentMinute = (totalSeconds / 60) % 60;
  currentHour = (totalSeconds / 3600) % 24;
}

void handleStandbyButton() {
  bool currentSWState = digitalRead(SW);
  
  if (lastSWState == HIGH && currentSWState == LOW) {
    unsigned long currentTime = millis();
    
    if (currentTime - lastButtonPressTime < doubleClickTime) {
      standbyMode = !standbyMode;
      if (!standbyMode) {
        display.clearBuffer();
        display.sendBuffer();
      }
    }
    
    lastButtonPressTime = currentTime;
    buttonPressed = true;
  } else {
    buttonPressed = false;
  }
  
  lastSWState = currentSWState;
}

void showDateTime() {
  display.clearBuffer();
  
  // Time display
  display.setFont(u8g2_font_logisoso24_tr);
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", currentHour, currentMinute, currentSecond);
  int timeWidth = display.getUTF8Width(timeStr);
  display.setCursor((128 - timeWidth) / 2, 40);
  display.print(timeStr);
  
  display.sendBuffer();
}

void handleEncoder() {
  int currentCLKState = digitalRead(CLK);
  if (currentCLKState != lastCLKState && currentCLKState == HIGH) {
    if (digitalRead(DT) == HIGH) {
      selection = (selection + 1) % numOptions;
    } else {
      selection = (selection - 1 + numOptions) % numOptions;
    }
  }
  lastCLKState = currentCLKState;
}

void handleButtonStart() {
  if (buttonPressed && !standbyMode && !countdownActive) {
    startCountdown();
    buttonPressed = false;
  }
}

void handleButtonReset() {
  if (buttonPressed) {
    if (finishedState) {
      finishedState = false;
      countdownActive = false;
      showSelection();
    } else if (countdownActive) {
      countdownActive = false;
      showSelection();
    }
    buttonPressed = false;
  }
}

void startCountdown() {
  countdownDurationMillis = timerOptions[selection] * 60UL * 1000UL;
  countdownStartMillis = millis();
  countdownActive = true;
  Serial.print("Timer started: ");
  Serial.print(timerOptions[selection]);
  Serial.println(" minutes");
}

void updateCountdown() {
  unsigned long elapsed = millis() - countdownStartMillis;
  unsigned long remaining = (elapsed < countdownDurationMillis) ? (countdownDurationMillis - elapsed) : 0;

  if (remaining <= 0) {
    countdownActive = false;
    finishedState = true;
    lastBlinkMillis = millis();
    Serial.println("Timer finished!");
  }
  
  showRemaining(remaining);
}

void showSelection() {
  display.clearBuffer();
  display.drawRFrame(0, 0, 128, 64, 8);

  display.setFont(u8g2_font_ncenB10_tr);
  display.setCursor((128 - display.getUTF8Width("Select Time:")) / 2, 18);
  display.print("Select Time:");

  display.setFont(u8g2_font_ncenB18_tr);
  char buf[10];
  snprintf(buf, sizeof(buf), "%d min", timerOptions[selection]);
  int textWidth = display.getUTF8Width(buf);
  display.setCursor((128 - textWidth) / 2, 50);
  display.print(buf);

  display.sendBuffer();
}

void showRemaining(unsigned long msRemaining) {
  int totalSeconds = msRemaining / 1000;
  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;

  hours = max(hours, 0);
  minutes = max(minutes, 0);
  seconds = max(seconds, 0);

  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);

  display.clearBuffer();
  display.drawRFrame(0, 0, 128, 64, 8);
  display.drawRFrame(2, 2, 124, 60, 6);

  display.setFont(u8g2_font_logisoso24_tr);
  int textWidth = display.getUTF8Width(buffer);
  display.setCursor((128 - textWidth) / 2, 45);
  display.print(buffer);

  display.sendBuffer();
}

void handleFinishedScreen() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastBlinkMillis >= blinkInterval) {
    finishedBlinkState = !finishedBlinkState;
    lastBlinkMillis = currentMillis;
    
    if (finishedBlinkState) {
      display.clearBuffer();
      display.drawRFrame(0, 0, 128, 64, 8);
      display.drawRFrame(2, 2, 124, 60, 6);

      display.setFont(u8g2_font_logisoso24_tr);
      const char* finishedTime = "00:00:00";
      int textWidth = display.getUTF8Width(finishedTime);
      display.setCursor((128 - textWidth) / 2, 45);
      display.print(finishedTime);

      display.sendBuffer();
    } else {
      display.clearBuffer();
      display.sendBuffer();
    }
  }
}