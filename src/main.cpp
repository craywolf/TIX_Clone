#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ClickButton.h>
#include <RTClib.h>

/*
 * Official TIX menu functions:
 *
 * Hold 'Set' to set time (in original it was a short press)
 * - Left 2 indicators flash. Use up/down to set from 1 to 12.
 * - Press again, tens-of-minutes flash. Up/down to set from 1-5.
 * - Press again, ones-of-minutes flash. Up/down to set from 1-9.
 * - Press again, all digits flash, then resume clock mode.
 *
 * Changing brightness:
 * - In normal mode, press 'Up' to cycle through brightness levels.
 *
 * Setting update rate:
 * - Hold 'Up' for 2 seconds (in original this was 'Set'). All indicators go out except one of
 *   the left-most LEDs. Press 'Up' to cycle through rates.
 * - Top LED lit = 1s
 * - Middle LED  = 4s (default)
 * - Bottom LED  = 1m
 * - Press 'Set' to return
 * 
 * New: Setting color pattern
 * - Hold 'Down' for 2 seconds (in original this did nothing)
 * - Tens-of-hours flashes, press 'Up' to cycle colors, press 'Down' to move to next
 * - Ones-of-hours flashes, " "
 * - Tens-of-minutes flashes, " "
 * - Ones-of-minutes flashes, " ", press 'Down' to save
 * 
 * New: Setting 12/24 hour time
 * - Press 'Set' button to toggle
 */

// RTC chip object
// Using a ChronoDot from Adafruit - DS3231 based
// Besides power, connect SDA to A4, and SCL to A5
RTC_DS3231 rtc;

// Button inputs
#define BTN_UP 7
#define BTN_DOWN 8
#define BTN_SET 9

ClickButton setButton(BTN_SET, LOW, CLICKBTN_PULLUP);
ClickButton upButton(BTN_UP, LOW, CLICKBTN_PULLUP);
ClickButton downButton(BTN_DOWN, LOW, CLICKBTN_PULLUP);

// Which pin on the Arduino is connected to the NeoPixels?
// On a Trinket or Gemma we suggest changing this to 1:
#define LED_PIN 6

// How many NeoPixels are attached to the Arduino?
#define LED_COUNT 27

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
// Argument 1 = Number of pixels in NeoPixel strip
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)

// Define which pixels are for each digit
//
// The LEDs are laid out to be compatible with using sections of LED strip:
//
// HourTens             HourOnes              MinuteTens             MinuteOnes
//     0      ---     1 --  2 --  3    ---      4 --  5    ---     6 --   7 -- 8
//                                                                             |
//     17     ---    16 -- 15 -- 14    ---     13 -- 12    ---    11 --  10 -- 9
//     |
//     18     ---    19 -- 20 -- 21    ---     22 -- 23    ---    24 --  25 -- 26

int hourTensLEDs[3]   = { 0, 17, 18 };
int hourTensMax       = 3;
int hourOnesLEDs[9]   = { 1, 2, 3, 16, 15, 14, 19, 20, 21 };
int hourOnesMax       = 9;
int minuteTensLEDs[6] = { 4, 5, 13, 12, 22, 23 };
int minuteTensMax     = 6;
int minuteOnesLEDs[9] = { 6, 7, 8, 11, 10, 9, 24, 25, 26 };
int minuteOnesMax     = 9;

uint32_t hourTensColor   = strip.Color(0, 255, 0);     // Red
uint32_t hourOnesColor   = strip.Color(255, 0, 0);     // Green
uint32_t minuteTensColor = strip.Color(0, 0, 255);     // Blue
//uint32_t minuteOnesColor = strip.Color(255, 255, 0);   // Yellow
uint32_t minuteOnesColor = strip.Color(0, 139, 139);   // Purple

int brightness = 50;
int brightnessMax = 200;
int brightnessMin = 50;
int brightnessStep = 50;

int hour   = 0;
int minute = 0;
int second = 0;

bool militaryTime = false;

unsigned long lastRTCUpdate = 0;
unsigned long RTCInterval = 120000; // Sync RTC every 2 minutes

unsigned long blinkInterval = 500; // menu blinks are 1/3 second
unsigned long lastBlink = 0;
bool blinkState = true;

// Menu:
// 0 = Display Time
// 1 = Set Hours
// 2 = Set Minutes Tens
// 3 = Set Minutes Ones
// 4 = Save settings and resume clock
int menuPosition = 0;
int menuMax      = 4;
unsigned long lastMenuAction = 0;
unsigned long menuTimeout = 20000; // time out of menu after 20s with no input

// Preferences
unsigned long lastDisplayUpdate = 0;
unsigned long updateInterval = 4000; // how many ms between display updates

// for testing via AdaFruit examples - remove along with colorWipe() and rainbow()
long firstPixelHue = 0;

// Function Declarations
void colorWipe(uint32_t, int);
void getRTCTime(void);
void displayDigit(int, uint32_t, int[], int, bool);
void printArray(int[], int);
void clearPixels(int[], int);

void setup() {
  // Init NeoPixel strip
  strip.begin();             // INITIALIZE NeoPixel strip object (REQUIRED)
  for (unsigned int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, 0);
  }
  strip.show();              // Turn OFF all pixels ASAP
  strip.setBrightness(brightness);   // Set BRIGHTNESS (max = 255)

  // Init buttons
  // Not sure if the pinMode() calls are needed with ClickButton but it can't
  // hurt
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SET, INPUT_PULLUP);

#define DEBOUNCE 30
#define MULTI_CLICK 50
#define LONG_CLICK 1000

  setButton.debounceTime   = DEBOUNCE;      // Debounce timer in ms
  setButton.multiclickTime = MULTI_CLICK;   // Time limit for multi clicks
  setButton.longClickTime =
      LONG_CLICK;   // time until "held-down clicks" register

  upButton.debounceTime   = DEBOUNCE;      // Debounce timer in ms
  upButton.multiclickTime = MULTI_CLICK;   // Time limit for multi clicks
  upButton.longClickTime =
      LONG_CLICK;   // time until "held-down clicks" register

  downButton.debounceTime   = DEBOUNCE;      // Debounce timer in ms
  downButton.multiclickTime = MULTI_CLICK;   // Time limit for multi clicks
  downButton.longClickTime =
      LONG_CLICK;   // time until "held-down clicks" register

  // Initialize the RTC
  if (!rtc.begin()) {
    Serial.println(F("Couldn't find RTC"));
    while (1) {};
  }

  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power, setting time to default"));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  Serial.begin(9600);
  getRTCTime();

  randomSeed(analogRead(0));

  Serial.println(F("End setup()"));
}

void loop() {
  setButton.Update();
  upButton.Update();
  downButton.Update();

  // var to hold the last time we moved forward 1 second
  // static vars init once and keep value between calls
  static unsigned long lastTick = 0;

  if ((unsigned long)(millis() - lastRTCUpdate) > RTCInterval) {
    lastRTCUpdate = millis();
    lastTick = lastRTCUpdate;
    getRTCTime();
  }

  if ((unsigned long)(millis() - lastTick) >= 1000) {
    Serial.println(F("Updating seconds"));
    Serial.print(F("lastDisplayUpdate = "));
    Serial.print(lastDisplayUpdate);
    Serial.print(F(", millis() = "));
    Serial.print(millis());
    Serial.println();

    lastTick = millis();
    second++;

    if (second > 59) {
        second = 0;
        minute++;
    }
    if (minute > 59) {
        minute = 0;
        hour++;
    }

    // hour is kept as 24h internally, changed to 12h for display if militaryTime = false
    if (hour > 23) { hour = 0; }
  }

  // If we're in menuPosition == 0 (meaning we're in normal diplay mode) 
  // and the updateInterval has passed (meaning we should update the display)
  //
  // In other words this is what should run when we're not in a menu
  if ((menuPosition == 0) && ((((unsigned long)(millis() - lastDisplayUpdate)) > updateInterval) || lastDisplayUpdate == 0)) {
    lastDisplayUpdate = millis();
    if (Serial) {
      Serial.print(F("Updating display: "));
      Serial.print(hour);
      Serial.print(F(":"));
      Serial.println(minute);
    }

    // Hour is always tracked as 24h, updated to 12h for display
    int displayHour = hour;
    if (!militaryTime) {
      if (displayHour > 12) { displayHour -= 12; }
    }

    displayDigit((int)(displayHour / 10), hourTensColor, hourTensLEDs, hourTensMax, true);
    displayDigit(((int)(displayHour - ((int)(displayHour / 10) * 10))), hourOnesColor, hourOnesLEDs, hourOnesMax, true);
    displayDigit((int)(minute / 10), minuteTensColor, minuteTensLEDs, minuteTensMax, true);
    displayDigit(((int)(minute - ((int)(minute / 10) * 10))), minuteOnesColor, minuteOnesLEDs, minuteOnesMax, true);

    strip.show();
  }

  // Menu position 1 == set hours (tens and ones)
  if (menuPosition == 1) {
    if (millis() - lastMenuAction > menuTimeout) {
      menuPosition = menuMax;
    }

    if(upButton.clicks > 0) {
      hour++;
      // Hour is always tracked as 24h, updated to 12h for display
      if (hour > 23) { hour = 0; }
      blinkState = false;
      lastBlink = 0;
      lastMenuAction = millis();
    }
    if(downButton.clicks > 0) {
      hour--;
      // Hour is always tracked as 24h, updated to 12h for display
      if (hour < 0) { hour = 23; }
      blinkState = false;
      lastBlink = 0;
      lastMenuAction = millis();
    }

    if ((millis() - lastBlink) > blinkInterval) {
      lastBlink = millis();
      blinkState = !blinkState;

      displayDigit((int)(minute / 10), minuteTensColor, minuteTensLEDs, minuteTensMax, false);
      displayDigit(((int)(minute - ((int)(minute / 10) * 10))), minuteOnesColor, minuteOnesLEDs, minuteOnesMax, false);

      if (blinkState) {
        displayDigit((int)(hour / 10), hourTensColor, hourTensLEDs, hourTensMax, false);
        displayDigit(((int)(hour - ((int)(hour / 10) * 10))), hourOnesColor, hourOnesLEDs, hourOnesMax, false);
      } else {
        clearPixels(hourOnesLEDs, hourOnesMax);
        clearPixels(hourTensLEDs, hourTensMax);
      }

      strip.show();
    }
  }

  // Set minute - tens digit
  if (menuPosition == 2) {
    if (millis() - lastMenuAction > menuTimeout) {
      menuPosition = menuMax;
    }

    if(upButton.clicks > 0) {
      minute += 10;
      if (minute > 59) { minute -= 60; }
      blinkState = false;
      lastBlink = 0;
      lastMenuAction = millis();
    }
    if(downButton.clicks > 0) {
      minute -= 10;
      if (minute < 0) { minute += 60; }
      blinkState = false;
      lastBlink = 0;
      lastMenuAction = millis();
    }

    if ((millis() - lastBlink) > blinkInterval) {
      lastBlink = millis();
      blinkState = !blinkState;

      displayDigit((int)(hour / 10), hourTensColor, hourTensLEDs, hourTensMax, false);
      displayDigit(((int)(hour - ((int)(hour / 10) * 10))), hourOnesColor, hourOnesLEDs, hourOnesMax, false);
      displayDigit(((int)(minute - ((int)(minute / 10) * 10))), minuteOnesColor, minuteOnesLEDs, minuteOnesMax, false);

      if (blinkState) {
        displayDigit((int)(minute / 10), minuteTensColor, minuteTensLEDs, minuteTensMax, false);
      } else {
        clearPixels(minuteTensLEDs, minuteTensMax);
      }

      strip.show();
    }
  }

  // Set minute - ones digit
  if (menuPosition == 3) {
    if (millis() - lastMenuAction > menuTimeout) {
      menuPosition = menuMax;
    }
    if(upButton.clicks > 0) {
      minute += 1;
      if (minute % 10 == 0) { minute -= 10; }
      blinkState = false;
      lastBlink = 0;
      lastMenuAction = millis();
    }
    if(downButton.clicks > 0) {
      minute -= 1;
      if (minute % 10 == 9) { minute += 10; }
      blinkState = false;
      lastBlink = 0;
      lastMenuAction = millis();
    }

    if ((millis() - lastBlink) > blinkInterval) {
      lastBlink = millis();
      blinkState = !blinkState;

      displayDigit((int)(hour / 10), hourTensColor, hourTensLEDs, hourTensMax, false);
      displayDigit(((int)(hour - ((int)(hour / 10) * 10))), hourOnesColor, hourOnesLEDs, hourOnesMax, false);
      displayDigit((int)(minute / 10), minuteTensColor, minuteTensLEDs, minuteTensMax, false);

      if (blinkState) {
        displayDigit((int)(minute % 10), minuteOnesColor, minuteOnesLEDs, minuteOnesMax, false);
      } else {
        clearPixels(minuteOnesLEDs, minuteOnesMax);
      }

      strip.show();
    }
  }

  if (menuPosition == menuMax) {
    // Save time to RTC
    menuPosition = 0;
  }

  if (setButton.clicks > 0)   // short click
  {
    if (menuPosition > 0) {
      menuPosition++;
      if (menuPosition > menuMax) {
        menuPosition = 0;
      }
      lastMenuAction = millis();
      Serial.print(F("Entering menu: "));
      Serial.println(menuPosition);
    } else {
      militaryTime = !militaryTime;
      lastDisplayUpdate -= updateInterval;
    }
  }
  if (setButton.clicks < 0)   // long click
  {
    // long press is only used for entering menu
    if (menuPosition == 0) {
      menuPosition++;
      lastMenuAction = millis();
      Serial.println(F("Entering Menu Mode"));
    }
  }

  if (upButton.clicks > 0) {
    if (menuPosition == 0) {
      brightness += brightnessStep;
      if (brightness > brightnessMax) { brightness = brightnessMin; }
      strip.setBrightness(brightness);
      strip.show();
      Serial.print(F("Brightness set to "));
      Serial.println(brightness);
    }
  }
}

void displayDigit(int digit, uint32_t color, int pixelList[], int max, bool randomize) {
  /*
  Serial.print(F("Displaying digit "));
  Serial.print(digit);
  Serial.print(F(" on LEDs: "));
  printArray(pixelList, max);
  Serial.println();
  */

  int digitOrder[max];
  for (int i = 0; i < max; i++) {
    digitOrder[i] = i;
  }

  if (randomize) {
    //Serial.println(F("Randomizing digit order"));
    for (int i = 0; i < max; i++) {
      int r = random(0, max);
      int t = digitOrder[r];

      digitOrder[r] = digitOrder[i];
      digitOrder[i] = t;
    }
    //Serial.print(F("New order: "));
    //printArray(digitOrder, max);
    //Serial.println();
  }

  clearPixels(pixelList, max);

  for (int i = 0; i < digit; i++) {
    strip.setPixelColor(pixelList[digitOrder[i]], color);
  }
}

void clearPixels(int arr[], int max) {
  for (int i = 0; i < max; i++){
    strip.setPixelColor(arr[i], 0);
  }
}

void printArray(int arr[], int max) {
  for (int i = 0; i < max; i++) {
    Serial.print(arr[i]);
    Serial.print(F(", "));
  }
}

// Fetch time from RTC into global vars
void getRTCTime() {
    DateTime now = rtc.now();

    hour   = now.hour();
    //if (hour > 12) { hour -= 12; }
    minute = now.minute();
    second = now.second();

    if (Serial) {
        Serial.print(F("Updating from RTC at "));
        Serial.println(millis());

        Serial.print(hour);
        Serial.print(F(":"));
        Serial.print(minute);
        Serial.print(F(":"));
        Serial.println(second);
    }
}
