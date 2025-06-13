// Includes
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <time.h>
#include <Preferences.h>  // <-- Added for persistent storage

// TFT pins
#define TFT_CS     5
#define TFT_RST    15
#define TFT_DC     2
#define TFT_SCLK   18
#define TFT_MOSI   23

// Display setup
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// Web server
WebServer server(80);

// Preferences for storing token selection and timezone
Preferences preferences;

// Token list
struct Token {
  const char* name;
  const char* id;
};

Token tokens[] = {
  {"Bitcoin", "bitcoin"},
  {"Ethereum", "ethereum"},
  {"Binance Coin", "binancecoin"},
  {"Tether", "tether"},
  {"USD Coin", "usd-coin"},
  {"XRP", "ripple"},
  {"Cardano", "cardano"},
  {"Dogecoin", "dogecoin"},
  {"Polkadot", "polkadot"},
  {"Uniswap", "uniswap"},
  {"Litecoin", "litecoin"},
  {"Chainlink", "chainlink"},
  {"Bitcoin Cash", "bitcoin-cash"},
  {"Stellar", "stellar"},
  {"VeChain", "vechain"},
  {"Filecoin", "filecoin"},
  {"Ethereum Classic", "ethereum-classic"},
  {"Theta", "theta"},
  {"TRON", "tron"},
  {"EOS", "eos"},
  {"Tezos", "tezos"},
  {"Aave", "aave"},
  {"Cosmos", "cosmos"},
  {"Shiba Inu", "shiba-inu"},
  {"Polygon", "matic-network"},
  {"Algorand", "algorand"}
};

const int tokenCount = sizeof(tokens) / sizeof(tokens[0]);
int currentTokenIndex = 0;

const char* defaultTimezone = "GMT0BST,M3.5.0/1,M10.5.0/2"; // UK time
String selectedTimezone = defaultTimezone;

float tokenPrice = 0.0;
float lastDisplayedPrice = -1.0;
unsigned long lastUpdateTime = 0;
unsigned long updateInterval = 60000;
unsigned long progressStartTime = 0;
unsigned long connectionTime = 0;

void fetchTokenPrice(const char* tokenId);
void drawProgressBar(unsigned long elapsedTime);

// HTML generation
String getTokenOptions() {
  String html = "";
  for (int i = 0; i < tokenCount; i++) {
    html += "<option value='" + String(i) + "'";
    if (i == currentTokenIndex) html += " selected";
    html += ">" + String(tokens[i].name) + "</option>";
  }
  return html;
}

String getIntervalOptions() {
  int intervals[] = {15000, 30000, 60000, 300000, 900000, 1800000, 3600000};
  String labels[] = {"15s", "30s", "60s", "5m", "15m", "30m", "1h"};
  String html = "";
  for (int i = 0; i < 7; i++) {
    html += "<option value='" + String(intervals[i]) + "'";
    if (intervals[i] == updateInterval) html += " selected";
    html += ">" + labels[i] + "</option>";
  }
  return html;
}

String getTimezoneOptions() {
  struct TimezoneOption {
    const char* label;
    const char* value;
  };
  TimezoneOption timezones[] = {
    {"UK (GMT/BST)", "GMT0BST,M3.5.0/1,M10.5.0/2"},
    {"UTC", "UTC0"},
    {"CET (Europe)", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {"EST (US East)", "EST5EDT,M3.2.0,M11.1.0"},
    {"PST (US West)", "PST8PDT,M3.2.0,M11.1.0"},
    {"Japan", "JST-9"},
    {"India", "IST-5:30"},
    {"Australia (Sydney)", "AEST-10AEDT,M10.1.0,M4.1.0"}
  };

  String html = "";
  for (auto tz : timezones) {
    html += "<option value='" + String(tz.value) + "'";
    if (selectedTimezone == tz.value) html += " selected";
    html += ">" + String(tz.label) + "</option>";
  }
  return html;
}

void handleRoot() {
  String html = "<h1>Select a Token</h1>";
  html += "<form action='/select' method='GET'>";
  html += "<label>Token:</label><br><select name='token'>" + getTokenOptions() + "</select><br><br>";
  html += "<label>Update Interval:</label><br><select name='interval'>" + getIntervalOptions() + "</select><br><br>";
  html += "<label>Timezone:</label><br><select name='timezone'>" + getTimezoneOptions() + "</select><br><br>";
  html += "<input type='submit' value='Track'>";
  html += "</form><br><a href='/reset'>Reset WiFi</a>";
  server.send(200, "text/html", html);
}

void handleSelection() {
  if (server.hasArg("token")) {
    currentTokenIndex = server.arg("token").toInt();
    preferences.putInt("tokenIndex", currentTokenIndex);  // Save token selection
  }
  if (server.hasArg("interval")) {
    updateInterval = server.arg("interval").toInt();
    preferences.putULong("updateInterval", updateInterval); // Save update interval
  }
  if (server.hasArg("timezone")) {
    selectedTimezone = server.arg("timezone");
    preferences.putString("timezone", selectedTimezone);  // Save timezone
  }

  setenv("TZ", selectedTimezone.c_str(), 1);
  tzset();

  String message = "<p>Tracking " + String(tokens[currentTokenIndex].name) + "</p>";
  message += "<p>Refresh rate: " + String(updateInterval / 1000) + "s</p>";
  message += "<p>Timezone: " + selectedTimezone + "</p>";
  message += "<a href='/'>Go back</a>";
  server.send(200, "text/html", message);
}

void setup() {
  Serial.begin(115200);

  // Initialize Preferences
  preferences.begin("settings", false);

  // Load saved values or use defaults
  currentTokenIndex = preferences.getInt("tokenIndex", 0);
  updateInterval = preferences.getULong("updateInterval", 60000);
  selectedTimezone = preferences.getString("timezone", defaultTimezone);

  tft.initR(INITR_18GREENTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("WiFi");
  tft.setCursor(10, 30);
  tft.println("Connecting..");

  WiFiManager wm;
  wm.setAPCallback([](WiFiManager* wm) {
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    Serial.println(wm->getConfigPortalSSID());
  });

  wm.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  bool res = wm.autoConnect("Tokii", "BoopGoop13");

  if (!res) {
    Serial.println("Failed to connect");
    delay(3000);
    ESP.restart();
  }

  connectionTime = millis();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", selectedTimezone.c_str(), 1);
  tzset();

  server.on("/", handleRoot);
  server.on("/select", handleSelection);
  server.on("/reset", []() {
    server.send(200, "text/html", "<p>Resetting WiFi settings. Rebooting...</p>");
    delay(1000);
    WiFiManager wm;
    wm.resetSettings();
    delay(1000);
    ESP.restart();
  });

  server.begin();
  progressStartTime = millis();
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  if (now - connectionTime < 15000) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.print("IP: ");
    tft.println(WiFi.localIP());
    delay(1000);
    return;
  }

  if (now - lastUpdateTime >= updateInterval || tokenPrice == 0.0) {
    fetchTokenPrice(tokens[currentTokenIndex].id);
    lastUpdateTime = now;
    progressStartTime = now;
  }

  if (tokenPrice != lastDisplayedPrice) {
    tft.fillScreen(ST77XX_BLACK);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
      return;
    }

    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);

    tft.setCursor(130, 0);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(timeStr);

    tft.setCursor(10, 30);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(tokens[currentTokenIndex].name);

    tft.setCursor(10, 60);
    char priceStr[20];
    sprintf(priceStr, "$%.2f", tokenPrice);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(priceStr);

    if (lastDisplayedPrice != -1.0) {
      if (tokenPrice > lastDisplayedPrice) {
        tft.fillCircle(140, 65, 5, ST77XX_GREEN);
      } else if (tokenPrice < lastDisplayedPrice) {
        tft.fillCircle(140, 65, 5, ST77XX_BLUE);
      }
    }

    lastDisplayedPrice = tokenPrice;
  }

  drawProgressBar(now - progressStartTime);
}

void fetchTokenPrice(const char* tokenId) {
  HTTPClient http;
  String url = "https://api.coingecko.com/api/v3/simple/price?ids=" + String(tokenId) + "&vs_currencies=usd";
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      tokenPrice = doc[tokenId]["usd"].as<float>();
      Serial.printf("Price of %s: $%.2f\n", tokenId, tokenPrice);
    } else {
      Serial.println("JSON parse error");
    }
  } else {
    Serial.printf("HTTP request failed, code: %d\n", httpCode);
  }
  http.end();
}

void drawProgressBar(unsigned long elapsedTime) {
  int width = map(elapsedTime, 0, updateInterval, 0, tft.width());
  tft.fillRect(0, tft.height() - 5, width, 5, ST77XX_GREEN);
  if (width < tft.width()) {
    tft.fillRect(width, tft.height() - 5, tft.width() - width, 5, ST77XX_BLACK);
  }
}
