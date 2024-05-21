#include "PinChangeInterrupt.h"  // NicoHood's library
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "display.h"

#define USE_BUTTONS 1  // Define if buttons are used
#define ENC_BUTTON_DEBOUNCE 50  // Debounce time for buttons
#define SCREEN_WIDTH 128  // OLED display width
#define SCREEN_HEIGHT 64  // OLED display height

const int NUM_ENCODERS = 5;  // Number of encoders
const int encoderPinsA[NUM_ENCODERS] = { 10, 8, 6, 4, 2 };  // Encoder A pins; any PCINT pin
const int encoderPinsB[NUM_ENCODERS] = { 11, 9, 7, 5, 3 };  // Encoder B pins; arbitrary - pick any IO

#ifdef USE_BUTTONS
const int encoderButtons[NUM_ENCODERS] = { 12, A0, A1, A2, A3 };  // Encoder buttons; can use analog pins, they will act as digital pins for this
#endif

int encoderValues[NUM_ENCODERS] = { 1023, 512, 512, 512, 512 }; // Initial values for encoders (0-1023)
int encoderMute[NUM_ENCODERS] = { 0, 0, 0, 0, 0 };  // Mute states for encoders; unused unless using buttons as mute
const int enc_inc = 1023 / 100;  // Step size each 'click' will increment/decrement; e.g., 1023/100 means roughly 100 'clicks' between 0% and 100% volume (moving 10 each click, between 0 and 1023)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);  // Initialize the display

bool displayNeedsUpdate[NUM_ENCODERS] = { true, true, true, true, true };  // Track if display needs update for each encoder
unsigned long lastUpdateTimes[NUM_ENCODERS] = { 0, 0, 0, 0, 0 };  // Track the last update time for each encoder
unsigned long screensaverStartTime = 0;  // Start time for screensaver
unsigned long sleepStartTime = 0;  // Start time for sleep mode
bool screensaverActive = false;  // Track if screensaver is active
bool sleepActive = false;  // Track if sleep mode is active

void TCA9548A(uint8_t bus) {
  Wire.beginTransmission(0x70);  
  Wire.write(1 << bus);
  Wire.endTransmission();
}

void setup() {
  for (int i = 0; i < NUM_ENCODERS; i++) {
    pinMode(encoderPinsA[i], INPUT);  // change to INPUT if using external pullup resistors
    pinMode(encoderPinsB[i], INPUT);  // change to INPUT if using external pullup resistors
#ifdef USE_BUTTONS
    pinMode(encoderButtons[i], INPUT_PULLUP); 
#endif
  }

  // A new set of ISRs will need to be added for each encoder, see bottom of sketch
  // Change FALLING to CHANGE if your encoders only update every two clicks
  attachPCINT(digitalPinToPCINT(encoderPinsA[0]), EN0_A_ISR, FALLING);
  attachPCINT(digitalPinToPCINT(encoderPinsA[1]), EN1_A_ISR, FALLING);
  attachPCINT(digitalPinToPCINT(encoderPinsA[2]), EN2_A_ISR, FALLING);
  attachPCINT(digitalPinToPCINT(encoderPinsA[3]), EN3_A_ISR, FALLING);
  attachPCINT(digitalPinToPCINT(encoderPinsA[4]), EN4_A_ISR, FALLING);

  Serial.begin(9600);  // Initialize serial communication
  Wire.begin();  // Initialize I2C communication

  for (int i = 2; i <= 6; i++) {
    TCA9548A(i);  // Select the I2C bus
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  // Initialize the display
      Serial.println(F("SSD1306 allocation failed"));  // Print error if display allocation fails
      for (;;);  // Infinite loop to halt the program
    }
    display.clearDisplay();  // Clear the display
  }
}

void loop() {
  static unsigned long lastDisplayUpdate = 0;  // Last time the display was updated
  unsigned long currentTime = millis();  // Current time in milliseconds

  if (currentTime - lastDisplayUpdate >= 100) {  // Check if 100 ms have passed since last update
    displayVol();  // Update the display volume
    lastDisplayUpdate = currentTime;  // Update the last display update time
  }

  sendEncoderValues();  // Actually send data (all the time)
  // printEncoderValues(); // For debug

#ifdef USE_BUTTONS
  for (int i = 0; i < NUM_ENCODERS; i++) {
    if (!digitalRead(encoderButtons[i])) {  // Check if encoder button is pressed
      encoderMute[i] ^= 1;  // ^= operator toggles between 1 and 0
      delay(ENC_BUTTON_DEBOUNCE);  // wait for encoder bounce (halts serial data, but presumably you aren't turning a knob while also holding mute)
      while (!digitalRead(encoderButtons[i]));  // after bounce, wait for button to be released
      delay(ENC_BUTTON_DEBOUNCE);  // wait for the release encoder bounce
      displayNeedsUpdate[i] = true;  // Mark display for update
      lastUpdateTimes[i] = millis();  // Reset update time
      screensaverActive = false;  // Reset screensaver state
      sleepActive = false;  // Reset sleep state
    }
  }
#endif

  bool anyActivity = false;  // Track if there is any encoder activity
  for (int i = 0; i < NUM_ENCODERS; i++) {
    if (millis() - lastUpdateTimes[i] < 10000) {  // Check if any encoder was updated in the last 10 seconds
      anyActivity = true;  // Mark activity detected
      break;  // Exit loop if any activity is found
    }
  }

  if (anyActivity) {
    screensaverStartTime = millis();  // Reset screensaver start time
    sleepActive = false;  // Deactivate sleep mode
    screensaverActive = false;  // Deactivate screensaver
  } else if (millis() - screensaverStartTime >= 10000 && !screensaverActive) {  // Check if 10 seconds have passed without activity and screensaver is not active
    showScreensaver();  // Activate screensaver
    screensaverActive = true;  // Mark screensaver as active
    sleepStartTime = millis();  // Reset sleep start time
  } else if (screensaverActive && millis() - sleepStartTime >= 180000 && !sleepActive) {  // Check if 3 minutes have passed with screensaver active and sleep mode is not active
    sleepDisplays();  // Activate sleep mode for displays
    sleepActive = true;  // Mark sleep mode as active
  }

  delay(10); 
}

void setEncoder(int enc) {
  if (digitalRead(encoderPinsB[enc])) {  // Check the state of encoder B pin
    encoderValues[enc] += enc_inc;  // Increment encoder value
  } else {
    encoderValues[enc] -= enc_inc;  // Decrement encoder value
  }
  encoderValues[enc] = constrain(encoderValues[enc], 0, 1023);  // Constrain encoder value to be within 0 and 1023
  displayNeedsUpdate[enc] = true;  // Mark display for update
  lastUpdateTimes[enc] = millis();  // Reset update time
}

void sendEncoderValues() {
  String builtString = String(""); 

  for (int i = 0; i < NUM_ENCODERS; i++) {
    builtString += String((int)(encoderValues[i] - (encoderValues[i] * encoderMute[i])));
    if (i < NUM_ENCODERS - 1) {
      builtString += String("|");
    }
  }

  Serial.println(builtString);
}

void displayVol() {
  for (int i = 0; i < NUM_ENCODERS; i++) {
    if (displayNeedsUpdate[i]) {  // Check if display needs update
      int volumeLevel;  // Variable to hold the volume level
      TCA9548A(i + 2);  // Select the I2C bus for the current encoder
      display.ssd1306_command(SSD1306_DISPLAYON);  // Turn on display
      display.clearDisplay();  // Clear the display

      switch (i) {
        case 0: display.drawBitmap(0, 0, master, 128, 64, WHITE); break;      // Draw bitmap for master volume
        case 1: display.drawBitmap(0, 0, games, 128, 64, WHITE); break;       // Draw bitmap for games volume
        case 2: display.drawBitmap(0, 0, discord, 128, 64, WHITE); break;     // Draw bitmap for discord volume
        case 3: display.drawBitmap(0, 0, spotify, 128, 64, WHITE); break;     // Draw bitmap for spotify volume
        case 4: display.drawBitmap(0, 0, app_window, 128, 64, WHITE); break;  // Draw bitmap for app window volume
      }

      if (!encoderMute[i]) {  // Check if encoder is not muted
        volumeLevel = map(encoderValues[i], 0, 1023, 0, 112);  // Map encoder value to volume level
        display.fillRect(8, 16, volumeLevel, 8, SSD1306_WHITE);  // Draw volume bar
        display.print(volumeLevel);  // Print volume level
      }
      display.display();  // Update the display
      displayNeedsUpdate[i] = false;  // Reset display update flag
    }
  }
}

void showScreensaver() {
  for (int i = 0; i < NUM_ENCODERS; i++) {
    TCA9548A(i + 2);  // Select the I2C bus for the current encoder
    display.clearDisplay();  // Clear the display
    switch (i) {
        case 0: display.drawBitmap(0, 0, master_big, 128, 64, WHITE); break;      // Draw bitmap for master screensaver
        case 1: display.drawBitmap(0, 0, games_big, 128, 64, WHITE); break;       // Draw bitmap for games screensaver
        case 2: display.drawBitmap(0, 0, discord_big, 128, 64, WHITE); break;     // Draw bitmap for discord screensaver
        case 3: display.drawBitmap(0, 0, spotify_big, 128, 64, WHITE); break;     // Draw bitmap for spotify screensaver
        case 4: display.drawBitmap(0, 0, app_window_big, 128, 64, WHITE); break;  // Draw bitmap for app window screensaver
      }
    display.display();  // Update the display
  }
}

void sleepDisplays() {
  for (int i = 0; i < NUM_ENCODERS; i++) {
    TCA9548A(i + 2);  // Select the I2C bus for the current encoder
    display.ssd1306_command(SSD1306_DISPLAYOFF);  // Turn off display
  }
}

void EN0_A_ISR() { setEncoder(0); }  // ISR for encoder 0
void EN1_A_ISR() { setEncoder(1); }  // ISR for encoder 1
void EN2_A_ISR() { setEncoder(2); }  // ISR for encoder 2
void EN3_A_ISR() { setEncoder(3); }  // ISR for encoder 3
void EN4_A_ISR() { setEncoder(4); }  // ISR for encoder 4

void printEncoderValues() {
  for (int i = 0; i < NUM_ENCODERS; i++) {
    String printedString = String("Encoder #") + String(i + 1) + String(": ") + String(encoderValues[i]) + String(" mV");  
    Serial.write(printedString.c_str()); 

    if (i < NUM_ENCODERS - 1) { Serial.write(" | "); }
    else { Serial.write("\n"); }
  }
}
