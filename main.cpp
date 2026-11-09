#include <Wire.h>
#include <GyverOLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <vector>
#include <algorithm>
#include <esp_wifi.h>

#include "main.hpp"
#include "display.hpp"
#include "input.hpp"
#include "dino.hpp"
#include "flappyBird.hpp"
#include "snake.hpp"
#include "AI.hpp"

#define PIN_SDA   12
#define PIN_SCL   13
#define PIN_UP    7
#define PIN_DOWN  8
#define PIN_OK    10
#define PIN_BAT   1

GyverOLED<SSD1306_128x64, OLED_BUFFER> oled;
GyverOLED<SSD1306_128x64, OLED_BUFFER>& display = oled;

Location UserLocation = Location::FileSelecter;
void processBat(void) {}

const int SCREEN_ROWS = 8;
const int CHARS_PER_LINE = 21;

const char* AP_SSID = "Shpora";
const char* AP_PASS = "91639163";
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer server(80);
bool wifiActive = false;

enum AppState {
  ST_MAIN,
  ST_LIST,
  ST_VIEW,
  ST_WIFI,
  ST_SETTINGS,
  ST_SETTINGS_BRIGHT,
  ST_SETTINGS_LEFT,
  ST_SETTINGS_BAT,
  ST_SETTINGS_FACTORY,
  ST_AI_CONFIG
};
AppState state = ST_MAIN;

const int MENU_COUNT = 7;
const char* menuNames[MENU_COUNT] = {
    "Шпоры",
    "Точка доступа",
    "Google Динозаврик",
    "FlappyBird",
    "Змейка",
    "Нейросеть",
    "Настройки"
};
int menuSelected = 0;

const int SETTINGS_COUNT = 4;
const char* settingsNames[SETTINGS_COUNT] = {
    "Яркость",
    "Режим левши",
    "Battery Monitor",
    "Сброс настроек"
};
int settingsSelected = 0;

bool factoryConfirmSelected = false;

bool leftHanded = false;

#define R1_KOHM   1.0f
#define R2_KOHM   1.0f
#define DIVIDER_RATIO  ((R1_KOHM + R2_KOHM) / R2_KOHM)
#define VREF      3.3f
#define ADC_MAX   4095.0f
#define V_FULL    4.20f
#define V_EMPTY   3.00f
#define BAT_SAMPLES 16

static uint32_t lastBatSample = 0;
static float filteredVoltage = 0.0f;
static float filteredPercent = 0.0f;

struct FileEntry { String fname; String title; };
std::vector<FileEntry> fileEntries;

int selected  = 0;
int listStart = 0;

String currentFileName = "";
std::vector<uint32_t> pageOffsets;
bool currentFileEmpty = false;
int page = 0;

uint8_t brightness = 200;
const uint8_t BRIGHTNESS_STEP = 15;
String lastFileName = "";

bool settingsDirty = false;
unsigned long settingsChangedAt = 0;
const unsigned long SETTINGS_SAVE_DELAY = 800;

const char* SETTINGS_PATH = "/_cfg.txt";

int scrollPos = 0;
unsigned long scrollTimer = 0;
const unsigned long SCROLL_INTERVAL = 300;
int lastScrollIdx = -1;

const int DEBOUNCE_MAX = 6;
const unsigned long LONGPRESS_MS    = 600;
const unsigned long REPEAT_START_MS = 500;
const unsigned long REPEAT_MS       = 180;

struct Btn {
  uint8_t pin;
  int counter;
  bool stable;
  unsigned long tPress, tRepeat;
  bool longFired;
  bool long1sFired;
  bool pressedEvent, releasedEvent, longEvent, repeatEvent;
};

Btn btnUp, btnDown, btnOk;

void btnInit(Btn &b, uint8_t pin);
void btnUpdate(Btn &b);
void btnUpdateFast(Btn &b);
void handleButtons();
void moveSelection(int dir);
void openSelectedFile();
void prevPage();
void nextPage();
void changeBrightness(int delta);
void toggleWifi();
void drawScreen();
void drawMain();
void drawList();
void drawView();
void drawWifiInfo();
void drawSettings();
void drawSettingsBright();
void drawSettingsLeft();
void drawSettingsBat();
void drawSettingsFactory();
void drawAiConfig();

int utf8Length(const String &s);
String utf8Substring(const String &s, int startChar, int charCount);
String marqueeText(const String &title, int width, int offset);

bool lsReadWord(File &f, String &word, bool &isBreak);
bool lsNextLine(File &f, String &line, uint32_t &lineStartPos);
void lsReset();
void buildPageIndex(const String &fname);

void markSettingsDirty();
void maybeSaveSettings();
void loadSettings();
void saveSettings();

String pathOf(const String &fn);
void refreshFileList();
unsigned long extractFileId(const String &fname);
struct FileData;
FileData readFile(const String &fname);
void writeFile(const String &fname, String title, const String &content);
String genFilename();
bool hasSpace(long extraBytes);

String htmlEscape(String s);
void redirectMsg(const char *m);
void handleRedirect();
String pageStyle();
void handleRoot();
void handleUpload();
void handleEditPage();
void handleSave();
void handleDelete();
void handleAiSave();
void setupWebServer();

float readBatteryVoltage() {
    uint32_t sum = 0;
    for (int i = 0; i < BAT_SAMPLES; i++) {
        sum += analogRead(PIN_BAT);
        delayMicroseconds(200);
    }
    float adcAvg = (float)sum / (float)BAT_SAMPLES;
    float vPin = (adcAvg / ADC_MAX) * VREF;
    float vBat = vPin * DIVIDER_RATIO;
    return vBat;
}

float voltageToPercent(float v) {
    if (v >= V_FULL)  return 100.0f;
    if (v <= V_EMPTY) return 0.0f;
    return (v - V_EMPTY) / (V_FULL - V_EMPTY) * 100.0f;
}

void drawBatteryBar(int x, int y, int w, int h, int percent) {
    oled.rect(x, y, x + w, y + h, OLED_STROKE);
    oled.rect(x + w + 1, y + h / 3, x + w + 3, y + h - h / 3, OLED_FILL);
    int innerW = w - 2;
    int innerH = h - 2;
    int fillW = (innerW * percent) / 100;
    if (fillW > 0) {
        oled.rect(x + 1, y + 1, x + 1 + fillW - 1, y + 1 + innerH - 1, OLED_FILL);
    }
}

struct FileData { String title; String content; };

#define AI_ITEM_INDEX 0

void btnInit(Btn &b, uint8_t pin) {
  b.pin = pin;
  pinMode(pin, INPUT_PULLUP);
  bool pressedNow = (digitalRead(pin) == LOW);
  b.counter = pressedNow ? DEBOUNCE_MAX : 0;
  b.stable = pressedNow;
  b.tPress = 0;
  b.tRepeat = 0;
  b.longFired = false;
  b.long1sFired = false;
  b.pressedEvent = b.releasedEvent = b.longEvent = b.repeatEvent = false;
}

void btnUpdate(Btn &b) {
  b.pressedEvent = b.releasedEvent = b.longEvent = b.repeatEvent = false;

  bool pressedNow = (digitalRead(b.pin) == LOW);

  if (pressedNow) {
    if (b.counter < DEBOUNCE_MAX) b.counter++;
  } else {
    if (b.counter > 0) b.counter--;
  }

  bool newStable = b.stable;
  if (b.counter >= DEBOUNCE_MAX) newStable = true;
  else if (b.counter <= 0) newStable = false;

  if (newStable != b.stable) {
    b.stable = newStable;
    if (b.stable) {
      b.tPress = millis();
      b.tRepeat = millis();
      b.longFired = false;
      b.long1sFired = false;
      b.pressedEvent = true;
    } else {
      b.releasedEvent = true;
    }
  }

  if (b.stable) {
    unsigned long held = millis() - b.tPress;
    if (!b.longFired && held >= LONGPRESS_MS) {
      b.longFired = true;
      b.longEvent = true;
    }
    if (!b.long1sFired && held >= 1000) b.long1sFired = true;

    if (held >= REPEAT_START_MS && millis() - b.tRepeat >= REPEAT_MS) {
      b.tRepeat = millis();
      b.repeatEvent = true;
    }
  }
}

void btnUpdateFast(Btn &b) {
  b.pressedEvent = b.releasedEvent = b.longEvent = b.repeatEvent = false;

  bool pressedNow = (digitalRead(b.pin) == LOW);

  if (pressedNow && !b.stable) {
    b.stable = true;
    b.tPress = millis();
    b.tRepeat = millis();
    b.longFired = false;
    b.long1sFired = false;
    b.pressedEvent = true;
  } else if (!pressedNow && b.stable) {
    b.stable = false;
    b.releasedEvent = true;
  }

  if (b.stable) {
    unsigned long held = millis() - b.tPress;
    if (!b.longFired && held >= LONGPRESS_MS) {
      b.longFired = true;
      b.longEvent = true;
    }
    if (!b.long1sFired && held >= 1000) b.long1sFired = true;
  }
}

static ButtonState mapLeftOk(const Btn& b) {
    if (!b.stable) return ButtonState::None;
    unsigned long held = millis() - b.tPress;
    if (held >= 1000) return ButtonState::Down;
    return ButtonState::None;
}

static ButtonState mapBtn(const Btn& b, unsigned long holdDelay = 300) {
    if (b.releasedEvent && !b.longFired) return ButtonState::Down;
    if (b.releasedEvent &&  b.longFired) return ButtonState::Up;
    if (b.stable && (millis() - b.tPress) >= holdDelay) return ButtonState::Hold;
    return ButtonState::None;
}

Input GetInput(void) {
    btnUpdateFast(btnOk);
    btnUpdateFast(btnUp);
    btnUpdateFast(btnDown);

    Btn& upRef   = leftHanded ? btnDown : btnUp;
    Btn& downRef = leftHanded ? btnUp   : btnDown;

    Input in(
        mapLeftOk(btnOk),
        mapBtn(downRef),
        mapBtn(upRef)
    );
    return in;
}

Input GetFastInput(void) {
    return GetInput();
}

void setup() {
  Serial.begin(9600);
  randomSeed(micros());

  btnInit(btnUp,   PIN_UP);
  btnInit(btnDown, PIN_DOWN);
  btnInit(btnOk,   PIN_OK);

  Wire.begin(PIN_SDA, PIN_SCL);
  oled.init();
  oled.clear();
  oled.update();

  if (!LittleFS.begin(true)) {
    Serial.println("Ошибка монтирования LittleFS");
  }

  WiFi.mode(WIFI_OFF);
  setupWebServer();
  refreshFileList();
  loadSettings();
  oled.setContrast(brightness);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BAT, ADC_11db);

  if (leftHanded) {
    oled.flipH(true);
    oled.flipV(true);
  } else {
    oled.flipH(false);
    oled.flipV(false);
  }

  esp_wifi_set_max_tx_power(WIFI_POWER_2dBm);
  state = ST_MAIN;
  menuSelected = 0;
  drawScreen();
}

void loop() {
  btnUpdate(btnUp);
  btnUpdate(btnDown);
  btnUpdate(btnOk);

  if (wifiActive) {
    dnsServer.processNextRequest();
    server.handleClient();
  }

  handleButtons();

  if (state == ST_LIST && !fileEntries.empty()) {
    if (selected > 0) {
      String title = fileEntries[selected - 1].title;
      if (utf8Length(title) > CHARS_PER_LINE - 2 && millis() - scrollTimer > SCROLL_INTERVAL) {
        scrollTimer = millis();
        scrollPos++;
        drawScreen();
      }
    }
  }

  if (state == ST_SETTINGS_BAT) {
    uint32_t now = millis();
    if (now - lastBatSample >= 500) {
      lastBatSample = now;

      float v = readBatteryVoltage();
      float p = voltageToPercent(v);

      if (filteredVoltage == 0.0f) {
        filteredVoltage = v;
        filteredPercent = p;
      } else {
        filteredVoltage = filteredVoltage * 0.8f + v * 0.2f;
        filteredPercent = filteredPercent * 0.8f + p * 0.2f;
      }
      drawSettingsBat();
      oled.update();
    }
  }

  maybeSaveSettings();
  delay(5);
}

void handleButtons() {
  switch (state) {
    case ST_MAIN:
      if (leftHanded) {
        if (btnUp.pressedEvent)   { menuSelected++; if (menuSelected >= MENU_COUNT) menuSelected = 0; drawScreen(); }
        if (btnDown.pressedEvent) { menuSelected--; if (menuSelected < 0) menuSelected = MENU_COUNT - 1; drawScreen(); }
      } else {
        if (btnUp.pressedEvent)   { menuSelected--; if (menuSelected < 0) menuSelected = MENU_COUNT - 1; drawScreen(); }
        if (btnDown.pressedEvent) { menuSelected++; if (menuSelected >= MENU_COUNT) menuSelected = 0; drawScreen(); }
      }

      if (btnOk.releasedEvent && !btnOk.longFired) {
          switch (menuSelected) {
              case 0:
                  refreshFileList();
                  selected = 0;
                  listStart = 0;
                  state = ST_LIST;
                  drawScreen();
                  break;
              case 1:
                  if (!wifiActive) toggleWifi();
                  else { state = ST_WIFI; drawScreen(); }
                  break;
              case 2:
                  dino_init(&oled);
                  dinosaurGame();
                  state = ST_MAIN;
                  drawScreen();
                  break;
              case 3:
                  flappyInit(&oled);
                  flappyBirdGame();
                  state = ST_MAIN;
                  drawScreen();
                  break;
              case 4:
                  snakeInit(&oled);
                  snakeGame();
                  state = ST_MAIN;
                  drawScreen();
                  break;
              case 5:
                  aiInit(&oled);
                  aiGame();
                  state = ST_MAIN;
                  drawScreen();
                  break;
              case 6:
                  settingsSelected = 0;
                  state = ST_SETTINGS;
                  drawScreen();
                  break;
          }
      }
      break;

    case ST_LIST:
      {
        int total = 1 + (int)fileEntries.size();
        if (leftHanded) {
          if (btnUp.pressedEvent   || btnUp.repeatEvent)   { selected = (selected + 1) % total; scrollPos = 0; lastScrollIdx = selected; drawScreen(); }
          if (btnDown.pressedEvent || btnDown.repeatEvent) { selected = (selected - 1 + total) % total; scrollPos = 0; lastScrollIdx = selected; drawScreen(); }
        } else {
          if (btnUp.pressedEvent   || btnUp.repeatEvent)   { selected = (selected - 1 + total) % total; scrollPos = 0; lastScrollIdx = selected; drawScreen(); }
          if (btnDown.pressedEvent || btnDown.repeatEvent) { selected = (selected + 1) % total; scrollPos = 0; lastScrollIdx = selected; drawScreen(); }
        }

        if (btnOk.releasedEvent && !btnOk.longFired) {
          if (selected == AI_ITEM_INDEX) {
            state = ST_AI_CONFIG;
            drawScreen();
          } else {
            int idx = selected - 1;
            if (idx >= 0 && idx < (int)fileEntries.size()) {
              selected = idx;
              openSelectedFile();
            }
          }
        }
        if (btnOk.longEvent) {
          state = ST_MAIN;
          drawScreen();
        }
      }
      break;

    case ST_VIEW:
      if (leftHanded) {
        if (btnUp.pressedEvent   || btnUp.repeatEvent)   nextPage();
        if (btnDown.pressedEvent || btnDown.repeatEvent) prevPage();
      } else {
        if (btnUp.pressedEvent   || btnUp.repeatEvent)   prevPage();
        if (btnDown.pressedEvent || btnDown.repeatEvent) nextPage();
      }
      if (btnOk.releasedEvent && !btnOk.longFired) {
        selected = selected + 1;
        state = ST_LIST;
        drawScreen();
      }
      break;

    case ST_WIFI:
      if (btnOk.longEvent) {
          if (wifiActive) toggleWifi();
          state = ST_MAIN;
          drawScreen();
      }
      break;

    case ST_SETTINGS:
      if (leftHanded) {
        if (btnUp.pressedEvent)   { settingsSelected++; if (settingsSelected >= SETTINGS_COUNT) settingsSelected = 0; drawScreen(); }
        if (btnDown.pressedEvent) { settingsSelected--; if (settingsSelected < 0) settingsSelected = SETTINGS_COUNT - 1; drawScreen(); }
      } else {
        if (btnUp.pressedEvent)   { settingsSelected--; if (settingsSelected < 0) settingsSelected = SETTINGS_COUNT - 1; drawScreen(); }
        if (btnDown.pressedEvent) { settingsSelected++; if (settingsSelected >= SETTINGS_COUNT) settingsSelected = 0; drawScreen(); }
      }
      if (btnOk.longEvent) {
          state = ST_MAIN;
          drawScreen();
      } else if (btnOk.releasedEvent && !btnOk.longFired) {
          switch (settingsSelected) {
              case 0: state = ST_SETTINGS_BRIGHT; drawScreen(); break;
              case 1: state = ST_SETTINGS_LEFT;   drawScreen(); break;
              case 2:
                  filteredVoltage = 0.0f;
                  filteredPercent = 0.0f;
                  lastBatSample = 0;
                  state = ST_SETTINGS_BAT;
                  drawScreen();
                  break;
              case 3:
                  factoryConfirmSelected = false;
                  state = ST_SETTINGS_FACTORY;
                  drawScreen();
                  break;
          }
      }
      break;

    case ST_SETTINGS_BRIGHT:
      if (leftHanded) {
        if (btnUp.pressedEvent   || btnUp.repeatEvent)   changeBrightness(-BRIGHTNESS_STEP);
        if (btnDown.pressedEvent || btnDown.repeatEvent) changeBrightness(BRIGHTNESS_STEP);
      } else {
        if (btnUp.pressedEvent   || btnUp.repeatEvent)   changeBrightness(BRIGHTNESS_STEP);
        if (btnDown.pressedEvent || btnDown.repeatEvent) changeBrightness(-BRIGHTNESS_STEP);
      }
      if (btnOk.longEvent) {
          markSettingsDirty();
          state = ST_SETTINGS;
          drawScreen();
      }
      break;

    case ST_SETTINGS_LEFT:
      if (btnOk.releasedEvent && !btnOk.longFired) {
          leftHanded = !leftHanded;
          if (leftHanded) {
              oled.flipH(true);
              oled.flipV(true);
          } else {
              oled.flipH(false);
              oled.flipV(false);
          }
          markSettingsDirty();
          drawScreen();
      }
      if (btnOk.longEvent) {
          state = ST_SETTINGS;
          drawScreen();
      }
      break;

    case ST_SETTINGS_BAT:
      if (btnOk.longEvent) {
          state = ST_SETTINGS;
          drawScreen();
      }
      break;

    case ST_SETTINGS_FACTORY:
      if (btnUp.pressedEvent || btnDown.pressedEvent) {
        factoryConfirmSelected = !factoryConfirmSelected;
        drawScreen();
      }
      if (btnOk.longEvent) {
        state = ST_SETTINGS;
        drawScreen();
      } else if (btnOk.releasedEvent && !btnOk.longFired) {
        if (factoryConfirmSelected) {
          dnsServer.stop();
          WiFi.softAPdisconnect(true);
          WiFi.mode(WIFI_OFF);
          wifiActive = false;
          
          LittleFS.format();
          LittleFS.begin(true);

          if (!LittleFS.exists("/_cfg.txt")) {
            File f = LittleFS.open("/_cfg.txt", "w", true);
            if (f) { f.println("200"); f.println(""); f.println("0"); f.close(); }
          }

          dino_init(&oled);
          File bf = LittleFS.open("/dino_game_score.sys", "w");
          if (bf) { bf.print("0!"); bf.close(); }

          brightness = 200;
          leftHanded = false;
          oled.setContrast(brightness);
          oled.flipH(false);
          oled.flipV(false);
          
          refreshFileList();

          state = ST_MAIN;
          menuSelected = 0;
          drawScreen();
        } else {
          state = ST_SETTINGS;
          drawScreen();
        }
      }
      break;

    case ST_AI_CONFIG:
      if (btnOk.longEvent) {
          state = ST_LIST;
          selected = AI_ITEM_INDEX;
          drawScreen();
      }
      break;
  }
}

void moveSelection(int dir) {
  int total = 1 + (int)fileEntries.size();
  if (total == 0) return;
  selected = (selected + dir + total) % total;
  scrollPos = 0;
  lastScrollIdx = selected;
  drawScreen();
}

void openSelectedFile() {
  if (fileEntries.empty()) return;
  if (selected < 0 || selected >= (int)fileEntries.size()) return;
  buildPageIndex(fileEntries[selected].fname);
  page = 0;
  state = ST_VIEW;
  drawScreen();
}

void prevPage() { if (page > 0) { page--; drawScreen(); } }
void nextPage() { if (page < (int)pageOffsets.size() - 1) { page++; drawScreen(); } }

void changeBrightness(int delta) {
  int v = (int)brightness + delta;
  v = constrain(v, 5, 255);
  brightness = (uint8_t)v;
  oled.setContrast(brightness);
  markSettingsDirty();
  drawScreen();
}

void toggleWifi() {
  wifiActive = !wifiActive;

  if (wifiActive) {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASS);
    dnsServer.start(53, "*", apIP);
    server.begin();
    state = ST_WIFI;
  } else {
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    state = ST_MAIN;
  }
  drawScreen();
}

void drawScreen() {
  oled.clear();
  switch (state) {
    case ST_MAIN: drawMain(); break;
    case ST_LIST: drawList(); break;
    case ST_VIEW: drawView(); break;
    case ST_WIFI: drawWifiInfo(); break;
    case ST_SETTINGS: drawSettings(); break;
    case ST_SETTINGS_BRIGHT: drawSettingsBright(); break;
    case ST_SETTINGS_LEFT: drawSettingsLeft(); break;
    case ST_SETTINGS_BAT: drawSettingsBat(); break;
    case ST_SETTINGS_FACTORY: drawSettingsFactory(); break;
    case ST_AI_CONFIG: drawAiConfig(); break;
  }
  oled.update();
}

void drawMain() {
  for (int row = 0; row < MENU_COUNT && row < SCREEN_ROWS - 1; row++) {
    String prefix = (row == menuSelected) ? "> " : "  ";
    oled.setCursor(0, row);
    oled.print(prefix + String(menuNames[row]));
  }
  oled.setCursor(0, 7);
  oled.print("OK - выбрать");
}

void drawSettings() {
  for (int row = 0; row < SETTINGS_COUNT && row < SCREEN_ROWS - 1; row++) {
    String prefix = (row == settingsSelected) ? "> " : "  ";
    oled.setCursor(0, row);
    oled.print(prefix + String(settingsNames[row]));
  }
  oled.setCursor(0, 7);
  oled.print("OK - вход, удерж. - назад");
}

void drawSettingsBright() {
  oled.setCursor(0, 0);
  oled.print("Яркость");
  oled.setCursor(0, 3);
  oled.print("Значение: ");
  oled.print((int)brightness);
  oled.setCursor(0, 5);
  oled.print("UP/DOWN - изменить");
  oled.setCursor(0, 7);
  oled.print("Удерж. OK - назад");
}

void drawSettingsLeft() {
  oled.setCursor(0, 0);
  oled.print("Режим левши");
  oled.setCursor(0, 3);
  oled.print("Сейчас: ");
  oled.print(leftHanded ? "ВКЛ" : "ВЫКЛ");
  oled.setCursor(0, 5);
  oled.print("OK - переключить");
  oled.setCursor(0, 7);
  oled.print("Удерж. OK - назад");
}

void drawSettingsBat() {
  oled.setCursor(0, 0);
  oled.print(F("BATTERY MONITOR"));

  oled.setScale(2);
  oled.setCursor(0, 2);
  char buf[8];
  dtostrf(filteredVoltage, 4, 2, buf);
  oled.print(buf);
  oled.print(F(" V"));
  oled.setScale(1);

  oled.setCursor(0, 5);
  int percent = (int)(filteredPercent + 0.5f);
  if (percent > 100) percent = 100;
  if (percent < 0)   percent = 0;
  oled.print(F("Заряд: "));
  oled.print(percent);
  oled.print(F(" %"));

  drawBatteryBar(2, 56, 100, 7, percent);
}

void drawSettingsFactory() {
  oled.setCursor(0, 0);
  oled.print("Сброс до заводских");
  oled.setCursor(0, 2);
  oled.print("Вы точно хотите?");
  oled.setCursor(0, 3);
  oled.print("Это сотрёт ваши");
  oled.setCursor(0, 4);
  oled.print("важные файлы.");

  oled.setCursor(0, 6);
  oled.print(factoryConfirmSelected ? "  Нет    [Да]" : "[Нет]    Да ");
  
  oled.setCursor(0, 7);
  oled.print("UP/DOWN - выбор");
}

void drawAiConfig() {
  oled.setCursor(0, 0);
  oled.print("AI-настройки");

  String s = aiGetWifiSsid();
  oled.setCursor(0, 2);
  oled.print("SSID: "); oled.print(s.length() ? s : "(пусто)");

  String k = aiGetApiKey();
  oled.setCursor(0, 3);
  oled.print("Key:  ");
  if (k.length() > 6) {
    oled.print(k.substring(0, 3) + "..." + k.substring(k.length() - 3));
  } else {
    oled.print(k.length() ? k : "(пусто)");
  }

  String u = aiGetApiUrl();
  oled.setCursor(0, 4);
  oled.print("URL:  "); oled.print(u.length() ? u : "(пусто)");

  String m = aiGetApiModel();
  oled.setCursor(0, 5);
  oled.print("Model: "); oled.print(m.length() ? m : "(пусто)");

  oled.setCursor(0, 7);
  oled.print("Ред.: точка доступа");
}

void drawList() {
  const int VIS = SCREEN_ROWS;
  const int MARGIN = 1;
  const int WIDTH = CHARS_PER_LINE - 2;
  int total = 1 + (int)fileEntries.size();

  if (total == 1) {
    oled.setCursor(0, 0);
    oled.print(selected == 0 ? "> " : "  ");
    oled.print("Настройки AI");
    oled.setCursor(0, 3);
    oled.print("Нет файлов");
    return;
  }

  if (selected < listStart + MARGIN)
    listStart = max(0, selected - MARGIN);
  if (selected > listStart + VIS - 1 - MARGIN)
    listStart = selected - VIS + 1 + MARGIN;

  int maxStart = max(0, total - VIS);
  listStart = constrain(listStart, 0, maxStart);

  for (int row = 0; row < VIS && (listStart + row) < total; row++) {
    int idx = listStart + row;
    String prefix = (idx == selected) ? "> " : "  ";
    oled.setCursor(0, row);

    if (idx == AI_ITEM_INDEX) {
      oled.print(prefix + "Настройки AI");
    } else {
      int fi = idx - 1;
      String title = fileEntries[fi].title;
      int titleChars = utf8Length(title);
      String shown;

      if (titleChars <= WIDTH) {
        shown = title;
      } else if (idx == selected) {
        if (lastScrollIdx != selected) { scrollPos = 0; lastScrollIdx = selected; }
        shown = marqueeText(title, WIDTH, scrollPos);
      } else {
        shown = utf8Substring(title, 0, WIDTH);
      }
      oled.print(prefix + shown);
    }
  }
}

void drawView() {
  if (currentFileEmpty || currentFileName.length() == 0) {
    oled.setCursor(0, 3);
    oled.print("Пусто");
    return;
  }

  File f = LittleFS.open(pathOf(currentFileName), "r");
  if (!f) {
    oled.setCursor(0, 3);
    oled.print("Ошибка чтения");
    return;
  }

  uint32_t offset = (page >= 0 && page < (int)pageOffsets.size()) ? pageOffsets[page] : 0;
  f.seek(offset);

  lsReset();

  String line;
  uint32_t dummy;
  for (int row = 0; row < SCREEN_ROWS; row++) {
    if (!lsNextLine(f, line, dummy)) break;
    oled.setCursor(0, row);
    oled.print(line);
  }
  f.close();
}

void drawWifiInfo() {
  oled.setCursor(0, 0);
  oled.print("WiFi: " + String(AP_SSID));
  oled.setCursor(0, 2);
  oled.print("IP: " + WiFi.softAPIP().toString());
  oled.setCursor(0, 4);
  oled.print("Pass: " + String(AP_PASS));
  oled.setCursor(0, 6);
  oled.print("Удерж. OK - назад");
}

int utf8Length(const String &s) {
  int count = 0;
  size_t i = 0;
  while (i < s.length()) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) i += 1;
    else if ((c & 0xE0) == 0xC0) i += 2;
    else if ((c & 0xF0) == 0xE0) i += 3;
    else if ((c & 0xF8) == 0xF0) i += 4;
    else i += 1;
    count++;
  }
  return count;
}

String utf8Substring(const String &s, int startChar, int charCount) {
  int idxChar = 0;
  size_t i = 0;
  int byteStart = -1;
  size_t byteEnd = s.length();

  while (i < s.length()) {
    if (idxChar == startChar) byteStart = (int)i;
    if (idxChar == startChar + charCount) { byteEnd = i; break; }
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) i += 1;
    else if ((c & 0xE0) == 0xC0) i += 2;
    else if ((c & 0xF0) == 0xE0) i += 3;
    else if ((c & 0xF8) == 0xF0) i += 4;
    else i += 1;
    idxChar++;
  }
  if (idxChar == startChar && byteStart == -1) byteStart = (int)i;
  if (byteStart == -1) return "";
  return s.substring(byteStart, byteEnd);
}

String marqueeText(const String &title, int width, int offset) {
  const String sep = "    ";
  int period = utf8Length(title) + utf8Length(sep);
  if (period <= 0) return title;

  String loop = title + sep + title + sep;
  int pos = ((offset % period) + period) % period;
  return utf8Substring(loop, pos, width);
}

bool   lsHavePending = false;
String lsPendingWord = "";
bool   lsPendingBreak = false;

void lsReset() {
  lsHavePending = false;
  lsPendingWord = "";
  lsPendingBreak = false;
}

bool lsReadWord(File &f, String &word, bool &isBreak) {
  word = "";
  isBreak = false;
  int ch;
  while ((ch = f.read()) != -1) {
    if (ch == ' ' || ch == '\r') {
      if (word.length() == 0) continue;
      return true;
    }
    if (ch == '\n') {
      isBreak = true;
      return true;
    }
    word += (char)ch;
    if (utf8Length(word) >= CHARS_PER_LINE) return true;
  }
  return word.length() > 0;
}

bool lsNextLine(File &f, String &line, uint32_t &lineStartPos) {
  line = "";
  bool gotAny = false;
  bool startCaptured = false;

  while (true) {
    if (!lsHavePending) {
      uint32_t before = f.position();
      if (!lsReadWord(f, lsPendingWord, lsPendingBreak)) {
        return gotAny;
      }
      if (!startCaptured) { lineStartPos = before; startCaptured = true; }
      lsHavePending = true;
    } else if (!startCaptured) {
      lineStartPos = f.position() - lsPendingWord.length() - (lsPendingBreak ? 1 : 0);
      startCaptured = true;
    }

    if (lsPendingWord.length() == 0) {
      lsHavePending = false;
      return true;
    }

    int lineChars = utf8Length(line);
    int wordChars = utf8Length(lsPendingWord);
    int extra = (lineChars > 0 ? 1 : 0) + wordChars;

    if (lineChars + extra <= CHARS_PER_LINE) {
      if (line.length() > 0) line += ' ';
      line += lsPendingWord;
      gotAny = true;
      lsHavePending = false;
      if (lsPendingBreak) return true;
    } else {
      return true;
    }
  }
}

void buildPageIndex(const String &fname) {
  pageOffsets.clear();
  currentFileName = fname;
  currentFileEmpty = false;

  File f = LittleFS.open(pathOf(fname), "r");
  if (!f) { currentFileEmpty = true; pageOffsets.push_back(0); return; }

  f.readStringUntil('\n');
  lsReset();

  uint32_t startPos = f.position();
  pageOffsets.push_back(startPos);

  String line;
  uint32_t lineStart;
  int rowInPage = 0;
  bool any = false;

  while (lsNextLine(f, line, lineStart)) {
    any = true;
    if (rowInPage == SCREEN_ROWS) {
      pageOffsets.push_back(lineStart);
      rowInPage = 0;
    }
    rowInPage++;
  }
  f.close();

  currentFileEmpty = !any;
}

void markSettingsDirty() {
  settingsDirty = true;
  settingsChangedAt = millis();
}

void maybeSaveSettings() {
  if (settingsDirty && millis() - settingsChangedAt > SETTINGS_SAVE_DELAY) {
    saveSettings();
    settingsDirty = false;
  }
}

void loadSettings() {
  if (!LittleFS.exists(SETTINGS_PATH)) return;
  File f = LittleFS.open(SETTINGS_PATH, "r");
  if (!f) return;
  String line1 = f.readStringUntil('\n');
  String line2 = f.readStringUntil('\n');
  String line3 = f.readStringUntil('\n');
  f.close();

  line1.trim();
  line2.trim();
  line3.trim();

  if (line1.length() > 0) {
    int v = line1.toInt();
    if (v >= 5 && v <= 255) brightness = (uint8_t)v;
  }
  lastFileName = line2;
  if (line3.length() > 0) {
    leftHanded = (line3.toInt() != 0);
  }
}

void saveSettings() {
  String fnameToSave = "";
  if (!fileEntries.empty() && selected >= 0 && selected < (int)fileEntries.size()) {
    fnameToSave = fileEntries[selected].fname;
  }
  File f = LittleFS.open(SETTINGS_PATH, "w");
  if (!f) return;
  f.println(String(brightness));
  f.println(fnameToSave);
  f.println(leftHanded ? "1" : "0");
  f.close();
}

String pathOf(const String &fn) {
  return fn.startsWith("/") ? fn : "/" + fn;
}

unsigned long extractFileId(const String &fname) {
  int start = fname.indexOf("d_");
  if (start == -1) return 0;
  start += 2;
  int end = fname.indexOf(".txt", start);
  if (end == -1) end = fname.length();
  String digits = fname.substring(start, end);
  if (digits.length() == 0) return 0;
  for (size_t i = 0; i < digits.length(); i++) {
    if (!isDigit(digits[i])) return 0;
  }
  return strtoul(digits.c_str(), nullptr, 10);
}

void refreshFileList() {
  fileEntries.clear();
  File root = LittleFS.open("/");
  if (!root) return;
  File f = root.openNextFile();
  while (f) {
    String fname = String(f.name());
    if (!f.isDirectory() && fname.indexOf("_cfg.txt") == -1
        && fname.indexOf("dino_game_score") == -1
        && fname.indexOf("flappy_best") == -1
        && fname.indexOf("snake_best") == -1
        && fname.indexOf("ai_cfg") == -1) {
      String title = f.readStringUntil('\n');
      title.trim();
      fileEntries.push_back({fname, title});
    }
    f = root.openNextFile();
  }

  std::sort(fileEntries.begin(), fileEntries.end(), [](const FileEntry &a, const FileEntry &b) {
    return extractFileId(a.fname) > extractFileId(b.fname);
  });
}

FileData readFile(const String &fname) {
  FileData d;
  File f = LittleFS.open(pathOf(fname), "r");
  if (!f) return d;
  d.title = f.readStringUntil('\n');
  d.title.trim();
  d.content = f.readString();
  f.close();
  return d;
}

void writeFile(const String &fname, String title, const String &content) {
  title.replace("\n", " ");
  title.replace("\r", "");
  File f = LittleFS.open(pathOf(fname), "w");
  if (!f) return;
  f.println(title);
  f.print(content);
  f.close();
}

String genFilename() {
  unsigned long maxId = 0;
  File root = LittleFS.open("/");
  if (root) {
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        unsigned long id = extractFileId(String(f.name()));
        if (id > maxId) maxId = id;
      }
      f = root.openNextFile();
    }
  }
  unsigned long newId = maxId + 1;
  String fn = "/d_" + String(newId) + ".txt";
  while (LittleFS.exists(fn)) {
    newId++;
    fn = "/d_" + String(newId) + ".txt";
  }
  return fn;
}

bool hasSpace(long extraBytes) {
  long freeBytes = (long)LittleFS.totalBytes() - (long)LittleFS.usedBytes();
  const long MARGIN = 512;
  return (freeBytes - extraBytes) > MARGIN;
}

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  return s;
}

void redirectMsg(const char *m) {
  server.sendHeader("Location", String("/?msg=") + m, true);
  server.send(302, "text/plain", "");
}

void handleRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

String pageStyle() {
  return F(
    "*{box-sizing:border-box}"
    "body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#0e0e10;color:#eee;"
    "padding:16px;max-width:520px;margin:auto;line-height:1.4}"
    "h2{color:#4caf50;margin:4px 0 12px}"
    "h3{color:#8bc34a;margin:24px 0 8px;font-size:16px;text-transform:uppercase;letter-spacing:.04em}"
    "input,textarea{width:100%;margin:6px 0;padding:10px;border-radius:8px;"
    "border:1px solid #333;background:#1b1b1f;color:#eee;font-size:16px}"
    "input:focus,textarea:focus{outline:none;border-color:#4caf50}"
    "textarea{min-height:150px;resize:vertical;font-family:inherit}"
    "button{width:100%;padding:12px;border:none;border-radius:8px;background:#4caf50;color:#fff;"
    "font-size:16px;font-weight:600;margin:6px 0;cursor:pointer}"
    "button:active{opacity:.8}"
    "button.del{background:#c0392b}"
    "button.edit{background:#2980b9}"
    "button.ai{background:#8e44ad}"
    ".row{display:flex;gap:8px}"
    ".row form{flex:1}"
    ".item{background:#1b1b1f;border-radius:10px;padding:12px;margin:10px 0}"
    ".item .title{font-weight:600;margin-bottom:8px;word-break:break-word}"
    ".bar{background:#2a2a2e;border-radius:8px;overflow:hidden;height:14px;margin:8px 0}"
    ".fill{background:#4caf50;height:100%}"
    ".hint{color:#888;font-size:13px;margin:4px 0 0}"
    ".msg-ok{color:#4caf50}.msg-err{color:#e74c3c}"
    ".ai-box{background:#1b1b1f;border:1px solid #8e44ad;border-radius:10px;padding:12px;margin:14px 0}"
    "a{color:#4caf50;text-decoration:none}"
  );
}

void handleRoot() {
  refreshFileList();
  size_t total = LittleFS.totalBytes();
  size_t used  = LittleFS.usedBytes();
  int pct = total ? (int)(used * 100 / total) : 0;

  String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Электронная Шпаргалка</title><style>");
  html += pageStyle();
  html += F("</style></head><body>");

  html += "<h2>Электронная Шпаргалка</h2>";
  html += "<div>Занято: " + String(used) + " / " + String(total) + " байт (" + String(pct) + "%)</div>";
  html += "<div class='bar'><div class='fill' style='width:" + String(pct) + "%'></div></div>";

  if (server.hasArg("msg")) {
    String msg = server.arg("msg");
    if (msg == "nospace") html += "<p class='msg-err'>Не хватает места!</p>";
    if (msg == "empty")   html += "<p class='msg-err'>Введите заголовок/Файл слишком большой</p>";
    if (msg == "ok")      html += "<p class='msg-ok'>Сохранено</p>";
  }

  html += "<div class='ai-box'><h3>Настройки нейросети</h3>";
  html += "<form method='POST' action='/ai_save'>";
  html += "<input type='text' name='ssid'  placeholder='WiFi SSID'    value='" + htmlEscape(aiGetWifiSsid()) + "'>";
  html += "<input type='text' name='pass'  placeholder='WiFi пароль'   value='" + htmlEscape(aiGetWifiPass()) + "'>";
  html += "<input type='text' name='key'   placeholder='API-ключ'      value='" + htmlEscape(aiGetApiKey()) + "'>";
  html += "<input type='text' name='url'   placeholder='API URL'       value='" + htmlEscape(aiGetApiUrl()) + "'>";
  html += "<input type='text' name='model' placeholder='Модель'        value='" + htmlEscape(aiGetApiModel()) + "'>";
  html += "<button class='ai' type='submit'>Сохранить настройки AI</button>";
  html += "</form></div>";

  html += F("<h3>Добавить файл</h3>"
    "<form method='POST' action='/upload'>"
    "<input type='text' name='title' placeholder='Заголовок' required>"
    "<textarea name='content' placeholder='Содержимое'></textarea>"
    "<button type='submit'>Загрузить</button>"
    "</form></body></html>");

  html += "<h3>Файлы</h3>";
  if (fileEntries.empty()) {
    html += "<p class='hint'>Нет файлов</p>";
  } else {
    for (auto &e : fileEntries) {
      html += "<div class='item'><div class='title'>" + htmlEscape(e.title) + "</div>"
              "<div class='row'>"
              "<form method='GET' action='/edit'>"
              "<input type='hidden' name='f' value='" + e.fname + "'>"
              "<button class='edit' type='submit'>Открыть/Изменить</button></form>"
              "<form method='POST' action='/delete' onsubmit=\"return confirm('Удалить файл?')\">"
              "<input type='hidden' name='f' value='" + e.fname + "'>"
              "<button class='del' type='submit'>Удалить</button></form>"
              "</div></div>";
    }
  }

  server.send(200, "text/html; charset=utf-8", html);
}

void handleAiSave() {
  String ssid  = server.arg("ssid");
  String pass  = server.arg("pass");
  String key   = server.arg("key");
  String url   = server.arg("url");
  String model = server.arg("model");

  ssid.trim(); pass.trim(); key.trim(); url.trim(); model.trim();

  if (url.length() == 0)   url   = "https://api.proxyapi.ru/v1/chat/completions";
  if (model.length() == 0) model = "gpt-4o-mini";

  aiSetAll(ssid, pass, key, url, model);
  redirectMsg("ok");
}

void handleUpload() {
  String title = server.arg("title");
  String content = server.arg("content");
  title.trim();
  if (title.length() == 0) { redirectMsg("empty"); return; }

  long extra = title.length() + content.length() + 8;
  if (!hasSpace(extra)) { redirectMsg("nospace"); return; }

  String fn = genFilename();
  writeFile(fn, title, content);
  redirectMsg("ok");
}

void handleEditPage() {
  if (!server.hasArg("f")) { handleRedirect(); return; }
  String fn = server.arg("f");
  if (!LittleFS.exists(pathOf(fn))) { handleRedirect(); return; }

  FileData d = readFile(fn);

  String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Изменить файл</title><style>");
  html += pageStyle();
  html += F("</style></head><body>");
  html += "<h2>Изменить файл</h2>";
  html += "<form method='POST' action='/save'>"
          "<input type='hidden' name='f' value='" + fn + "'>"
          "<input type='text' name='title' value='" + htmlEscape(d.title) + "' required>"
          "<textarea name='content'>" + htmlEscape(d.content) + "</textarea>"
          "<button type='submit'>Сохранить</button>"
          "</form>"
          "<form method='POST' action='/delete' onsubmit=\"return confirm('Удалить файл?')\">"
          "<input type='hidden' name='f' value='" + fn + "'>"
          "<button class='del' type='submit'>Удалить</button></form>"
          "<p><a href='/'>&larr; Назад к списку</a></p></body></html>";

  server.send(200, "text/html; charset=utf-8", html);
}

void handleSave() {
  String fn = server.arg("f");
  String title = server.arg("title");
  String content = server.arg("content");
  title.trim();

  if (title.length() == 0 || !LittleFS.exists(pathOf(fn))) { redirectMsg("empty"); return; }

  size_t oldSize = 0;
  {
    File f = LittleFS.open(pathOf(fn), "r");
    if (f) { oldSize = f.size(); f.close(); }
  }
  long newSize = title.length() + content.length() + 2;
  long delta = newSize - (long)oldSize;
  if (delta > 0 && !hasSpace(delta)) { redirectMsg("nospace"); return; }

  writeFile(fn, title, content);
  redirectMsg("ok");
}

void handleDelete() {
  if (server.hasArg("f")) {
    LittleFS.remove(pathOf(server.arg("f")));
  }
  redirectMsg("ok");
}
void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/ai_save", HTTP_POST, handleAiSave);
  server.on("/upload", HTTP_POST, handleUpload);
  server.on("/edit", HTTP_GET, handleEditPage);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/delete", HTTP_POST, handleDelete);

  server.on("/generate_204", HTTP_GET, handleRedirect);
  server.on("/gen_204", HTTP_GET, handleRedirect);
  server.on("/hotspot-detect.html", HTTP_GET, handleRedirect);
  server.on("/library/test/success.html", HTTP_GET, handleRedirect);
  server.on("/ncsi.txt", HTTP_GET, handleRedirect);
  server.on("/connecttest.txt", HTTP_GET, handleRedirect);
  server.on("/fwlink", HTTP_GET, handleRedirect);

  server.onNotFound(handleRedirect);
}
void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/ai_save", HTTP_POST, handleAiSave);
  server.on("/upload", HTTP_POST, handleUpload);
  server.on("/edit", HTTP_GET, handleEditPage);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/delete", HTTP_POST, handleDelete);

  server.on("/generate_204", HTTP_GET, handleRedirect);
  server.on("/gen_204", HTTP_GET, handleRedirect);
  server.on("/hotspot-detect.html", HTTP_GET, handleRedirect);
  server.on("/library/test/success.html", HTTP_GET, handleRedirect);
  server.on("/ncsi.txt", HTTP_GET, handleRedirect);
  server.on("/connecttest.txt", HTTP_GET, handleRedirect);
  server.on("/fwlink", HTTP_GET, handleRedirect);

  server.onNotFound(handleRedirect);
}