/*
  Starbie: a tiny motion-controlled digital pet
  Custom firmware by Ziyaad, built on the Half Life Week 1 starter.

  Three apps. HOLD Button 2 (about half a second) to switch between them:
    PET     the original Starbie. Button 1 = menu, tap Button 2 = stats
    DESK    clock, outside weather and room temperature. Button 1 = refresh weather
    REMOTE  Bluetooth slide clicker. Button 1 = next slide, tap Button 2 = previous

  BEFORE UPLOADING TO THE REAL BOARD (these defaults are set for Wokwi):
    1. Set DHT_SENSOR_TYPE to DHT11
    2. Set ENABLE_BLE_REMOTE to 1
    3. Make secrets.h with your Wi-Fi name and password (see WI-FI below)
    4. Arduino IDE: Tools > Partition Scheme > "Huge APP (3MB No OTA/1MB SPIFFS)"
       (Wi-Fi + Bluetooth together are too big for the default layout)

  This is intentionally one file. Open this .ino file in Arduino IDE, edit
  the BEGINNER SETTINGS section, and upload it to a Seeed XIAO ESP32-C3.

  Install these libraries with Sketch > Include Library > Manage Libraries:
    - Adafruit GFX Library
    - Adafruit SSD1306
    - Adafruit MPU6050
    - DHT sensor library
  Arduino IDE will offer to install their dependencies too. Click Install All.
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <DHT.h>
#include <Preferences.h>
#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

// =========================== BEGINNER SETTINGS ===========================
// Everything most builders will want to customize is in this one section.

// --- Your board's pins ----------------------------------------------------
// These are ESP32-C3 GPIO numbers, with the XIAO pin labels beside them.
const int I2C_SDA_PIN = 6;       // XIAO D4: OLED + MPU6050 SDA
const int I2C_SCL_PIN = 7;       // XIAO D5: OLED + MPU6050 SCL
const int DHT_PIN = 3;           // XIAO D1: DHT11 data
const int BUTTON_ONE_PIN = 4;    // XIAO D2: opens/confirms the radial menu
const int BUTTON_TWO_PIN = 5;    // XIAO D3: shows/hides stats

// Set this to false if your submission does not have a DHT11.
const bool USE_DHT11 = true;

// Wokwi has no DHT11, so the simulator uses its sibling, the DHT22.
// Use DHT22 in Wokwi and DHT11 on the real board.
const uint8_t DHT_SENSOR_TYPE = DHT22;

// Most parts use these addresses. Only change them if an I2C scanner says
// your part is different (common OLED alternative: 0x3D).
const uint8_t OLED_ADDRESS = 0x3C;
const uint8_t MPU6050_ADDRESS = 0x68;

// --- Starting stats -------------------------------------------------------
// Stats are 0 to 100. They only change when you select an action or shake
// Starbie; nothing slowly drains while it sits on your desk.
const int STARTING_JOY = 70;
const int STARTING_ENERGY = 75;
const int STARTING_FULLNESS = 65;

// Set true, upload once, then set it back to false if you want a fresh pet.
const bool RESET_SAVED_PET_ON_BOOT = false;

// --- WI-FI (Desk app) -----------------------------------------------------
// Wokwi's simulated network is "Wokwi-GUEST" with no password, so the sketch
// uses that unless it finds a secrets.h file. For the real board, create a
// file called secrets.h next to this sketch containing:
//     #define WIFI_SSID "your wifi name"
//     #define WIFI_PASSWORD "your wifi password"
// NEVER commit secrets.h to GitHub. Add it to your .gitignore.
// (The ESP32-C3 only sees 2.4 GHz networks.)
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#endif

// --- Desk app ---------------------------------------------------------------
// Weather comes from Open-Meteo: free, and no API key needed.
// Change these to your own city (look it up on latlong.net).
const float WEATHER_LATITUDE = 14.5995f;    // Manila
const float WEATHER_LONGITUDE = 120.9842f;
const int UTC_OFFSET_HOURS = 8;             // Philippines is UTC+8
const bool USE_24_HOUR_CLOCK = false;
const uint32_t WEATHER_REFRESH_MS = 10UL * 60UL * 1000UL;  // every 10 minutes

// --- Remote app -------------------------------------------------------------
// 0 = Bluetooth off (use this in Wokwi, which cannot simulate Bluetooth).
// 1 = Bluetooth on (use this on the real board).
#define ENABLE_BLE_REMOTE 0
const char *BLE_DEVICE_NAME = "Starbie";

// --- Radial menu ----------------------------------------------------------
// These four actions appear at TOP, RIGHT, BOTTOM, then LEFT in the menu.
// The last word controls the matching little visual reaction.
enum PetReaction {
  NAP_REACTION,
  JUMP_REACTION,
  HEART_REACTION,
  RUN_REACTION,
};

struct MenuItem {
  const char *label;
  int joyChange;
  int energyChange;
  int fullnessChange;
  PetReaction reaction;
};

const MenuItem MENU_ITEMS[] = {
  {"NAP",   1,  18, -4, NAP_REACTION},   // top: sleep and emit Zs
  {"PLAY", 12, -9, -5, RUN_REACTION},    // right: two fast laps + hearts
  {"FEED",  3,  2,  18, JUMP_REACTION},  // bottom: wiggle and jump
  {"PET",   7,  0,  0, HEART_REACTION},  // left: jump and emit hearts
};
const int MENU_ITEM_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

// --- Movement feel --------------------------------------------------------
// Bigger MENU_TILT_LIMIT makes the menu ball move less.
const float MENU_TILT_LIMIT = 6.0f;

// MPU6050 orientation: change these settings instead of rewiring your board.
// 1. If the sensor is turned 90 degrees, set SWAP_MPU_AXES to true.
// 2. If a direction feels backwards, change that direction from 1.0f to -1.0f.
const bool SWAP_MPU_AXES = false;
const float MENU_X_DIRECTION = 1.0f;
const float MENU_Y_DIRECTION = -1.0f;

// A small dead zone makes the ball rest in the center until the board is tilted.
const float MENU_CENTER_DEADZONE = 0.8f;
const float SHAKE_THRESHOLD = 7.0f;

// Shake is the only movement that has an effect outside the radial menu.
const int SHAKE_JOY_CHANGE = 5;
const int SHAKE_ENERGY_CHANGE = -2;
const int SHAKE_FULLNESS_CHANGE = -1;

// --- Your pet's bitmap and animation -------------------------------------
// This is a simplified 32 by 32, one-bit version of the supplied character.
// You can replace it with your own art later; keep these size values matched
// to the sprite array you paste in.
const int PET_SPRITE_WIDTH = 32;
const int PET_SPRITE_HEIGHT = 32;

// The pet wanders unless it is asleep. A non-nap action does a tiny shake,
// then a jump, except PLAY which runs two fast laps.
const uint16_t PET_WALK_PIXEL_MS = 70;
const uint16_t PET_PRE_JUMP_MS = 230;
const uint16_t PET_JUMP_MS = 430;
const int PET_JUMP_HEIGHT = 16;
const uint32_t NAP_DURATION_MS = 48000;  // 48 seconds: four times the old nap.
const uint16_t HEARTS_DURATION_MS = 1600;
const uint16_t PLAY_LAP_MS = 800;
const uint8_t PLAY_LAP_COUNT = 2;

// Each byte stores eight pixels, left to right. This sparse outline keeps the
// supplied creature readable on a tiny, one-bit OLED: head, eye, legs, tail,
// and a small flower-like ear accent all stay separate instead of becoming a blob.
const uint8_t PROGMEM PET_SPRITE[] = {
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x70, 0x0a, 0x00,
  0x00, 0xf8, 0x1f, 0x00,
  0x00, 0x8c, 0x3f, 0x80,
  0x00, 0x43, 0xff, 0x00,
  0x00, 0x43, 0xff, 0x00,
  0x00, 0x30, 0x7e, 0x00,
  0x00, 0x98, 0x01, 0x00,
  0x01, 0x98, 0x01, 0x80,
  0x03, 0x00, 0x10, 0xc0,
  0x03, 0x04, 0x00, 0xc0,
  0x01, 0x8b, 0x01, 0x80,
  0x01, 0xcb, 0x03, 0x80,
  0x03, 0xc0, 0x03, 0xc0,
  0x03, 0xc0, 0x03, 0xc0,
  0x01, 0xc0, 0x03, 0x80,
  0x01, 0x80, 0x01, 0x80,
  0x00, 0x60, 0x06, 0x00,
  0x00, 0x3f, 0xfc, 0x00,
  0x00, 0x7f, 0xfe, 0x00,
  0x00, 0x70, 0x0e, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
};
// ===========================================================================
// You can read below without needing to understand every line. The rest of
// the sketch handles buttons, sensors, drawing, and saving automatically.

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int MENU_CENTER_X = 64;
const int MENU_CENTER_Y = 32;
const int MENU_ITEM_X[] = {64, 108, 64, 20};
const int MENU_ITEM_Y[] = {12, 32, 53, 32};
const int MENU_BALL_X_RANGE = 37;
const int MENU_BALL_Y_RANGE = 22;
const uint16_t BUTTON_DEBOUNCE_MS = 30;
const uint16_t MPU_READ_INTERVAL_MS = 30;
const uint16_t DHT_READ_INTERVAL_MS = 2200;
const uint16_t SHAKE_COOLDOWN_MS = 650;
const float STANDARD_GRAVITY = 9.80665f;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_MPU6050 mpu;
DHT dht(DHT_PIN, DHT_SENSOR_TYPE);
Preferences preferences;

struct PetState {
  int joy;
  int energy;
  int fullness;
};

struct ButtonState {
  int pin;
  bool stableState;
  bool lastRawState;
  uint32_t lastChangedAt;
};

enum View {
  PET_VIEW,
  MENU_VIEW,
  STATS_VIEW,
};

// Which app is showing. Hold Button 2 to cycle through them.
enum App {
  PET_APP,
  DESK_APP,
  REMOTE_APP,
};
const int APP_COUNT = 3;

// Button 2 can be a short tap or a long hold.
enum ButtonTwoEvent {
  NO_PRESS,
  SHORT_PRESS,
  LONG_PRESS,
};

PetState pet = {STARTING_JOY, STARTING_ENERGY, STARTING_FULLNESS};
ButtonState buttonOne = {BUTTON_ONE_PIN, HIGH, HIGH, 0};
ButtonState buttonTwo = {BUTTON_TWO_PIN, HIGH, HIGH, 0};
View currentView = PET_VIEW;
App currentApp = PET_APP;

const uint16_t LONG_PRESS_MS = 600;
uint32_t buttonTwoDownAt = 0;
bool buttonTwoLongFired = false;

bool mpuFound = false;
bool dhtFound = false;
float accelerationX = 0.0f;
float accelerationY = 0.0f;
float accelerationZ = STANDARD_GRAVITY;
float temperatureC = NAN;
float humidity = NAN;
float menuBallX = MENU_CENTER_X;
float menuBallY = MENU_CENTER_Y;
float menuCenterAccelerationX = 0.0f;
float menuCenterAccelerationY = 0.0f;
int selectedMenuItem = -1;
uint32_t lastMpuReadAt = 0;
uint32_t lastDhtReadAt = 0;
uint32_t lastShakeAt = 0;
uint32_t shakeAnimationEndsAt = 0;
uint32_t petJumpStartedAt = 0;
uint32_t nappingUntil = 0;
uint32_t heartAnimationEndsAt = 0;
uint32_t playRunStartedAt = 0;
int nappingPetX = 0;

int petWalkingX(uint32_t now);

int keepInRange(int value, int smallest, int largest) {
  return constrain(value, smallest, largest);
}

void changePet(int joyChange, int energyChange, int fullnessChange) {
  pet.joy = keepInRange(pet.joy + joyChange, 0, 100);
  pet.energy = keepInRange(pet.energy + energyChange, 0, 100);
  pet.fullness = keepInRange(pet.fullness + fullnessChange, 0, 100);
}

void loadPet() {
  preferences.begin("starbie", false);
  if (RESET_SAVED_PET_ON_BOOT) {
    preferences.clear();
  }
  pet.joy = preferences.getInt("joy", STARTING_JOY);
  pet.energy = preferences.getInt("energy", STARTING_ENERGY);
  pet.fullness = preferences.getInt("full", STARTING_FULLNESS);
}

void savePet() {
  preferences.putInt("joy", pet.joy);
  preferences.putInt("energy", pet.energy);
  preferences.putInt("full", pet.fullness);
}

void setUpButton(ButtonState &button) {
  pinMode(button.pin, INPUT_PULLUP);
  button.stableState = digitalRead(button.pin);
  button.lastRawState = button.stableState;
  button.lastChangedAt = millis();
}

// Returns true exactly once for each press, even if the switch bounces.
bool wasPressed(ButtonState &button) {
  const bool rawState = digitalRead(button.pin);
  const uint32_t now = millis();

  if (rawState != button.lastRawState) {
    button.lastRawState = rawState;
    button.lastChangedAt = now;
  }

  if (rawState != button.stableState &&
      now - button.lastChangedAt >= BUTTON_DEBOUNCE_MS) {
    button.stableState = rawState;
    return button.stableState == LOW;
  }

  return false;
}

// Button 2 needs to tell a tap from a hold, so it is read differently:
// a tap counts when you let go, a hold counts once it passes LONG_PRESS_MS.
ButtonTwoEvent readButtonTwo() {
  const bool rawState = digitalRead(buttonTwo.pin);
  const uint32_t now = millis();

  if (rawState != buttonTwo.lastRawState) {
    buttonTwo.lastRawState = rawState;
    buttonTwo.lastChangedAt = now;
  }

  if (rawState != buttonTwo.stableState &&
      now - buttonTwo.lastChangedAt >= BUTTON_DEBOUNCE_MS) {
    buttonTwo.stableState = rawState;
    if (rawState == LOW) {
      // Just pressed down. Start timing.
      buttonTwoDownAt = now;
      buttonTwoLongFired = false;
    } else if (!buttonTwoLongFired) {
      // Let go before it became a hold, so it was a tap.
      return SHORT_PRESS;
    }
  }

  if (buttonTwo.stableState == LOW && !buttonTwoLongFired &&
      now - buttonTwoDownAt >= LONG_PRESS_MS) {
    buttonTwoLongFired = true;
    return LONG_PRESS;
  }

  return NO_PRESS;
}

void updateMpu() {
  if (!mpuFound || millis() - lastMpuReadAt < MPU_READ_INTERVAL_MS) {
    return;
  }
  lastMpuReadAt = millis();

  sensors_event_t acceleration;
  sensors_event_t gyro;
  sensors_event_t sensorTemperature;
  mpu.getEvent(&acceleration, &gyro, &sensorTemperature);
  accelerationX = acceleration.acceleration.x;
  accelerationY = acceleration.acceleration.y;
  accelerationZ = acceleration.acceleration.z;
}

void updateDht() {
  if (!dhtFound || millis() - lastDhtReadAt < DHT_READ_INTERVAL_MS) {
    return;
  }
  lastDhtReadAt = millis();

  const float newHumidity = dht.readHumidity();
  const float newTemperatureC = dht.readTemperature();
  if (!isnan(newHumidity)) {
    humidity = newHumidity;
  }
  if (!isnan(newTemperatureC)) {
    temperatureC = newTemperatureC;
  }
}

int petWalkingX(uint32_t now) {
  const int farthestX = SCREEN_WIDTH - PET_SPRITE_WIDTH;
  const uint32_t roundTrip = static_cast<uint32_t>(farthestX) * 2;
  const uint32_t step = (now / PET_WALK_PIXEL_MS) % roundTrip;
  return step <= farthestX ? step : roundTrip - step;
}

uint32_t playRunDuration() {
  return static_cast<uint32_t>(PLAY_LAP_MS) * PLAY_LAP_COUNT;
}

bool isNapping() {
  return nappingUntil != 0 && millis() < nappingUntil;
}

bool isPlaying() {
  return playRunStartedAt != 0 && millis() - playRunStartedAt < playRunDuration();
}

int playRunX(uint32_t now) {
  const int farthestX = SCREEN_WIDTH - PET_SPRITE_WIDTH;
  const uint32_t lapAge = (now - playRunStartedAt) % PLAY_LAP_MS;
  const float progress = static_cast<float>(lapAge) / PLAY_LAP_MS;
  return progress < 0.5f ? static_cast<int>(progress * 2.0f * farthestX)
                         : static_cast<int>((1.0f - progress) * 2.0f * farthestX);
}

void updatePetTimers() {
  if (nappingUntil != 0 && !isNapping()) {
    nappingUntil = 0;  // The pet wakes naturally when its nap time is over.
  }
  if (playRunStartedAt != 0 && !isPlaying()) {
    playRunStartedAt = 0;  // PLAY is exactly two left-to-right-to-left laps.
  }
}

// While the menu is open, the MPU6050 acts like a tiny air mouse. The position
// at the instant Button 1 opens the menu becomes the center for that menu use.
void updateMenuBall() {
  float xTilt = accelerationX - menuCenterAccelerationX;
  float yTilt = accelerationY - menuCenterAccelerationY;

  if (SWAP_MPU_AXES) {
    const float oldXTilt = xTilt;
    xTilt = yTilt;
    yTilt = oldXTilt;
  }
  xTilt = constrain(xTilt * MENU_X_DIRECTION, -MENU_TILT_LIMIT, MENU_TILT_LIMIT);
  yTilt = constrain(yTilt * MENU_Y_DIRECTION, -MENU_TILT_LIMIT, MENU_TILT_LIMIT);
  const float targetX = MENU_CENTER_X + (xTilt / MENU_TILT_LIMIT) * MENU_BALL_X_RANGE;
  const float targetY = MENU_CENTER_Y + (yTilt / MENU_TILT_LIMIT) * MENU_BALL_Y_RANGE;

  // This small amount of smoothing makes the ball feel less jittery.
  menuBallX += (targetX - menuBallX) * 0.20f;
  menuBallY += (targetY - menuBallY) * 0.20f;

  // The menu has four full sectors, not four tiny targets. Any tilt direction
  // past the center dead zone selects its matching top/right/bottom/left action.
  if (fabsf(xTilt) < MENU_CENTER_DEADZONE && fabsf(yTilt) < MENU_CENTER_DEADZONE) {
    selectedMenuItem = -1;
  } else if (fabsf(xTilt) > fabsf(yTilt)) {
    selectedMenuItem = xTilt > 0.0f ? 1 : 3;  // right or left
  } else {
    selectedMenuItem = yTilt > 0.0f ? 2 : 0;  // bottom or top
  }
}

void checkForShake() {
  if (!mpuFound || currentView != PET_VIEW) {
    return;
  }

  const float magnitude = sqrtf(accelerationX * accelerationX +
                                accelerationY * accelerationY +
                                accelerationZ * accelerationZ);
  const uint32_t now = millis();
  if (fabsf(magnitude - STANDARD_GRAVITY) >= SHAKE_THRESHOLD &&
      now - lastShakeAt >= SHAKE_COOLDOWN_MS) {
    lastShakeAt = now;
    nappingUntil = 0;  // A shake wakes a sleeping pet right away.
    shakeAnimationEndsAt = now + 350;
    changePet(SHAKE_JOY_CHANGE, SHAKE_ENERGY_CHANGE, SHAKE_FULLNESS_CHANGE);
    savePet();
  }
}

void openMenu() {
  currentView = MENU_VIEW;
  selectedMenuItem = -1;
  menuBallX = MENU_CENTER_X;
  menuBallY = MENU_CENTER_Y;
  // Calibrate here so every menu starts from however the builder is holding it.
  menuCenterAccelerationX = accelerationX;
  menuCenterAccelerationY = accelerationY;
}

void chooseMenuItem() {
  if (selectedMenuItem == -1) {
    return;  // Keep the menu open until the ball reaches a choice.
  }

  const MenuItem &item = MENU_ITEMS[selectedMenuItem];
  changePet(item.joyChange, item.energyChange, item.fullnessChange);
  savePet();
  const uint32_t now = millis();
  nappingUntil = 0;
  heartAnimationEndsAt = 0;
  playRunStartedAt = 0;

  if (item.reaction == NAP_REACTION) {
    petJumpStartedAt = 0;
    nappingPetX = petWalkingX(now);
    nappingUntil = now + NAP_DURATION_MS;
  } else if (item.reaction == RUN_REACTION) {
    petJumpStartedAt = 0;
    playRunStartedAt = now;
    heartAnimationEndsAt = now + playRunDuration();
  } else {
    // A confirmed action gets a little anticipation shake before the jump.
    petJumpStartedAt = now;
    if (item.reaction == HEART_REACTION) {
      heartAnimationEndsAt = now + HEARTS_DURATION_MS;
    }
  }
  currentView = PET_VIEW;
}

// =============================== CUSTOM APPS ===============================

void drawCurrentView();  // defined further down, used by the Desk app

// --- Desk app: Wi-Fi, clock and weather -------------------------------------
float outsideTemperatureC = NAN;
int weatherCode = -1;
uint32_t nextWeatherAt = 0;   // 0 means "fetch as soon as Wi-Fi is up"
const char *deskStatus = "";  // short message when something is not ready

bool wifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void startWifiAndClock() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // connects in the background
  // Ask internet time servers for the time. This also runs in the background.
  configTime(UTC_OFFSET_HOURS * 3600L, 0, "pool.ntp.org", "time.google.com");
}

// Finds "key" in the JSON text (after position startAt) and reads the number
// right after it. Small and simple, so no JSON library is needed.
float readJsonNumber(const String &json, const char *key, int startAt) {
  const int keyAt = json.indexOf(key, startAt);
  if (keyAt < 0) {
    return NAN;
  }
  return json.substring(keyAt + strlen(key)).toFloat();
}

void fetchWeather() {
  WiFiClientSecure client;
  client.setInsecure();  // skip the certificate check; fine for public weather data
  HTTPClient http;
  http.setTimeout(5000);

  const String url = String("https://api.open-meteo.com/v1/forecast?latitude=") +
                     String(WEATHER_LATITUDE, 4) + "&longitude=" +
                     String(WEATHER_LONGITUDE, 4) +
                     "&current=temperature_2m,weather_code";

  if (!http.begin(client, url)) {
    deskStatus = "weather: bad url";
    nextWeatherAt = millis() + 30000;
    return;
  }

  const int responseCode = http.GET();
  if (responseCode != 200) {
    http.end();
    deskStatus = "weather: no reply";
    nextWeatherAt = millis() + 30000;  // try again in 30 seconds
    return;
  }

  const String body = http.getString();
  http.end();

  // The reply has a "current_units" block first, then the "current" block we want.
  const int currentAt = body.indexOf("\"current\":{");
  if (currentAt < 0) {
    deskStatus = "weather: odd reply";
    nextWeatherAt = millis() + 30000;
    return;
  }

  outsideTemperatureC = readJsonNumber(body, "\"temperature_2m\":", currentAt);
  const float code = readJsonNumber(body, "\"weather_code\":", currentAt);
  weatherCode = isnan(code) ? -1 : static_cast<int>(code);
  deskStatus = "";
  nextWeatherAt = millis() + WEATHER_REFRESH_MS;
}

void updateDesk() {
  if (!wifiConnected()) {
    deskStatus = "wifi: connecting...";
    return;
  }
  if (deskStatus[0] != '\0' && strcmp(deskStatus, "wifi: connecting...") == 0) {
    deskStatus = "";
  }

  const bool weatherDue = nextWeatherAt == 0 ||
                          static_cast<int32_t>(millis() - nextWeatherAt) >= 0;
  if (weatherDue) {
    deskStatus = "weather: updating...";
    drawCurrentView();  // show the message before the slow network call
    fetchWeather();
  }
}

// Open-Meteo weather codes, squeezed into one short word.
const char *weatherWord(int code) {
  if (code < 0) return "--";
  if (code == 0) return "Clear";
  if (code <= 2) return "Cloudy";
  if (code == 3) return "Overcast";
  if (code <= 48) return "Fog";
  if (code <= 57) return "Drizzle";
  if (code <= 67) return "Rain";
  if (code <= 77) return "Snow";
  if (code <= 82) return "Showers";
  return "Storm";
}

void printCentered(const char *text, int y, int textSize) {
  display.setTextSize(textSize);
  const int width = strlen(text) * 6 * textSize;
  display.setCursor((SCREEN_WIDTH - width) / 2, y);
  display.print(text);
}

void drawDesk() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Clock. getLocalTime(..., 0) checks once and never waits.
  struct tm timeNow;
  if (getLocalTime(&timeNow, 0)) {
    char clockText[6];
    strftime(clockText, sizeof(clockText), USE_24_HOUR_CLOCK ? "%H:%M" : "%I:%M", &timeNow);
    printCentered(clockText, 0, 3);

    char dateText[12];
    strftime(dateText, sizeof(dateText), "%a %d %b", &timeNow);
    printCentered(dateText, 27, 1);
  } else {
    printCentered("--:--", 0, 3);
    printCentered("syncing time", 27, 1);
  }

  // Outside weather, or a status message if it is not ready.
  display.setTextSize(1);
  display.setCursor(4, 41);
  if (deskStatus[0] != '\0') {
    display.print(deskStatus);
  } else {
    display.print("OUT ");
    if (!isnan(outsideTemperatureC)) {
      display.print(static_cast<int>(roundf(outsideTemperatureC)));
      display.print("C ");
    } else {
      display.print("-- ");
    }
    display.print(weatherWord(weatherCode));
  }

  // Room temperature and humidity from Starbie's own sensor.
  display.setCursor(4, 54);
  display.print("IN  ");
  if (!isnan(temperatureC)) {
    display.print(static_cast<int>(roundf(temperatureC)));
    display.print("C");
  } else {
    display.print("--");
  }
  display.setCursor(76, 54);
  if (!isnan(humidity)) {
    display.print("H ");
    display.print(static_cast<int>(roundf(humidity)));
    display.print("%");
  } else {
    display.print("H --");
  }
}

// --- Remote app: Bluetooth slide clicker -------------------------------------
// Standard keyboard key codes for the arrow keys.
const uint8_t REMOTE_KEY_NEXT = 0x4F;  // right arrow
const uint8_t REMOTE_KEY_BACK = 0x50;  // left arrow

uint8_t lastRemoteKey = 0;
uint32_t lastRemoteKeyAt = 0;

#if ENABLE_BLE_REMOTE
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>

// Tells the computer "I am a keyboard": 8 modifier keys, 1 spare byte,
// then up to 6 keys pressed at once.
const uint8_t KEYBOARD_REPORT_MAP[] = {
  0x05, 0x01,  // Usage Page (Generic Desktop)
  0x09, 0x06,  // Usage (Keyboard)
  0xA1, 0x01,  // Collection (Application)
  0x85, 0x01,  //   Report ID (1)
  0x05, 0x07,  //   Usage Page (Keyboard)
  0x19, 0xE0,  //   Usage Minimum (Left Control)
  0x29, 0xE7,  //   Usage Maximum (Right GUI)
  0x15, 0x00,  //   Logical Minimum (0)
  0x25, 0x01,  //   Logical Maximum (1)
  0x75, 0x01,  //   Report Size (1)
  0x95, 0x08,  //   Report Count (8)
  0x81, 0x02,  //   Input (modifier keys)
  0x95, 0x01,  //   Report Count (1)
  0x75, 0x08,  //   Report Size (8)
  0x81, 0x01,  //   Input (spare byte)
  0x95, 0x06,  //   Report Count (6)
  0x75, 0x08,  //   Report Size (8)
  0x15, 0x00,  //   Logical Minimum (0)
  0x25, 0x65,  //   Logical Maximum (101)
  0x05, 0x07,  //   Usage Page (Keyboard)
  0x19, 0x00,  //   Usage Minimum (0)
  0x29, 0x65,  //   Usage Maximum (101)
  0x81, 0x00,  //   Input (the pressed keys)
  0xC0         // End Collection
};

BLEHIDDevice *hidDevice = nullptr;
BLECharacteristic *keyboardInput = nullptr;
bool bleStarted = false;
bool bleConnected = false;

class RemoteConnectionCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleConnected = true;
  }
  void onDisconnect(BLEServer *server) override {
    bleConnected = false;
    BLEDevice::startAdvertising();  // be ready to pair again
  }
};

// Bluetooth only starts the first time you open the Remote app, so the pet
// and the desk app never pay for it.
void startBleRemote() {
  if (bleStarted) {
    return;
  }
  bleStarted = true;

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new RemoteConnectionCallbacks());

  hidDevice = new BLEHIDDevice(server);
  keyboardInput = hidDevice->inputReport(1);
  hidDevice->manufacturer()->setValue("Starbie");
  hidDevice->pnp(0x02, 0xe502, 0xa111, 0x0210);
  hidDevice->hidInfo(0x00, 0x01);

  BLESecurity *security = new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_BOND);

  hidDevice->reportMap((uint8_t *)KEYBOARD_REPORT_MAP, sizeof(KEYBOARD_REPORT_MAP));
  hidDevice->startServices();

  BLEAdvertising *advertising = server->getAdvertising();
  advertising->setAppearance(HID_KEYBOARD);
  advertising->addServiceUUID(hidDevice->hidService()->getUUID());
  advertising->start();
}
#endif

void sendRemoteKey(uint8_t key) {
  lastRemoteKey = key;
  lastRemoteKeyAt = millis();  // the screen flashes the arrow, even in Wokwi

#if ENABLE_BLE_REMOTE
  if (!bleConnected) {
    return;
  }
  uint8_t pressReport[8] = {0, 0, key, 0, 0, 0, 0, 0};
  keyboardInput->setValue(pressReport, sizeof(pressReport));
  keyboardInput->notify();
  delay(8);
  uint8_t releaseReport[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  keyboardInput->setValue(releaseReport, sizeof(releaseReport));
  keyboardInput->notify();
#endif
}

void drawRemote() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("REMOTE", 0, 2);

#if ENABLE_BLE_REMOTE
  printCentered(bleConnected ? "connected" : "pair: Starbie", 19, 1);
#else
  printCentered("bluetooth off (sim)", 19, 1);
#endif

  if (millis() - lastRemoteKeyAt < 350 && lastRemoteKey != 0) {
    printCentered(lastRemoteKey == REMOTE_KEY_NEXT ? "NEXT >" : "< BACK", 33, 2);
  } else {
    display.setTextSize(1);
    display.setCursor(4, 33);
    display.print("B1  next slide  >");
    display.setCursor(4, 43);
    display.print("B2  prev slide  <");
  }
  printCentered("hold B2: switch app", 55, 1);
  display.setTextSize(1);
}

// --- App switching ------------------------------------------------------------
void switchToNextApp() {
  currentApp = static_cast<App>((currentApp + 1) % APP_COUNT);
  currentView = PET_VIEW;  // leave the pet's menu or stats behind
  if (currentApp == DESK_APP && wifiConnected()) {
    nextWeatherAt = 0;  // fresh weather whenever you open the desk
  }
#if ENABLE_BLE_REMOTE
  if (currentApp == REMOTE_APP) {
    startBleRemote();
  }
#endif
}

void handleButtons() {
  const ButtonTwoEvent buttonTwoEvent = readButtonTwo();
  const bool buttonOnePressed = wasPressed(buttonOne);

  if (buttonTwoEvent == LONG_PRESS) {
    switchToNextApp();
    return;
  }

  if (currentApp == PET_APP) {
    if (buttonOnePressed) {
      if (currentView == MENU_VIEW) {
        chooseMenuItem();
      } else {
        openMenu();
      }
    }
    if (buttonTwoEvent == SHORT_PRESS) {
      // Button 2 never changes the pet. It only lets you peek at the values.
      currentView = currentView == STATS_VIEW ? PET_VIEW : STATS_VIEW;
    }
  } else if (currentApp == DESK_APP) {
    if (buttonOnePressed) {
      nextWeatherAt = 0;  // refresh the weather now
    }
  } else if (currentApp == REMOTE_APP) {
    if (buttonOnePressed) {
      sendRemoteKey(REMOTE_KEY_NEXT);
    }
    if (buttonTwoEvent == SHORT_PRESS) {
      sendRemoteKey(REMOTE_KEY_BACK);
    }
  }
}

void drawHeart(int x, int y) {
  // A tiny seven-pixel-wide heart that stays crisp on the OLED.
  display.fillRect(x - 2, y, 2, 2, SSD1306_WHITE);
  display.fillRect(x + 1, y, 2, 2, SSD1306_WHITE);
  display.fillRect(x - 3, y + 2, 7, 2, SSD1306_WHITE);
  display.fillRect(x - 2, y + 4, 5, 1, SSD1306_WHITE);
  display.fillRect(x - 1, y + 5, 3, 1, SSD1306_WHITE);
  display.drawPixel(x, y + 6, SSD1306_WHITE);
}

void drawHearts(uint32_t now, int petX, int petY) {
  if (now >= heartAnimationEndsAt) {
    return;
  }

  const float progress =
      1.0f - static_cast<float>(heartAnimationEndsAt - now) / HEARTS_DURATION_MS;
  const int rise = static_cast<int>(progress * 18.0f);
  drawHeart(petX + 10, petY - 3 - rise);
  drawHeart(petX + 22, petY - 9 - rise / 2);
}

void drawSleepZs(uint32_t now, int petX, int petY) {
  // The Zs float upward, then loop back down while the pet is napping.
  const int rise = static_cast<int>((now / 300UL) % 15UL);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(petX + 23, petY - 2 - rise);
  display.print("z");
  display.setCursor(petX + 28, petY - 8 - rise / 2);
  display.print("z");
}

void drawPet() {
  display.clearDisplay();

  const uint32_t now = millis();
  int petX = isNapping() ? nappingPetX : petWalkingX(now);
  if (isPlaying()) {
    petX = playRunX(now);
  }
  int petY = SCREEN_HEIGHT - PET_SPRITE_HEIGHT;

  // The pet always wanders except during a nap; PLAY temporarily takes over.
  if (now < shakeAnimationEndsAt) {
    petX += static_cast<int>(sinf(now / 18.0f) * 3.0f);
  }

  if (!isNapping() && petJumpStartedAt != 0) {
    const uint32_t animationAge = now - petJumpStartedAt;

    if (animationAge < PET_PRE_JUMP_MS) {
      petX += static_cast<int>(sinf(now / 16.0f) * 3.0f);
    } else if (animationAge < PET_PRE_JUMP_MS + PET_JUMP_MS) {
      const float jumpProgress =
          static_cast<float>(animationAge - PET_PRE_JUMP_MS) / PET_JUMP_MS;
      petY -= static_cast<int>(sinf(jumpProgress * PI) * PET_JUMP_HEIGHT);
    } else {
      petJumpStartedAt = 0;
    }
  }

  petX = constrain(petX, 0, SCREEN_WIDTH - PET_SPRITE_WIDTH);
  display.drawBitmap(petX, petY, PET_SPRITE, PET_SPRITE_WIDTH, PET_SPRITE_HEIGHT,
                     SSD1306_WHITE);

  if (isNapping()) {
    drawSleepZs(now, petX, petY);
  }
  drawHearts(now, petX, petY);
}

void drawMenuItem(int item) {
  const int boxWidth = 33;
  const int boxHeight = 12;
  const int boxX = MENU_ITEM_X[item] - boxWidth / 2;
  const int boxY = MENU_ITEM_Y[item] - boxHeight / 2;
  const bool isSelected = item == selectedMenuItem;

  if (isSelected) {
    display.fillRoundRect(boxX, boxY, boxWidth, boxHeight, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.drawRoundRect(boxX, boxY, boxWidth, boxHeight, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
  }

  display.setTextSize(1);
  const int labelLength = strlen(MENU_ITEMS[item].label);
  display.setCursor(MENU_ITEM_X[item] - labelLength * 3, MENU_ITEM_Y[item] - 3);
  display.print(MENU_ITEMS[item].label);
  display.setTextColor(SSD1306_WHITE);
}

void drawMenu() {
  display.clearDisplay();
  // The two diagonal lines divide the screen into four big selection sectors.
  display.drawLine(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, SSD1306_WHITE);
  display.drawLine(0, SCREEN_HEIGHT - 1, SCREEN_WIDTH - 1, 0, SSD1306_WHITE);
  for (int item = 0; item < MENU_ITEM_COUNT; item++) {
    drawMenuItem(item);
  }

  display.drawCircle(MENU_CENTER_X, MENU_CENTER_Y, 11, SSD1306_WHITE);
  display.fillCircle(static_cast<int>(menuBallX), static_cast<int>(menuBallY), 4,
                     SSD1306_WHITE);
}

void drawStatBar(int y, const char *label, int value) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, y);
  display.print(label);
  display.drawRect(48, y, 57, 8, SSD1306_WHITE);
  display.fillRect(49, y + 1, map(value, 0, 100, 0, 55), 6, SSD1306_WHITE);
  display.setCursor(109, y);
  display.print(value);
}

void drawStats() {
  display.clearDisplay();
  drawStatBar(5, "JOY", pet.joy);
  drawStatBar(20, "ENERGY", pet.energy);
  drawStatBar(35, "FULL", pet.fullness);

  display.setCursor(4, 53);
  if (!isnan(temperatureC)) {
    display.print("TEMP ");
    display.print(static_cast<int>(temperatureC));
    display.print("C");
  } else {
    display.print("TEMP --");
  }

  display.setCursor(76, 53);
  if (!isnan(humidity)) {
    display.print("H ");
    display.print(static_cast<int>(humidity));
    display.print("%");
  } else {
    display.print("H --");
  }
}

void drawCurrentView() {
  if (currentApp == DESK_APP) {
    drawDesk();
  } else if (currentApp == REMOTE_APP) {
    drawRemote();
  } else if (currentView == MENU_VIEW) {
    drawMenu();
  } else if (currentView == STATS_VIEW) {
    drawStats();
  } else {
    drawPet();
  }
  display.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  setUpButton(buttonOne);
  setUpButton(buttonTwo);
  loadPet();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED not found. Check power, GND, SDA, SCL, and address.");
    while (true) {
      delay(10);
    }
  }

  mpuFound = mpu.begin(MPU6050_ADDRESS, &Wire);
  if (mpuFound) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  } else {
    Serial.println("MPU6050 not found. The menu will not move until it is connected.");
  }

  if (USE_DHT11) {
    dht.begin();
    dhtFound = true;
  }

  drawCurrentView();    // show Starbie right away
  startWifiAndClock();  // Wi-Fi takes a moment, so start it after the first frame
}

void loop() {
  updateMpu();
  updateDht();
  updatePetTimers();
  handleButtons();

  if (currentApp == PET_APP) {
    if (currentView == MENU_VIEW) {
      updateMenuBall();
    } else {
      checkForShake();
    }
  } else if (currentApp == DESK_APP) {
    updateDesk();
  }

  drawCurrentView();
  delay(16);  // Smooth display updates without making the code complicated.
}
