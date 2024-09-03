/*************************************************************************
 *
 * This file is part of the DoubleWallClock Arduino sketch.
 * Copyleft 2024 Nicolas Agius <nicolas.agius@lps-it.fr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * ***********************************************************************/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiManager.h>              // See https://github.com/tzapu/WiFiManager for documentation    
#include <Adafruit_NeoPixel.h>
#include <ezTime.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

#include "Logger.h"

//#define NODEMCU       // Uncomment to run on Generic ESP8266
#define DISPLAY_BINARY  // Uncomment to display seconds as binary instead of flashing double dot

// GPIO configuration
#ifdef NODEMCU
  // Nodemcu
  #define GPIO_NEOPIXEL 5   // D1
  #define GPIO_CLOCK 14     // D5
  #define GPIO_LATCH 12     // D6
  #define GPIO_DATA 13      // D7
#else
  // ESP01
  #define GPIO_NEOPIXEL 0   // GPIO0
  #define GPIO_CLOCK 3      // GPIO3 RX
  #define GPIO_LATCH 2      // GPIO2
  #define GPIO_DATA 1       // GPIO1 TX
#endif

// Default value
#define DEFAULT_HOSTNAME "DoubleWallClock"
#define DEFAULT_LOGIN ""              // AuthBasic credentials
#define DEFAULT_PASSWORD ""           // (default no auth)
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define DEFAULT_TIMEZONE_A "Europe/London"
#define DEFAULT_TIMEZONE_B "Europe/Madrid"
#define DEFAULT_ALT_URL ""  // HTTP URL for alternate display
#define DEFAULT_BRIGHTNESS 255

// Internal constant
#define VERSION "1.0"

#define AUTHBASIC_LEN 21        // Login or password 20 char max
#define BUF_SIZE 512            // Used for string buffers
#define NEOPIXEL_COUNT 8        // Length of Neopixel strip
#define DNS_SIZE 255            // Used for DNS names
//#define ALLOW_CORS_FOR_LOCAL_DEV

struct ST_SETTINGS {
  bool debug;
  char login[AUTHBASIC_LEN];
  char password[AUTHBASIC_LEN];
  char ntp_server[DNS_SIZE];
  char alt_url[DNS_SIZE];
  uint8_t brightness; // Max 255
};

// Global variables
Logger logger = Logger();
ESP8266WebServer server(80);
WiFiClient client;
HTTPClient http;
ST_SETTINGS settings;
bool shouldSaveConfig = false;    // Flag for WifiManager custom parameters
char buffer[BUF_SIZE];            // Global char* to avoir multiple String concatenation which causes RAM fragmentation
StaticJsonDocument<BUF_SIZE> json_output;

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(NEOPIXEL_COUNT, GPIO_NEOPIXEL, NEO_GRB + NEO_KHZ800);
// Argument 1 = Number of pixels in NeoPixel strip
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)

const static uint32_t RED = strip.Color(255,   0,   0);
const static uint32_t BLUE = strip.Color(0,   0,   255);
const static uint32_t ORANGE = strip.Color(255,  128,  13);

Timezone tzA;
Timezone tzB;
long last_display_refresh = 0L;   // Timer to refresh displays every seconds
bool dot;                         // Flip-flop for dot display
bool alt_display;                 // Flip-flop for display page

void setupNTP()
{
  displayProgress(2);

  tzA.setLocation(DEFAULT_TIMEZONE_A);
  tzB.setLocation(DEFAULT_TIMEZONE_B);
  setServer(settings.ntp_server);
  waitForSync(60);  // 60s timeout on initial NTP request
}

void setupMDNS()
{
  if(MDNS.begin(DEFAULT_HOSTNAME))
  {
    displayProgress(3);

    MDNS.addService("http", "tcp", 80);
    logger.info("mDNS activated.");
  }
  else
  {
    logger.info("Error setting up mDNS responder");
  }
  delay(500);
}

void setupNeoPixel()
{
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(settings.brightness); // Set BRIGHTNESS (max = 255)
  displayProgress(1);
}

void setup()
{
  EEPROM.begin(1024);
  WiFiManager wifiManager;

#ifdef NODEMCU
  Serial.begin(115200);
  logger.setSerial(true);
#endif

  // Setup GPIO
#ifndef NODEMCU
  // Use TX abd RX as general GPIO https://www.esp8266.com/wiki/doku.php?id=esp8266_gpio_pin_allocations
  pinMode(GPIO_CLOCK, FUNCTION_3);
  pinMode(GPIO_DATA, FUNCTION_3);
  delay(10);
#endif

  pinMode(GPIO_CLOCK, OUTPUT);
  pinMode(GPIO_DATA, OUTPUT);
  pinMode(GPIO_LATCH, OUTPUT);

  digitalWrite(GPIO_CLOCK, LOW);
  digitalWrite(GPIO_DATA, LOW);
  digitalWrite(GPIO_LATCH, LOW);

  // Load settigns from flash
  loadSettings();

  // Startup messsage
  setupNeoPixel();
  display(88,88,88); // Test all segments
  logger.info("DoubleWallClock version %s started.", VERSION);

  // Configure custom parameters
  WiFiManagerParameter http_login("htlogin", "HTTP Login", settings.login, AUTHBASIC_LEN);
  WiFiManagerParameter http_password("htpassword", "HTTP Password", settings.password, AUTHBASIC_LEN, "type='password'");
  wifiManager.setSaveConfigCallback([](){
    shouldSaveConfig = true;
  });
  wifiManager.addParameter(&http_login);
  wifiManager.addParameter(&http_password);
  
  // Connect to Wifi or ask for SSID
  wifiManager.setHostname(DEFAULT_HOSTNAME);
  wifiManager.autoConnect(DEFAULT_HOSTNAME);

  // Save new configuration set by captive portal
  if(shouldSaveConfig)
  {
    strncpy(settings.login, http_login.getValue(), AUTHBASIC_LEN);
    strncpy(settings.password, http_password.getValue(), AUTHBASIC_LEN);

    logger.info("Saving new config from portal web page");
    saveSettings();
  }

  // Display local ip
  logger.info("Connected. IP address: %s", WiFi.localIP().toString().c_str());
  http.setReuse(true);  // Bug in http client if not set

  // Setup HTTP handlers
#ifdef ALLOW_CORS_FOR_LOCAL_DEV
  server.enableCORS(true);
  server.on("/settings", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST,GET,OPTIONS");
    server.send(204);
 });
#endif
  server.on("/", handleGETRoot );
  server.on("/debug", HTTP_GET, handleGETDebug);
  server.on("/settings", HTTP_GET, handleGETSettings);
  server.on("/settings", HTTP_POST, handlePOSTSettings);
  server.on("/reset", HTTP_POST, handlePOSTReset);
  server.begin();
  
  logger.info("HTTP server started.");

  setupNTP();
  setupMDNS();
  displayTime();
}

void displayProgress(uint8_t value)
{
  strip.clear();
  if(value > 0)
  {
    strip.fill(ORANGE, 0, value);
    strip.show();
  }
}

void displayBinary(uint8_t value)
{
  uint32_t color = alt_display ? BLUE : RED;
  logger.debug("Display binary for %i", value);

  strip.clear();
  for(int i=0; i<8; i++)
  {
    if(bitRead(value, i))
    {
      strip.setPixelColor(i, color);  
    }
  }
  strip.show();
}

void toggleDot()
{
  dot=!dot;

  uint32_t color = alt_display ? BLUE : RED;

  strip.clear();
  if(dot)
  {
    strip.setPixelColor(0, color);
    strip.setPixelColor(7, color);
  }
  strip.show();
}

void convertIntToDigits(byte digits[2], int value)
{
  if(value > 99)
  {
    digits[0]=' ';
    digits[1]='-';
  }
  else if(value < -9)
  {
    digits[0]='-';
    digits[1]='-';
  }
  else if(value < 0)
  {
    digits[0]='-';
    digits[1]=abs(value);
  }
  else
  {
    digits[0]=value / 10;
    digits[1]=value % 10;
  }
}

// Expected JSON format from external URL:
// Value are Integer ranging between -9 and 99
// {"A":34,"B":31,"C":52}
bool getAlternateData(int data[3])
{
  StaticJsonDocument<BUF_SIZE> json;
  int httpCode;

  logger.debug("Fetching data from %s", settings.alt_url);

  if (http.begin(client, settings.alt_url))
  { 
    httpCode = http.GET();

    if(httpCode == HTTP_CODE_OK)
    {
      String payload = http.getString();
      logger.debug("Alternate data: %s", payload.c_str());

      DeserializationError error = deserializeJson(json, payload);
      if(error)
      {
        logger.info("[HTTP] JSON deserialize failed: %s", error.c_str());
        http.end();
        return false;
      }

      data[0]=json["A"];
      data[1]=json["B"];
      data[2]=json["C"];

      logger.debug("Parsed values %i:%i:%i", data[0], data[1], data[2]);
    }
    else
    {
      logger.info("[HTTP] GET failed, error: (%i) %s", httpCode, http.errorToString(httpCode).c_str());
      http.end();
      return false;
    }

    http.end();
  } 
  else
  {
    logger.info("[HTTP] Unable to connect to %s", settings.alt_url);
    return false;
  }

  return true;
}

void displayError()
{
  postNumber(' ', false);
  postNumber(' ', false);
  postNumber(' ', false);
  postNumber(' ', false);
  postNumber('E', false);
  postNumber('r', false);

  //Latch the current segment data
  digitalWrite(GPIO_LATCH, LOW);
  digitalWrite(GPIO_LATCH, HIGH); //Register moves storage register on the rising edge of RCK
}

void display(int A, int B, int C)
{
  static byte digits[2];

  logger.debug("Display values %i:%i:%i", A, B, C);

  convertIntToDigits(digits, A);
  postNumber(digits[0], false);
  postNumber(digits[1], false);

  convertIntToDigits(digits, B);
  postNumber(digits[0], false);
  postNumber(digits[1], false);

  convertIntToDigits(digits, C);
  postNumber(digits[0], false);
  postNumber(digits[1], false);

  //Latch the current segment data
  digitalWrite(GPIO_LATCH, LOW);
  digitalWrite(GPIO_LATCH, HIGH); //Register moves storage register on the rising edge of RCK
}

void displayTime()
{
  tmElements_t tmA;
  tmElements_t tmB;

  alt_display = false;
  breakTime(tzA.now(), tmA);
  breakTime(tzB.now(), tmB);

  display(tmA.Hour, tmB.Hour, tmA.Minute);
}

void displayAlternate()
{
  static int data[3];
  if(getAlternateData(data))
  {
    display(data[0], data[1], data[2]);
  }
  else
  {
    displayError();
  }

  alt_display = true;
}

void updateDisplay()
{
  int seconds = tzA.second();

  if(seconds == 0 || seconds == 34)
    displayTime();

  if(seconds == 26 && strlen(settings.alt_url) > 1)
    displayAlternate();
}

void loop()
{
  MDNS.update();
  server.handleClient();
  events();  // EZTime events
  
  long current_millis = millis();
  if(current_millis - last_display_refresh > 1000)
  {
    updateDisplay();
#ifdef DISPLAY_BINARY
    displayBinary(tzA.second());
#else
    toggleDot();
#endif

    // update the timing variable
    last_display_refresh = current_millis;
  }
}

//Given a number, or '-', shifts it out to the display
void postNumber(byte number, boolean decimal)
{
  //    -  A
  //   / / F/B
  //    -  G
  //   / / E/C
  //    -. D/DP

#define a  1<<0
#define b  1<<6
#define c  1<<5
#define d  1<<4
#define e  1<<3
#define f  1<<1
#define g  1<<2
#define dp 1<<7

  byte segments;

  switch (number)
  {
    case 1: segments = b | c; break;
    case 2: segments = a | b | d | e | g; break;
    case 3: segments = a | b | c | d | g; break;
    case 4: segments = f | g | b | c; break;
    case 5: segments = a | f | g | c | d; break;
    case 6: segments = a | f | g | e | c | d; break;
    case 7: segments = a | b | c; break;
    case 8: segments = a | b | c | d | e | f | g; break;
    case 9: segments = a | b | c | d | f | g; break;
    case 0: segments = a | b | c | d | e | f; break;
    case ' ': segments = 0; break;
    case 'c': segments = g | e | d; break;
    case '-': segments = g; break;
    case 'E': segments = a | d | e | f | g ; break;
    case 'r': segments = e | g ; break;
  }

  if (decimal) segments |= dp;

  //Clock these bits out to the drivers
  for (byte i = 0 ; i < 8 ; i++)
  {
    digitalWrite(GPIO_CLOCK, LOW);
    digitalWrite(GPIO_DATA, segments & 1 << (7 - i));
    digitalWrite(GPIO_CLOCK, HIGH); //Data transfers to the register on the rising edge of SRCK
  }
}
