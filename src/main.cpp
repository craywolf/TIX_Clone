#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ClickButton.h>
#include <RTClib.h>

/*
 * Official TIX menu functions:
 *
 * Press 'Mode' to set time
 * - Left 2 indicators flash. Use up/down to set from 1 to 12.
 * - Press again, tens-of-minutes flash. Up/down to set from 1-5.
 * - Press again, ones-of-minutes flash. Up/down to set from 1-9.
 * - Press again, all digits flash, then resume clock mode.
 *
 * Changing brightness:
 * - In normal mode, press 'Up' to cycle through brightness levels.
 *
 * Setting update rate:
 * - Hold 'Mode' for 2 seconds. All indicators go out except one of
 *   the left-most LEDs. Press 'Up' to cycle through rates.
 * - Top LED lit = 1s
 * - Middle LED  = 4s (default)
 * - Bottom LED  = 1m
 * - Press 'Mode' to return
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
uint32_t minuteOnesColor = strip.Color(0, 139, 139);   // Purple

int hour   = 0;
int minute = 0;
int second = 0;

// Menu:
// 0 = Display Time
// 1 = Set Hours
// 2 = Set Minutes Tens
// 3 = Set Minutes Ones
// 4 = 12/24 Hour Time
int menuPosition = 0;
int menuMax      = 2;

// Preferences
int updateInterval = 4; // how many seconds between display updates

// for testing via AdaFruit examples - remove along with colorWipe() and rainbow()
long firstPixelHue = 0;

// Function Declarations
void colorWipe(uint32_t, int);
void rainbow(int[], int);
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
  strip.setBrightness(50);   // Set BRIGHTNESS (max = 255)

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
}

void loop() {
  setButton.Update();
  upButton.Update();
  downButton.Update();

  static int curNum = 1;

  if (setButton.clicks > 0)   // short click
  {
    Serial.println(F("BTN_SET (short)"));
    displayDigit(curNum, minuteOnesColor, hourOnesLEDs, hourOnesMax, true);
    curNum++;
    if (curNum > hourOnesMax) { curNum = 1; }
  }
  if (setButton.clicks < 0)   // long click
  {
    Serial.println(F("BTN_SET (long)"));
  }

  static long lastChange = 0;
  if (millis() - lastChange > 25) {
    lastChange = millis();

    rainbow(minuteOnesLEDs, minuteOnesMax);
    // rainbow(hourOnesLEDs, hourOnesMax);
    strip.show();

    firstPixelHue += 256;
  }
}

void displayDigit(int digit, uint32_t color, int pixelList[], int max, bool randomize) {
  Serial.print(F("Displaying digit "));
  Serial.print(digit);
  Serial.print(F(" on LEDs: "));
  printArray(pixelList, max);
  Serial.println();

  int digitOrder[max];
  for (int i = 0; i < max; i++) {
    digitOrder[i] = i;
  }

  if (randomize) {
    Serial.println(F("Randomizing digit order"));
    for (int i = 0; i < max; i++) {
      int r = random(0, max);
      int t = digitOrder[r];

      digitOrder[r] = digitOrder[i];
      digitOrder[i] = t;
    }
    Serial.print(F("New order: "));
    printArray(digitOrder, max);
    Serial.println();
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

void rainbow(int pixelList[], int max) {
  for (int i = 0; i < max; i++) {
    int pixelHue = firstPixelHue + (i * 65536L / max);
    strip.setPixelColor(pixelList[i], strip.gamma32(strip.ColorHSV(pixelHue)));

    // Serial.print(pixelList[i]);
    // Serial.print(" = ");
    // Serial.println(pixelHue);
  }
}

// Fill strip pixels one after another with a color. Strip is NOT cleared
// first; anything there will be covered pixel by pixel. Pass in color
// (as a single 'packed' 32-bit value, which you can get by calling
// strip.Color(red, green, blue) as shown in the loop() function above),
// and a delay time (in milliseconds) between pixels.
//
// Fill along the length of the strip in various colors...
// colorWipe(strip.Color(255,   0,   0), 50); // Red
// colorWipe(strip.Color(  0, 255,   0), 50); // Green
// colorWipe(strip.Color(  0,   0, 255), 50); // Blue

void colorWipe(uint32_t color, int wait) {
  for (unsigned int i = 0; i < strip.numPixels();
       i++) {                        // For each pixel in strip...
    strip.setPixelColor(i, color);   //  Set pixel's color (in RAM)
    strip.show();                    //  Update strip to match
    delay(wait);                     //  Pause for a moment
  }
}

// Fetch time from RTC into global vars
void getRTCTime() {
    DateTime now = rtc.now();

    hour   = now.hour();
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
