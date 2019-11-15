#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ClickButton.h>
#include <EEPROM.h>
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
 * - Press 'Set' or long press 'Up' to return
 *
 * New: Setting color pattern
 * - Hold 'Down' for 2 seconds (in original this did nothing)
 * - Press Down to cycle through color options
 * - Press 'Set' to save
 */

/*
 *
 * Set up attached hardware
 *
 */

/*
 * Initialize RTC
 */

// Using a ChronoDot from Adafruit - DS3231 based
// Besides power, connect SDA to A4, and SCL to A5
RTC_DS3231 rtc;

/*
 * Software version
 */

#define VER_MAJ 1
#define VER_MIN 0

/*
 * Intialize buttons
 */

#define BTN_UP 7
#define BTN_DOWN 8
#define BTN_SET 9

ClickButton setButton(BTN_SET, LOW, CLICKBTN_PULLUP);
ClickButton upButton(BTN_UP, LOW, CLICKBTN_PULLUP);
ClickButton downButton(BTN_DOWN, LOW, CLICKBTN_PULLUP);

/*
 * Intialize NeoPixels
 */

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

/*
 *
 * Global variables
 *
 */

/*
 * Define which pixels are for each digit
 */

// The LEDs are laid out to be compatible with using sections of LED strip:
//
// HourTens             HourOnes              MinuteTens             MinuteOnes
//     0      ---     1 --  2 --  3    ---      4 --  5    ---     6 --   7 -- 8
//                                                                             |
//     17     ---    16 -- 15 -- 14    ---     13 -- 12    ---    11 --  10 -- 9
//     |
//     18     ---    19 -- 20 -- 21    ---     22 -- 23    ---    24 --  25 -- 26

const static byte PROGMEM hourTensLEDs[3]   = { 0, 17, 18 };
const static byte PROGMEM hourTensMax       = 3;
const static byte PROGMEM hourOnesLEDs[9]   = { 1, 2, 3, 16, 15, 14, 19, 20, 21 };
const static byte PROGMEM hourOnesMax       = 9;
const static byte PROGMEM minuteTensLEDs[6] = { 4, 5, 13, 12, 22, 23 };
const static byte PROGMEM minuteTensMax     = 6;
const static byte PROGMEM minuteOnesLEDs[9] = { 6, 7, 8, 11, 10, 9, 24, 25, 26 };
const static byte PROGMEM minuteOnesMax     = 9;

const static byte PROGMEM logo_V_size         = 5;
const static byte PROGMEM logo_V[logo_V_size] = { 1, 3, 16, 14, 20 };

/*
 * Min, max brightness and interval between
 */

byte brightnessMax  = 250;
byte brightnessMin  = 50;
byte brightnessStep = 50;

/*
 * Internal time tracking (between updates from RTC)
 */

byte hour   = 0;
byte minute = 0;
byte second = 0;

/*
 * Tracking of event timing in internal loops
 */

unsigned long lastRTCUpdate     = 0;        // Last time we got an update from the RTC
unsigned long RTCInterval       = 120000;   // How often up to update from RTC (ms)
unsigned long blinkInterval     = 333;      // Blink timing in menus
unsigned long lastBlink         = 0;        // Last time menu blink changed on/off
bool          blinkState        = true;     // Is menu blink on or off
unsigned long lastDisplayUpdate = 0;        // Last time we updated the pixel display

/*
 * Define menu positions
 */

// 0 = Display Time
// 1 = Set Hours
// 2 = Set Minutes Tens
// 3 = Set Minutes Ones
// 4 = Save settings and resume clock
// 5 = Set update interval
// 6 = Save update interval
// 7 = Set color scheme
// 8 = Save color scheme
byte          menuPosition   = 0;
byte          menuSaveTime   = 4;   // Menu position where time gets saved to RTC
byte          menuSaveColor  = 8;
byte          menuMax        = 8;       // Max menu position
unsigned long lastMenuAction = 0;       // Time of last button press in menu
unsigned long menuTimeout    = 20000;   // Menu timeout (no input)

/*
 * Predefined colors
 */

const uint32_t clrRed      = strip.Color(0, 255, 0);
const uint32_t clrGreen    = strip.Color(255, 0, 0);
const uint32_t clrBlue     = strip.Color(0, 0, 255);
const uint32_t clrPurple   = strip.Color(0, 139, 139);
const uint32_t clrWhite    = strip.Color(255, 255, 255);
const uint32_t clrDimWhite = strip.Color(50, 50, 50);
const uint32_t clrYellow   = strip.Color(255, 255, 0);

/*
 * Update interval options
 */

const unsigned int updateIntervalFast   = 1000;    // 1 second
const unsigned int updateIntervalMedium = 4000;    // 4 seconds
const unsigned int updateIntervalSlow   = 60000;   // 60 seconds

/*
 * Default values for preferences
 *
 * Will be overwritten by EEPROM settings if found, otherwise
 * EEPROM is initialized with these values
 */

unsigned long updateInterval  = updateIntervalMedium;   // how many ms between display updates
uint32_t      hourTensColor   = clrRed;                 // Color of Hour Tens digit
uint32_t      hourOnesColor   = clrGreen;               // Color of Hour Ones digit
uint32_t      minuteTensColor = clrBlue;                // Color of Minutes Tens digit
uint32_t      minuteOnesColor = clrPurple;              // Color of Minutes Ones digit
byte          brightness      = brightnessMin;          // Brightness out of 255
byte          colorScheme     = 0;                      // Pre-set color schemes

/*
 * Vars for storing settings in EEPROM
 */

struct ConfigSettings {
  byte          flag;
  unsigned long updateInterval;
  uint32_t      hourTensColor;
  uint32_t      hourOnesColor;
  uint32_t      minuteTensColor;
  uint32_t      minuteOnesColor;
  byte          brightness;
  byte          colorScheme;
};
ConfigSettings settings;

/*
 * Function Declarations
 */
void getRTCTime(void);                            // Fetch time from RTC into global vars
void setRTCTime(void);                            // Update time in RTC from global vars
void printArray(byte[], byte);                    // Send an array to serial.print
void clearPixels(const byte[], byte);             // Turn off all pixels in an array
void clearPixels(const byte[], byte, uint32_t);   // Turn off all pixels in an array
void displayVersion(void);                        // Display version
void loadEEPROM(void);                            // Load saved values from EEPROM to RAM
void displayDigit(byte, uint32_t, uint32_t, const byte[], byte,
                  bool);     // Send a digit to the display
void setColorScheme(void);   // Choose a pre-set color scheme

void setup() {
  Serial.begin(115200);

  /*
   * Fetch settings from EEPROM
   */

  loadEEPROM();

  /*
   * Init NeoPixel strip
   */
  strip.begin();   // INITIALIZE NeoPixel strip object (REQUIRED)
  // Set all pixels to off
  strip.clear();
  strip.setBrightness(brightness);   // Set BRIGHTNESS (max = 255)
  strip.show();                      // Commit the change

  /*
   * Init buttons
   */

  // Not sure if the pinMode() calls are needed with ClickButton
  // but it can't hurt
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SET, INPUT_PULLUP);

  const int DEBOUNCE    = 30;     // ms to wait for debouncing
  const int MULTI_CLICK = 50;     // how long must pass between multiple clicks
  const int LONG_CLICK  = 1000;   // length of a long press

  setButton.debounceTime   = DEBOUNCE;
  setButton.multiclickTime = MULTI_CLICK;
  setButton.longClickTime  = LONG_CLICK;

  upButton.debounceTime   = DEBOUNCE;
  upButton.multiclickTime = MULTI_CLICK;
  upButton.longClickTime  = LONG_CLICK;

  downButton.debounceTime   = DEBOUNCE;
  downButton.multiclickTime = MULTI_CLICK;
  downButton.longClickTime  = LONG_CLICK;

  /*
   * Initialize the RTC
   */

  if (!rtc.begin()) {
    Serial.println(F("Couldn't find RTC"));
    strip.fill(clrRed);
    strip.show();
    while (1) {};
  }

  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power, setting time to default"));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  getRTCTime();

  /*
   * Initialize the RNG with input from a disconnected pin
   *
   * Hopefully that introduces some actual randomness
   */
  randomSeed(analogRead(0));

  displayVersion();

  Serial.println(F("End setup()"));
}

void loop() {
  // var to hold the last time we moved forward 1 second
  // static vars init once and keep value between calls
  static unsigned long lastTick = 0;

  // Check for any button presses that have been queued
  setButton.Update();
  upButton.Update();
  downButton.Update();

  if (menuPosition == 0) {
    // Update time from RTC
    if ((unsigned long)(millis() - lastRTCUpdate) > RTCInterval) {
      lastRTCUpdate = millis();
      lastTick      = lastRTCUpdate;
      getRTCTime();
    }

    // Update our stored time vars once every second
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

      // hour is kept as 24h internally, changed to 12h for display
      if (hour > 23) { hour = 0; }
    }
  }

  /* If we're in menuPosition == 0 (meaning we're in normal diplay mode)
   * and the updateInterval has passed (meaning we should update the display)
   *
   * In other words this is what should run when we're not in a menu
   */
  if ((menuPosition == 0) && ((((unsigned long)(millis() - lastDisplayUpdate)) > updateInterval) ||
                              lastDisplayUpdate == 0)) {
    lastDisplayUpdate = millis();
    if (Serial) {
      Serial.print(F("Updating display: "));
      Serial.print(hour);
      Serial.print(F(":"));
      Serial.println(minute);
    }

    // Hour is always tracked as 24h, updated to 12h for display
    byte displayHour = hour;
    if (displayHour > 12) { displayHour -= 12; }
    if (displayHour == 0) { displayHour = 12; }

    displayDigit((int)(displayHour / 10), hourTensColor, 0, hourTensLEDs, hourTensMax, true);
    displayDigit(((int)(displayHour - ((int)(displayHour / 10) * 10))), hourOnesColor, 0,
                 hourOnesLEDs, hourOnesMax, true);
    displayDigit((int)(minute / 10), minuteTensColor, 0, minuteTensLEDs, minuteTensMax, true);
    displayDigit(((int)(minute - ((int)(minute / 10) * 10))), minuteOnesColor, 0, minuteOnesLEDs,
                 minuteOnesMax, true);

    // Only run strip.show when needed, otherwise it wastes cycles
    strip.show();
  }

  /*
   * Menu handling
   *
   * Loop above doesn't run when we're in a menu - these run instead
   */

  // Menu position 1 == set hours (tens and ones)
  if (menuPosition == 1) {
    // Save time and exit if no button presses in menuTimeout ms
    if (millis() - lastMenuAction > menuTimeout) { menuPosition = menuSaveTime; }

    if (upButton.clicks > 0) {
      lastMenuAction = millis();

      hour++;
      if (hour > 23) { hour = 0; }
      second = 0;   // Keeps time from updating on us while we're trying to set it

      // Reset blink state on button press
      blinkState = false;
      lastBlink  = 0;
    }
    if (downButton.clicks > 0) {
      lastMenuAction = millis();

      if (hour == 0) {
        hour = 23;
      } else {
        hour--;
      }
      second = 0;   // Keeps time from updating on us while we're trying to set it

      // Reset blink state on button press
      blinkState = false;
      lastBlink  = 0;
    }

    // Every blinkInterval ms, update the display
    if ((millis() - lastBlink) > blinkInterval) {
      lastBlink  = millis();
      blinkState = !blinkState;

      strip.fill(clrDimWhite);

      // Minutes digits don't blink
      displayDigit((int)(minute / 10), minuteTensColor, clrDimWhite, minuteTensLEDs, minuteTensMax,
                   false);
      displayDigit(((int)(minute - ((int)(minute / 10) * 10))), minuteOnesColor, clrDimWhite,
                   minuteOnesLEDs, minuteOnesMax, false);

      if (blinkState) {
        byte displayHour = hour;
        if (displayHour > 12) { displayHour -= 12; }
        if (displayHour == 0) { displayHour = 12; }

        displayDigit((int)(displayHour / 10), hourTensColor, clrDimWhite, hourTensLEDs, hourTensMax,
                     false);
        displayDigit(((int)(displayHour - ((int)(displayHour / 10) * 10))), hourOnesColor,
                     clrDimWhite, hourOnesLEDs, hourOnesMax, false);
      } else {
        clearPixels(hourOnesLEDs, hourOnesMax);
        clearPixels(hourTensLEDs, hourTensMax);
      }

      strip.show();
    }
  }

  // Set minute - tens digit
  if (menuPosition == 2) {
    // Save time and exit if no button presses in menuTimeout ms
    if (millis() - lastMenuAction > menuTimeout) { menuPosition = menuSaveTime; }

    if (upButton.clicks > 0) {
      lastMenuAction = millis();

      minute += 10;
      if (minute > 59) { minute -= 60; }
      second = 0;   // Keeps time from updating on us while we're trying to set it

      blinkState = false;
      lastBlink  = 0;
      Serial.print(F("minute = "));
      Serial.println(minute);
    }
    if (downButton.clicks > 0) {
      lastMenuAction = millis();

      if (minute < 10) {
        minute += 50;
      } else {
        minute -= 10;
      }
      second = 0;   // Keeps time from updating on us while we're trying to set it

      blinkState = false;
      lastBlink  = 0;
      Serial.print(F("minute = "));
      Serial.println(minute);
    }

    if ((millis() - lastBlink) > blinkInterval) {
      lastBlink  = millis();
      blinkState = !blinkState;

      strip.fill(clrDimWhite);

      // Hours digits and minute ones don't blink
      byte displayHour = hour;
      if (displayHour > 12) { displayHour -= 12; }

      displayDigit((int)(displayHour / 10), hourTensColor, clrDimWhite, hourTensLEDs, hourTensMax,
                   false);
      displayDigit(((int)(displayHour - ((int)(displayHour / 10) * 10))), hourOnesColor,
                   clrDimWhite, hourOnesLEDs, hourOnesMax, false);
      displayDigit(((int)(minute - ((int)(minute / 10) * 10))), minuteOnesColor, clrDimWhite,
                   minuteOnesLEDs, minuteOnesMax, false);

      if (blinkState) {
        if ((int)(minute / 10) == 0) {
          displayDigit(minuteTensMax, clrDimWhite, clrDimWhite, minuteTensLEDs, minuteTensMax,
                       false);
        } else {
          displayDigit((int)(minute / 10), minuteTensColor, clrDimWhite, minuteTensLEDs,
                       minuteTensMax, false);
        }
      } else {
        clearPixels(minuteTensLEDs, minuteTensMax);
      }

      strip.show();
    }
  }

  // Set minute - ones digit
  if (menuPosition == 3) {
    // Save time and exit if no button presses in menuTimeout ms
    if (millis() - lastMenuAction > menuTimeout) { menuPosition = menuSaveTime; }

    if (upButton.clicks > 0) {
      lastMenuAction = millis();

      minute += 1;
      if (minute % 10 == 0) { minute -= 10; }
      second = 0;

      blinkState = false;
      lastBlink  = 0;
    }
    if (downButton.clicks > 0) {
      lastMenuAction = millis();

      if (minute == 0) {
        minute += 9;
      } else {
        minute -= 1;
        if (minute % 10 == 9) { minute += 10; }
      }
      second = 0;

      blinkState = false;
      lastBlink  = 0;
    }

    if ((millis() - lastBlink) > blinkInterval) {
      lastBlink  = millis();
      blinkState = !blinkState;

      strip.fill(clrDimWhite);

      // Hours digits and minute tens don't blink
      byte displayHour = hour;
      if (displayHour > 12) { displayHour -= 12; }

      displayDigit((int)(displayHour / 10), hourTensColor, clrDimWhite, hourTensLEDs, hourTensMax,
                   false);
      displayDigit(((int)(displayHour - ((int)(displayHour / 10) * 10))), hourOnesColor,
                   clrDimWhite, hourOnesLEDs, hourOnesMax, false);
      displayDigit((int)(minute / 10), minuteTensColor, clrDimWhite, minuteTensLEDs, minuteTensMax,
                   false);

      if (blinkState) {
        // Instead of being blank for 0, blink all white
        if ((int)(minute % 10) == 0) {
          displayDigit(minuteOnesMax, clrDimWhite, clrDimWhite, minuteOnesLEDs, minuteOnesMax,
                       false);
        } else {
          displayDigit((int)(minute % 10), minuteOnesColor, clrDimWhite, minuteOnesLEDs,
                       minuteOnesMax, false);
        }
      } else {
        clearPixels(minuteOnesLEDs, minuteOnesMax);
      }

      strip.show();
    }
  }

  // This menu position isn't interactive - just signals we're done setting time
  if (menuPosition == menuSaveTime) {
    // Save global time vars to RTC
    setRTCTime();

    // Reset menu to none
    menuPosition = 0;

    // Reset the display (needed b/c our randomization logic looks for blank pixels)
    strip.clear();
    // No strip.show needed here, the next update run will get it immediately

    // Make sure we update the display immediately
    lastDisplayUpdate = 0;
  }

  // Set display update interval
  if (menuPosition == 5) {
    if (millis() - lastMenuAction > menuTimeout) { menuPosition = menuSaveTime; }
    if (upButton.clicks > 0) {
      switch (updateInterval) {
        default:
        case updateIntervalFast:
          updateInterval = updateIntervalMedium;
          break;
        case updateIntervalMedium:
          updateInterval = updateIntervalSlow;
          break;
        case updateIntervalSlow:
          updateInterval = updateIntervalFast;
          break;
      }
      strip.show();
      lastBlink      = 0;
      lastMenuAction = millis();
    }

    if ((millis() - lastBlink) > blinkInterval) {
      // No actual blinking going on, but it's useful for not having to update
      // the strip hundreds of times per second
      lastBlink = millis();

      switch (updateInterval) {
        case updateIntervalFast:
          displayDigit(1, clrWhite, 0, hourTensLEDs, hourTensMax, false);
          break;
        case updateIntervalMedium:
          displayDigit(2, clrWhite, 0, hourTensLEDs, hourTensMax, false);
          break;
        case updateIntervalSlow:
          displayDigit(3, clrWhite, 0, hourTensLEDs, hourTensMax, false);
          break;
        default:
          clearPixels(hourTensLEDs, hourTensMax);
          break;
      }
      strip.show();
    }
  }

  // Another non-interactive menu position, this one saves the
  // display interval to EEPROM and then exits the menu
  if (menuPosition == 6) {
    Serial.print(F("Setting updateInterval = "));
    Serial.println(updateInterval);

    settings.updateInterval = updateInterval;
    EEPROM.put(0, settings);

    menuPosition      = 0;
    lastDisplayUpdate = 0;

    strip.clear();
    strip.show();
  }

  // Color scheme chooser
  if (menuPosition == 7) {
    if (millis() - lastMenuAction > menuTimeout) { menuPosition = menuSaveColor; }

    if (downButton.clicks > 0) {
      colorScheme++;
      setColorScheme();

      lastBlink      = 0;
      lastMenuAction = millis();
    }

    if ((millis() - lastBlink) > blinkInterval) {
      lastBlink  = millis();
      blinkState = true; // don't blink the color chooser

      if (blinkState) {
        displayDigit(hourTensMax, hourTensColor, 0, hourTensLEDs, hourTensMax, false);
        displayDigit(hourOnesMax, hourOnesColor, 0, hourOnesLEDs, hourOnesMax, false);
        displayDigit(minuteTensMax, minuteTensColor, 0, minuteTensLEDs, minuteTensMax, false);
        displayDigit(minuteOnesMax, minuteOnesColor, 0, minuteOnesLEDs, minuteOnesMax, false);
      } else {
        strip.clear();
      }

      strip.show();
    }
  }

  // Save color scheme - non-interactive
  if (menuPosition == menuSaveColor) {
    Serial.print(F("Setting colorScheme = "));
    Serial.println(colorScheme);

    settings.colorScheme = colorScheme;
    EEPROM.put(0, settings);

    menuPosition      = 0;
    lastDisplayUpdate = 0;

    strip.clear();
    strip.show();
  }

  /*
   * Handle any button presses
   */

  // Set button - short click
  if (setButton.clicks > 0) {
    // If we're in a menu, cycle to the next menu
    if (menuPosition > 0) {
      menuPosition++;
      if (menuPosition > menuMax) { menuPosition = 0; }
      lastMenuAction = millis();
      Serial.print(F("Entering menu: "));
      Serial.println(menuPosition);
    }
  }

  // Set button - long click
  if (setButton.clicks < 0) {
    // long press is only used for entering menu
    if (menuPosition == 0) {
      menuPosition++;
      lastMenuAction = millis();
      Serial.println(F("Entering Menu Mode"));
    }
  }

  // Up button - short click
  if (upButton.clicks > 0) {
    // Outside of menus, this cycles through brightness settings
    // (Inside menus it's handled by that code)
    if (menuPosition == 0) {
      brightness += brightnessStep;
      if ((brightness > brightnessMax) || (brightness < brightnessMin)) { brightness = brightnessMin; }

      strip.setBrightness(brightness);
      strip.show();   // Update brightness immediately

      settings.brightness = brightness;
      EEPROM.put(0, settings);

      Serial.print(F("Brightness set to "));
      Serial.println(brightness);
    }
  }

  // Up button - long click
  if (upButton.clicks < 0) {
    // Outside of menus, a long press here enters the update interval chooser
    if (menuPosition == 0) {
      // enter update interval setting menu
      clearPixels(hourTensLEDs, hourTensMax);
      clearPixels(hourOnesLEDs, hourOnesMax);
      clearPixels(minuteTensLEDs, minuteTensMax);
      clearPixels(minuteOnesLEDs, minuteOnesMax);
      lastMenuAction = millis();
      lastBlink      = millis() - blinkInterval;
      menuPosition   = 5;
    }
    // Inside the update interval chooser, a long press saves and exits
    else if (menuPosition == 5) {
      menuPosition++;
    }
  }

  // Down button - long click
  // Enter color scheme chooser
  if (downButton.clicks < 0) {
    if (menuPosition == 0) {
      clearPixels(hourTensLEDs, hourTensMax);
      clearPixels(hourOnesLEDs, hourOnesMax);
      clearPixels(minuteTensLEDs, minuteTensMax);
      clearPixels(minuteOnesLEDs, minuteOnesMax);
      lastMenuAction = millis();
      lastBlink      = millis() - blinkInterval;
      menuPosition   = 7;
    }
  }
}

/*
 * Update the digit display
 *
 * Takes as arguments:
 * digit - the number we're displaying from 0 to max-1 (based on number of pixels in this array)
 * color - NeoPixel RGB color value
 * pixelList - an array of the NeoPixel IDs making up this digit
 * max - the number of NeoPixels in this digit array
 * randomize - use a random order, or the pixelList array-defined order
 */

void displayDigit(byte digit, uint32_t color, uint32_t bgcolor, const byte pixelList[], byte max,
                  bool randomize) {
  // Create an array of digits 0 to max-1.
  byte digitOrder[max];
  for (byte i = 0; i < max; i++) { digitOrder[i] = i; }

  // Shuffle that array if we're randomizing the digits

  // If digit is '0' or 'max' we can't possible ensure a change since
  // they're all on or all off
  if (digit < max && digit > 0) {
    while (randomize) {
      for (byte i = 0; i < max; i++) {
        byte r = random(0, max);
        byte t = digitOrder[r];

        digitOrder[r] = digitOrder[i];
        digitOrder[i] = t;
      }

      // We don't want a TRUE random, which could result in no change between updates.
      // We want a fudged random, where (as long as we aren't displaying 0 or max) there
      // is always a difference from the last time we displayed.

      // Check each digit we're to light up
      // If any are not lit, we have a change
      for (byte i = 0; i < digit; i++) {
        if (strip.getPixelColor(pgm_read_byte(&pixelList[digitOrder[i]])) == 0) {
          randomize = false;
        }
      }
    }
  }

  // Clear this set of digits
  clearPixels(pixelList, max, bgcolor);

  // For however many digits we're setting, walk through the
  // (possibly) shuffled list of 0..max-1, and turn that pixel on
  for (byte i = 0; i < digit; i++) {
    strip.setPixelColor(pgm_read_byte(&pixelList[digitOrder[i]]), color);
  }
}

/*
 * Set all pixels in arr to off (black) or an optional passed color
 */

void clearPixels(const byte arr[], byte max) { clearPixels(arr, max, 0); }

void clearPixels(const byte arr[], byte max, uint32_t color) {
  for (byte i = 0; i < max; i++) { strip.setPixelColor(pgm_read_byte(&arr[i]), color); }
}

/*
 * Print the values in an array to the Serial output
 */

void printArray(byte arr[], byte max) {
  if (Serial) {
    for (byte i = 0; i < max; i++) {
      Serial.print(arr[i]);
      Serial.print(F(", "));
    }
  }
}

/*
 * Fetch time from RTC into global vars
 */
void getRTCTime() {
  DateTime now = rtc.now();

  hour = now.hour();
  // if (hour > 12) { hour -= 12; }
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

/*
 * Take time in global vars and store in RTC chip
 */

void setRTCTime() {
  // This clock isn't date-aware so it doesn't matter what date we set here
  rtc.adjust(DateTime(2014, 1, 1, hour, minute, 0));

  if (Serial) {
    Serial.print(F("Setting RTC to "));

    Serial.print(hour);
    Serial.print(F(":"));
    Serial.print(minute);
    Serial.println(F(":00"));
  }
}

void setColorScheme(void) {
  if (Serial) {
    Serial.print(F("setting color scheme "));
    Serial.println(colorScheme);
  }

  switch (colorScheme) {
    default:
      colorScheme = 0;
    case 0:   // My personal default
      hourTensColor   = clrRed;
      hourOnesColor   = clrGreen;
      minuteTensColor = clrBlue;
      minuteOnesColor = clrPurple;
      break;
    case 1:   // TIX II
      hourTensColor   = clrBlue;
      hourOnesColor   = clrYellow;
      minuteTensColor = clrPurple;
      minuteOnesColor = clrGreen;
      break;
    case 2:                                          // Green/yellow
      hourTensColor   = strip.Color(175, 13, 186);   // blue-green
      hourOnesColor   = clrGreen;
      minuteTensColor = strip.Color(255, 154, 50);
      minuteOnesColor = clrYellow;
      break;
    case 3:   // Red/orange
      hourTensColor   = clrRed;
      hourOnesColor   = strip.Color(69, 255, 0);
      minuteTensColor = strip.Color(140, 255, 0);
      minuteOnesColor = clrYellow;   // strip.Color(174, 255, 66);
      break;
    case 4:   // Purple/blue
      hourTensColor   = strip.Color(13, 129, 112);
      hourOnesColor   = strip.Color(29, 73, 118); //clrPurple; // strip.Color(0, 139, 139);
      minuteTensColor = strip.Color(46, 23, 124);
      minuteOnesColor = strip.Color(175, 13, 186); //clrBlue;
      break;
    case 5:   // Christmas
      hourTensColor   = clrRed;
      hourOnesColor   = clrGreen;
      minuteTensColor = clrRed;
      minuteOnesColor = clrGreen;
      break;
    case 6:   // Hanukkah
      hourTensColor   = clrWhite;
      hourOnesColor   = clrBlue;
      minuteTensColor = clrWhite;
      minuteOnesColor = clrBlue;
      break;
  }
}

void loadEEPROM(void) {
  EEPROM.get(0, settings);

  // flag is used to determine if we've saved valid data before.
  // if we read any value other than this, assume this is our first
  // time running on this chip, or the data is corrupt.
  //
  // update flag by adding 1 if changing struct, so we don't load bad data.
  const byte flag = B10110011;

  if (settings.flag != flag) {
    Serial.print(F("EEPROM flag invalid! Expected "));
    Serial.print(flag, BIN);
    Serial.print(F(", got "));
    Serial.println(settings.flag, BIN);
    Serial.println(F("Saving default config data"));

    settings.flag            = flag;
    settings.updateInterval  = updateInterval;
    settings.hourTensColor   = hourTensColor;
    settings.hourOnesColor   = hourOnesColor;
    settings.minuteTensColor = minuteTensColor;
    settings.minuteOnesColor = minuteOnesColor;
    settings.brightness      = brightness;
    settings.colorScheme     = colorScheme;

    EEPROM.put(0, settings);
  } else {
    updateInterval = settings.updateInterval;
    brightness     = settings.brightness;
    if (brightness > brightnessMax || brightness < brightnessMin) { brightness = brightnessMin; }
    strip.setBrightness(brightness);

    colorScheme = settings.colorScheme;
    setColorScheme();

    Serial.println(F("Loaded settings from EEPROM:"));
    Serial.print(F("- updateInterval = "));
    Serial.println(updateInterval);
    Serial.print(F("- brightness = "));
    Serial.println(brightness);
    Serial.print(F("- colorScheme = "));
    Serial.print(colorScheme);
    Serial.println();
  }
}

void displayVersion(void) {
  strip.clear();

  for (byte i = 0; i < pgm_read_byte(&logo_V_size); i++) {
    strip.setPixelColor(pgm_read_byte(&logo_V[i]), hourOnesColor);
  }
  displayDigit(VER_MAJ, minuteTensColor, 0, minuteTensLEDs, minuteTensMax, false);
  displayDigit(VER_MIN, minuteOnesColor, 0, minuteOnesLEDs, minuteOnesMax, false);

  strip.show();
  delay(3000);

  strip.clear();
  strip.show();
  delay(500);
}