#include "AI.hpp"
#include "Arduino.h"
#include "display.hpp"
#include "main.hpp"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>

#define PIN_UP    7
#define PIN_DOWN  8
#define PIN_OK    10

static const char* AI_CFG_PATH = "/ai_cfg.txt";

static GyverOLED<SSD1306_128x64, OLED_BUFFER>* oled;

static String textBuffer = "";
static String textToSend = "";
static int mode = 0;

struct Message {
  String role;
  String content;
};
static std::vector<Message> chatHistory;
static const int MAX_HISTORY_PAIRS = 5;

static const int SCREEN_ROWS = 8;
static const int CHARS_PER_LINE = 21;
static std::vector<String> responseLines;
static int page = 0;

static const int KB_ROWS = 5;
static const int KB_COLS = 8;
static const int KB_TOP  = 2;
static const int CELL_W  = 16;

enum KeyAction { KA_CHAR, KA_SPACE, KA_SWITCH_LANG, KA_SWITCH_SYM, KA_BACKSPACE, KA_SEND };

struct Key {
  const char* label;
  const char* value;
  KeyAction action;
};

static const Key layoutRuUpper[4][KB_COLS] = {
  {{"А","А",KA_CHAR},{"Б","Б",KA_CHAR},{"В","В",KA_CHAR},{"Г","Г",KA_CHAR},{"Д","Д",KA_CHAR},{"Е","Е",KA_CHAR},{"Ж","Ж",KA_CHAR},{"З","З",KA_CHAR}},
  {{"И","И",KA_CHAR},{"Й","Й",KA_CHAR},{"К","К",KA_CHAR},{"Л","Л",KA_CHAR},{"М","М",KA_CHAR},{"Н","Н",KA_CHAR},{"О","О",KA_CHAR},{"П","П",KA_CHAR}},
  {{"Р","Р",KA_CHAR},{"С","С",KA_CHAR},{"Т","Т",KA_CHAR},{"У","У",KA_CHAR},{"Ф","Ф",KA_CHAR},{"Х","Х",KA_CHAR},{"Ц","Ц",KA_CHAR},{"Ч","Ч",KA_CHAR}},
  {{"Ш","Ш",KA_CHAR},{"Щ","Щ",KA_CHAR},{"Ъ","Ъ",KA_CHAR},{"Ы","Ы",KA_CHAR},{"Ь","Ь",KA_CHAR},{"Э","Э",KA_CHAR},{"Ю","Ю",KA_CHAR},{"Я","Я",KA_CHAR}}
};

static const Key layoutRuLower[4][KB_COLS] = {
  {{"а","а",KA_CHAR},{"б","б",KA_CHAR},{"в","в",KA_CHAR},{"г","г",KA_CHAR},{"д","д",KA_CHAR},{"е","е",KA_CHAR},{"ж","ж",KA_CHAR},{"з","з",KA_CHAR}},
  {{"и","и",KA_CHAR},{"й","й",KA_CHAR},{"к","к",KA_CHAR},{"л","л",KA_CHAR},{"м","м",KA_CHAR},{"н","н",KA_CHAR},{"о","о",KA_CHAR},{"п","п",KA_CHAR}},
  {{"р","р",KA_CHAR},{"с","с",KA_CHAR},{"т","т",KA_CHAR},{"у","у",KA_CHAR},{"ф","ф",KA_CHAR},{"х","х",KA_CHAR},{"ц","ц",KA_CHAR},{"ч","ч",KA_CHAR}},
  {{"ш","ш",KA_CHAR},{"щ","щ",KA_CHAR},{"ъ","ъ",KA_CHAR},{"ы","ы",KA_CHAR},{"ь","ь",KA_CHAR},{"э","э",KA_CHAR},{"ю","ю",KA_CHAR},{"я","я",KA_CHAR}}
};

static const Key layoutEnUpper[4][KB_COLS] = {
  {{"A","A",KA_CHAR},{"B","B",KA_CHAR},{"C","C",KA_CHAR},{"D","D",KA_CHAR},{"E","E",KA_CHAR},{"F","F",KA_CHAR},{"G","G",KA_CHAR},{"H","H",KA_CHAR}},
  {{"I","I",KA_CHAR},{"J","J",KA_CHAR},{"K","K",KA_CHAR},{"L","L",KA_CHAR},{"M","M",KA_CHAR},{"N","N",KA_CHAR},{"O","O",KA_CHAR},{"P","P",KA_CHAR}},
  {{"Q","Q",KA_CHAR},{"R","R",KA_CHAR},{"S","S",KA_CHAR},{"T","T",KA_CHAR},{"U","U",KA_CHAR},{"V","V",KA_CHAR},{"W","W",KA_CHAR},{"X","X",KA_CHAR}},
  {{"Y","Y",KA_CHAR},{"Z","Z",KA_CHAR},{"-","-",KA_CHAR},{"+","+",KA_CHAR},{"=","=",KA_CHAR},{"_","_",KA_CHAR},{"!","!",KA_CHAR},{"?","?",KA_CHAR}}
};

static const Key layoutEnLower[4][KB_COLS] = {
  {{"a","a",KA_CHAR},{"b","b",KA_CHAR},{"c","c",KA_CHAR},{"d","d",KA_CHAR},{"e","e",KA_CHAR},{"f","f",KA_CHAR},{"g","g",KA_CHAR},{"h","h",KA_CHAR}},
  {{"i","i",KA_CHAR},{"j","j",KA_CHAR},{"k","k",KA_CHAR},{"l","l",KA_CHAR},{"m","m",KA_CHAR},{"n","n",KA_CHAR},{"o","o",KA_CHAR},{"p","p",KA_CHAR}},
  {{"q","q",KA_CHAR},{"r","r",KA_CHAR},{"s","s",KA_CHAR},{"t","t",KA_CHAR},{"u","u",KA_CHAR},{"v","v",KA_CHAR},{"w","w",KA_CHAR},{"x","x",KA_CHAR}},
  {{"y","y",KA_CHAR},{"z","z",KA_CHAR},{",",",",KA_CHAR},{".",".",KA_CHAR},{":",":",KA_CHAR},{";",";",KA_CHAR},{"\"","\"",KA_CHAR},{"'","'",KA_CHAR}}
};

static const Key layoutSymNum1[4][KB_COLS] = {
  {{"0","0",KA_CHAR},{"1","1",KA_CHAR},{"2","2",KA_CHAR},{"3","3",KA_CHAR},{"4","4",KA_CHAR},{"5","5",KA_CHAR},{"6","6",KA_CHAR},{"7","7",KA_CHAR}},
  {{"8","8",KA_CHAR},{"9","9",KA_CHAR},{"+","+",KA_CHAR},{"-","-",KA_CHAR},{"*","*",KA_CHAR},{"/","/",KA_CHAR},{"%","%",KA_CHAR},{"@","@",KA_CHAR}},
  {{"#","#",KA_CHAR},{"$","$",KA_CHAR},{"&","&",KA_CHAR},{"[","[",KA_CHAR},{"]","]",KA_CHAR},{"{","{",KA_CHAR},{"}","}",KA_CHAR},{"<","<",KA_CHAR}},
  {{">",">",KA_CHAR},{"(","(",KA_CHAR},{")",")",KA_CHAR},{"|","|",KA_CHAR},{"~","~",KA_CHAR},{"^","^",KA_CHAR},{"`","`",KA_CHAR},{"\\","\\",KA_CHAR}}
};

static const Key layoutSymNum2[4][KB_COLS] = {
  {{".",".",KA_CHAR},{",",",",KA_CHAR},{";",";",KA_CHAR},{":",":",KA_CHAR},{"!","!",KA_CHAR},{"?","?",KA_CHAR},{"'","'",KA_CHAR},{"\"","\"",KA_CHAR}},
  {{"=","=",KA_CHAR},{"_","_",KA_CHAR},{"-","-",KA_CHAR},{"+","+",KA_CHAR},{"*","*",KA_CHAR},{"/","/",KA_CHAR},{"\\","\\",KA_CHAR},{"|","|",KA_CHAR}},
  {{"@","@",KA_CHAR},{"#","#",KA_CHAR},{"$","$",KA_CHAR},{"%","%",KA_CHAR},{"&","&",KA_CHAR},{"(","(",KA_CHAR},{")",")",KA_CHAR},{"[","[",KA_CHAR}},
  {{"]","]",KA_CHAR},{"{","{",KA_CHAR},{"}","}",KA_CHAR},{"<","<",KA_CHAR},{">",">",KA_CHAR},{"`","`",KA_CHAR},{"~","~",KA_CHAR},{"^","^",KA_CHAR}}
};

static int currentLang = 0;
static int currentSym  = 0;
static int kbRow = 0;
static int kbCol = 0;

static Key controlRow[KB_COLS];

static void buildControlRow() {
  const char* langLabel = (currentLang >= 2) ? "Н" : "W";
  const char* symLabel = (currentSym == 0) ? "*" : "7";

  controlRow[0] = {langLabel, "", KA_SWITCH_LANG};
  controlRow[1] = {"_", " ", KA_SPACE};
  controlRow[2] = {"<", "", KA_BACKSPACE};
  controlRow[3] = {">", "", KA_SEND};
  controlRow[4] = {symLabel, "", KA_SWITCH_SYM};
  controlRow[5] = {"", "", KA_CHAR};
  controlRow[6] = {"", "", KA_CHAR};
  controlRow[7] = {"", "", KA_CHAR};
}

struct AiBtn {
  uint8_t pin;
  bool stable;
  unsigned long tPress;
  unsigned long tRepeat;
  bool longFired;
  bool pressedEvent;
  bool releasedEvent;
  bool longEvent;
  bool repeatEvent;
  int counter;
};

static AiBtn aiBtnUp   { PIN_UP,   false, 0, 0, false, false, false, false, false, 0 };
static AiBtn aiBtnDown { PIN_DOWN, false, 0, 0, false, false, false, false, false, 0 };
static AiBtn aiBtnOk   { PIN_OK,   false, 0, 0, false, false, false, false, false, 0 };

static const int DEBOUNCE_MAX = 6;
static const unsigned long LONGPRESS_MS = 600;
static const unsigned long UP_REPEAT_START_MS = 300;
static const unsigned long DOWN_REPEAT_START_MS = 300;
static const unsigned long REPEAT_INTERVAL_MS = 150;
static const unsigned long CLEAR_HISTORY_MS = 2000;
static const unsigned long EXIT_MS = 1000;

static void   removeLastUtf8Char(String &s);
static int    lastUtf8CharLen(const String &s);
static String askChatGPT(const String &userMessage);
static void   drawReadMode();
static void   drawInputMode();
static void   drawStatus(const String &msg);
static void   buildResponseLines();
static int    utf8Length(const String &s);
static String utf8Substring(const String &s, int startChar, int charCount);
static void   updateAiBtn(AiBtn &b);

static void aiEnsureCfgFile() {
  if (LittleFS.exists(AI_CFG_PATH)) return;
  File f = LittleFS.open(AI_CFG_PATH, "w", true);
  if (!f) return;
  f.println("");
  f.println("");
  f.println("");
  f.println("https://api.proxyapi.ru/v1/chat/completions");
  f.println("gpt-4o-mini");
  f.close();
}

String aiGetWifiSsid()  { aiEnsureCfgFile(); File f = LittleFS.open(AI_CFG_PATH, "r"); if (!f) return ""; String s = f.readStringUntil('\n'); f.close(); s.trim(); return s; }
String aiGetWifiPass()  { aiEnsureCfgFile(); File f = LittleFS.open(AI_CFG_PATH, "r"); if (!f) return ""; f.readStringUntil('\n'); String s = f.readStringUntil('\n'); f.close(); s.trim(); return s; }
String aiGetApiKey()    { aiEnsureCfgFile(); File f = LittleFS.open(AI_CFG_PATH, "r"); if (!f) return ""; f.readStringUntil('\n'); f.readStringUntil('\n'); String s = f.readStringUntil('\n'); f.close(); s.trim(); return s; }
String aiGetApiUrl()    { aiEnsureCfgFile(); File f = LittleFS.open(AI_CFG_PATH, "r"); if (!f) return "https://api.proxyapi.ru/v1/chat/completions"; f.readStringUntil('\n'); f.readStringUntil('\n'); f.readStringUntil('\n'); String s = f.readStringUntil('\n'); f.close(); s.trim(); if (s.length()==0) s = "https://api.proxyapi.ru/v1/chat/completions"; return s; }
String aiGetApiModel()  { aiEnsureCfgFile(); File f = LittleFS.open(AI_CFG_PATH, "r"); if (!f) return "gpt-4o-mini"; f.readStringUntil('\n'); f.readStringUntil('\n'); f.readStringUntil('\n'); f.readStringUntil('\n'); String s = f.readStringUntil('\n'); f.close(); s.trim(); if (s.length()==0) s = "gpt-4o-mini"; return s; }

void aiSetAll(const String& ssid, const String& pass,
              const String& key,  const String& url,
              const String& model) {
  File f = LittleFS.open(AI_CFG_PATH, "w");
  if (!f) return;
  f.println(ssid);
  f.println(pass);
  f.println(key);
  f.println(url);
  f.println(model);
  f.close();
}

bool aiIsConfigured() {
  return aiGetWifiSsid().length() > 0 && aiGetApiKey().length() > 0;
}

static int utf8Length(const String &s) {
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

static String utf8Substring(const String &s, int startChar, int charCount) {
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

static int lastUtf8CharLen(const String &s) {
  if (s.length() == 0) return 0;
  int i = s.length() - 1;
  while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) i--;
  return s.length() - i;
}

static void removeLastUtf8Char(String &s) {
  int n = lastUtf8CharLen(s);
  if (n <= 0 || n > (int)s.length()) return;
  s.remove(s.length() - n);
}

static void buildResponseLines() {
  responseLines.clear();
  page = 0;

  String text = textBuffer;
  int n = text.length();
  int i = 0;
  String currentLine = "";

  while (i < n) {
    String word = "";
    while (i < n) {
      char c = text[i];
      if (c == ' ' || c == '\n' || c == '\r') break;
      uint8_t b = (uint8_t)c;
      int len = 1;
      if ((b & 0xE0) == 0xC0) len = 2;
      else if ((b & 0xF0) == 0xE0) len = 3;
      else if ((b & 0xF8) == 0xF0) len = 4;
      for (int k = 0; k < len && i + k < n; k++) word += text[i + k];
      i += len;
    }

    bool explicitNewline = false;
    if (i < n && text[i] == '\n') { explicitNewline = true; i++; }
    else if (i < n) { i++; }

    int curLen = utf8Length(currentLine);
    int wordLen = utf8Length(word);

    if (curLen == 0) {
      if (wordLen > CHARS_PER_LINE) {
        int pos = 0;
        while (pos < wordLen) {
          responseLines.push_back(utf8Substring(word, pos, CHARS_PER_LINE));
          pos += CHARS_PER_LINE;
        }
      } else {
        currentLine = word;
      }
    } else if (curLen + 1 + wordLen <= CHARS_PER_LINE) {
      currentLine += " " + word;
    } else {
      responseLines.push_back(currentLine);
      currentLine = word;
    }

    if (explicitNewline) {
      if (currentLine.length() > 0) responseLines.push_back(currentLine);
      else responseLines.push_back("");
      currentLine = "";
    }
  }

  if (currentLine.length() > 0) responseLines.push_back(currentLine);
}

static Key getKey(int r, int c) {
  if (r == 4) {
    return controlRow[c];
  }
  
  if (currentSym == 0) {
    if (currentLang == 0) return layoutRuUpper[r][c];
    if (currentLang == 1) return layoutRuLower[r][c];
    if (currentLang == 2) return layoutEnUpper[r][c];
    if (currentLang == 3) return layoutEnLower[r][c];
  } else {
    if (currentLang == 0) return layoutSymNum1[r][c];
    if (currentLang == 1) return layoutSymNum1[r][c];
    if (currentLang == 2) return layoutSymNum2[r][c];
    if (currentLang == 3) return layoutSymNum2[r][c];
  }
  return layoutRuUpper[r][c];
}

static String askChatGPT(const String &userMessage) {
  if (!aiIsConfigured()) {
    return "Настройте WiFi и API-ключ через точку доступа (http://192.168.4.1)";
  }

  String WIFI_SSID     = aiGetWifiSsid();
  String WIFI_PASSWORD = aiGetWifiPass();
  String API_KEY       = aiGetApiKey();
  String API_URL       = aiGetApiUrl();
  String API_MODEL     = aiGetApiModel();

  String result;

  drawStatus("Wi-Fi подключение...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWi-Fi FAIL");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return "Wi-Fi не подключен (проверьте SSID/пароль)";
  }

  Serial.println("\nWi-Fi OK, IP: " + WiFi.localIP().toString());

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  if (!https.begin(client, API_URL.c_str())) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return "Ошибка HTTPS begin (проверьте URL)";
  }

  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + API_KEY);
  https.setTimeout(30000);

  JsonDocument doc;
  doc["model"] = API_MODEL;
  JsonArray messages = doc["messages"].to<JsonArray>();

  JsonObject sysMsg = messages.add<JsonObject>();
  sysMsg["role"] = "system";
  sysMsg["content"] = "Ты краткий и полезный ассистент. Отвечай кратко, без лишних деталей, так как у пользователя маленький экран.";

  for (const auto &m : chatHistory) {
    JsonObject histMsg = messages.add<JsonObject>();
    histMsg["role"] = m.role;
    histMsg["content"] = m.content;
  }

  JsonObject newMsg = messages.add<JsonObject>();
  newMsg["role"] = "user";
  newMsg["content"] = userMessage;

  String body;
  serializeJson(doc, body);
  Serial.println("Отправка (с историей): " + body);

  int code = https.POST(body);
  if (code != 200) {
    result = "HTTP " + String(code);
    String resp = https.getString();
    Serial.println("Ответ сервера: " + resp);
    https.end();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return result;
  }

  String response = https.getString();
  https.end();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("Wi-Fi OFF (экономия)");

  JsonDocument resp;
  DeserializationError err = deserializeJson(resp, response);
  if (err) return "JSON error: " + String(err.c_str());

  if (!resp["choices"].is<JsonArray>() || resp["choices"].size() == 0) {
    return "Пустой ответ";
  }

  result = resp["choices"][0]["message"]["content"].as<String>();
  if (result.length() == 0) result = "Пустой ответ";

  chatHistory.push_back({"user", userMessage});
  chatHistory.push_back({"assistant", result});

  while (chatHistory.size() > (size_t)(MAX_HISTORY_PAIRS * 2)) {
    chatHistory.erase(chatHistory.begin());
    chatHistory.erase(chatHistory.begin());
  }

  return result;
}

static void drawStatus(const String &msg) {
  oled->clear();
  oled->setCursor(0, 0);
  oled->print(F("Статус:"));
  oled->setCursor(0, 2);
  oled->print(msg);
  oled->update();
}

static void drawReadMode() {
  oled->clear();

  if (responseLines.empty()) {
    oled->setCursor(0, 0);
    oled->print(F("Нет ответа"));
    oled->setCursor(0, 7);
    oled->print(F("OK - ввод"));
    oled->update();
    return;
  }

  int totalPages = (responseLines.size() + SCREEN_ROWS - 1) / SCREEN_ROWS;
  if (page < 0) page = 0;
  if (page >= totalPages) page = totalPages - 1;

  int startLine = page * SCREEN_ROWS;
  for (int row = 0; row < SCREEN_ROWS; row++) {
    int idx = startLine + row;
    if (idx >= (int)responseLines.size()) break;
    oled->setCursor(0, row);
    oled->print(responseLines[idx]);
  }

  if (page < totalPages - 1) {
    oled->setCursor(20, 7);
    oled->print(F("v"));
  }

  oled->update();
}

static void drawInputMode() {
  oled->clear();

  oled->setCursor(0, 0);
  oled->print(F("Набор:"));

  const int MAX_SHOWN = 21;
  String shown = textToSend;
  if ((int)shown.length() > MAX_SHOWN) {
    shown = shown.substring(shown.length() - MAX_SHOWN);
  }

  oled->setCursor(0, 1);
  oled->print(shown);
  oled->print(F("_"));

  for (int r = 0; r < KB_ROWS; r++) {
    int y = KB_TOP + r;
    for (int c = 0; c < KB_COLS; c++) {
      int x = c * CELL_W;
      Key k = getKey(r, c);
      oled->setCursor(x, y);
      oled->print(k.label);

      if (r == kbRow && c == kbCol) {
        oled->setCursor(x + CELL_W - 6, y);
        oled->print(F("<"));
      }
    }
  }

  oled->update();
}

static void updateAiBtn(AiBtn &b) {
  b.pressedEvent = b.releasedEvent = b.longEvent = b.repeatEvent = false;
  bool pressedNow = (digitalRead(b.pin) == LOW);

  if (pressedNow) { if (b.counter < DEBOUNCE_MAX) b.counter++; }
  else            { if (b.counter > 0)           b.counter--; }

  bool newStable = b.stable;
  if (b.counter >= DEBOUNCE_MAX) newStable = true;
  else if (b.counter <= 0)       newStable = false;

  if (newStable != b.stable) {
    b.stable = newStable;
    if (b.stable) {
      b.tPress = millis();
      b.tRepeat = millis();
      b.longFired = false;
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

    unsigned long repeatStart = 0;
    if (b.pin == PIN_UP) repeatStart = UP_REPEAT_START_MS;
    else if (b.pin == PIN_DOWN) repeatStart = DOWN_REPEAT_START_MS;

    if (repeatStart > 0 && held >= repeatStart) {
      if (millis() - b.tRepeat >= REPEAT_INTERVAL_MS) {
        b.tRepeat = millis();
        b.repeatEvent = true;
      }
    }
  }
}

void aiInit(GyverOLED<SSD1306_128x64, OLED_BUFFER>* _oled) {
  oled = _oled;
  chatHistory.clear();
  textBuffer = "Нажми OK для набора";
  textToSend = "";
  mode = 0;
  page = 0;
  currentLang = 0;
  currentSym = 0;
  kbRow = 0;
  kbCol = 0;

  aiBtnUp   = { PIN_UP,   false, 0, 0, false, false, false, false, false, 0 };
  aiBtnDown = { PIN_DOWN, false, 0, 0, false, false, false, false, false, 0 };
  aiBtnOk   = { PIN_OK,   false, 0, 0, false, false, false, false, false, 0 };

  pinMode(PIN_UP,   INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_OK,   INPUT_PULLUP);

  aiEnsureCfgFile();
  buildControlRow();

  buildResponseLines();
  drawReadMode();
}

void aiGame(void) {
  while (1) {
    updateAiBtn(aiBtnUp);
    updateAiBtn(aiBtnDown);
    updateAiBtn(aiBtnOk);

    if (mode == 0 && aiBtnOk.stable && (millis() - aiBtnOk.tPress >= CLEAR_HISTORY_MS)) {
      if (chatHistory.size() > 0) {
        chatHistory.clear();
        textBuffer = "Контекст очищен";
        buildResponseLines();
        drawReadMode();
      }
      while (digitalRead(PIN_OK) == LOW) {
        delay(10);
        yield();
      }
      aiBtnOk.stable = false;
      aiBtnOk.counter = 0;
      aiBtnOk.longFired = true;
      continue;
    }

    if (aiBtnOk.stable && (millis() - aiBtnOk.tPress >= EXIT_MS) && !aiBtnOk.longFired) {
      return;
    }

    if (mode == 0) {
      if (aiBtnUp.releasedEvent && !aiBtnUp.longFired) {
        if (page > 0) { page--; drawReadMode(); }
      }
      if (aiBtnDown.releasedEvent && !aiBtnDown.longFired) {
        int totalPages = (responseLines.size() + SCREEN_ROWS - 1) / SCREEN_ROWS;
        if (page < totalPages - 1) { page++; drawReadMode(); }
      }

      if (aiBtnOk.releasedEvent && !aiBtnOk.longFired) {
        mode = 1;
        textToSend = "";
        currentLang = 0;
        currentSym = 0;
        kbRow = 0;
        kbCol = 0;
        buildControlRow();
        drawInputMode();
      }
    }
    else {
      if (aiBtnUp.releasedEvent && !aiBtnUp.longFired) {
        kbRow = (kbRow - 1 + KB_ROWS) % KB_ROWS;
        drawInputMode();
      }
      else if (aiBtnUp.repeatEvent) {
        kbRow = (kbRow - 1 + KB_ROWS) % KB_ROWS;
        drawInputMode();
      }

      if (aiBtnDown.releasedEvent && !aiBtnDown.longFired) {
        kbCol = (kbCol + 1) % KB_COLS;
        drawInputMode();
      }
      else if (aiBtnDown.repeatEvent) {
        kbCol = (kbCol + 1) % KB_COLS;
        drawInputMode();
      }

      if (aiBtnOk.releasedEvent && !aiBtnOk.longFired) {
        Key k = getKey(kbRow, kbCol);

        switch (k.action) {
          case KA_CHAR:
            if (k.value[0] != '\0') textToSend += k.value;
            break;
          case KA_SPACE:
            textToSend += " ";
            break;
          case KA_SWITCH_LANG:
            if (currentLang == 0) currentLang = 1;
            else if (currentLang == 1) currentLang = 2;
            else if (currentLang == 2) currentLang = 3;
            else currentLang = 0;
            
            buildControlRow();
            drawInputMode();
            break;
          case KA_SWITCH_SYM:
            currentSym = 1 - currentSym;
            buildControlRow();
            drawInputMode();
            break;
          case KA_BACKSPACE:
            removeLastUtf8Char(textToSend);
            break;
          case KA_SEND:
            if (textToSend.length() > 0) {
              drawStatus("Отправка...");
              String answer = askChatGPT(textToSend);
              textBuffer = answer;
              buildResponseLines();
              mode = 0;
              drawReadMode();

              while (digitalRead(PIN_OK) == LOW) {
                delay(5);
                yield();
              }
              aiBtnOk.stable        = false;
              aiBtnOk.releasedEvent = false;
              aiBtnOk.pressedEvent  = false;
              aiBtnOk.longFired     = false;
              aiBtnOk.counter       = 0;

              continue;
            }
            break;
        }
        drawInputMode();
      }
    }

    delay(5);
  }
}