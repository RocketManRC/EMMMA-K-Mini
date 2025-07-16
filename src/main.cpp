// Started on EMMMA-K-Mini-v2.0 on May 2, 2025. The goal is to refactor the code to make
// it more understandable and to move key parts of the source to libraries
// to assist in reuse in the future...

// This code will not run properly on an R8 (or greater) MCU as pins 35 to 37
// are used for the rotary encoder. That is only of course if PSRAM is being
// used which it isn't in this application at the moment anyway

/* 

This is the source code for the EMMMA-K-v3.2 Master processor (ESP32-S3).

Copyright 2023 RocketManRC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

If you use this code for any purpose please make sure that you
are not infringing on any third party's intellectual property rights.

The following opensource libraries are used by this software and are much appreciated:

Adafruit BusIO - MIT License
Adafruit GFX Library - BSD License
Adafruit NeoPixel - GLPL License
Adafruit SH110X - BSD License
Adafruit SPI Flash - MIT License
Adafruit TinyUSB Library - MIT License
ArduinoBLE - GLPL License
ArduinoJson - MIT License
BLE-MIDI - MIT License
I2Cdevlib - MIT License
MIDI Library - MIT License
NimBLE-Arduino - Apache License
SdFat - Adafruit Fork - MIT License

To support USB MIDI the following library is required
    MIDI Library by Forty Seven Effects
    https://github.com/FortySevenEffects/arduino_midi_library
  
 There is a bug in thislibrary for MIDI pitch bend where it will only bend up. Need to 
 change line 343 in MIDI.hpp to:

 const int value = int(fabs(inPitchValue) * double(scale));

 The file is in $PROJECT_DIR/.pio/ESP32-S3-DevKitC/MIDI Library/src

 A patch to fix this should be applied automatically via patchfile.py
 which is executed by PlatformIO prior to building.

*/

#include <Arduino.h>
#include <driver/touch_pad.h>
#include <driver/touch_sensor.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>

extern void setupEncoder();
extern void handleEncoder();
extern void displayRefresh();
extern void displaySetup();
extern void configSetup();
extern void buttonTick();
extern bool chordSupported();
extern void displayChords(bool init);
extern void displayNotes(bool init);
extern void changeChordsOn(int value);
extern void displayBinding();

extern String configInit; // if this is changed config will be initialized (default 3.14159265359)
extern bool useBluetooth;
extern int scaleIndex; // default is minor pentatonic
extern uint8_t midiChannel;
extern uint8_t masterVolume; 
extern bool adjacentPinsFilter;
extern bool dissonantNotesFilter;
extern uint8_t ccForModwheel;

extern String mode;
extern bool enableDissonantNotes;
extern bool enableAdjacentPins;
extern String scales[];
extern int key;
extern int octave;
extern bool playChords;

extern const int totalNotePins = 13; // 13 note pins on the keyboard itself 
extern const int totalNotes = 20;    // 13 notes plus room for chord notes above the last one including inversions

extern int wirelessSend(uint8_t *incomingData, int len, bool useBluetooth);
extern void initWireless(bool useBluetooth);
extern bool midiRead(uint8_t *type, uint8_t *data1, uint8_t *data2);

bool notePinsOn[totalNotePins] = {false};
uint8_t midiValues[totalNotes];
bool chordOn = false; // set when a chord has been played so the next pin press will be only a single note

// forward references
void handleChangeRequest(uint8_t type, uint8_t data1, uint8_t data2);
void MPU6050Setup();
void MPU6050Loop();

float ypr[3];  // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

// ************ The following define should normally be 1 for USB MIDI. Also must set the compile flag in platformio.ini ************
#define USEMIDI 0 // set to 0 to force remote via wireless (ESP-Now or BLE)

#define SERIALSLAVE Serial1

bool binding = false; // this will be set to true if middle touch pin pressed during startup

// I wanted to use the RGB LED to show note colours using Scriabin's Colour Sequence 
// but the Neopixel update is too slow and it adds random latency into the note generation.
// For now I just use the LED for status and error indications:
// GREEN - USB MIDI has successfully started.
// RED - ESP-Now fails initialization or packet failed to be delivered.
// BLUE - Using ESP-Now.

uint32_t espNowMicrosAtSend = 0;
uint32_t espNowReturnTime = 0;  // this will hold the return time of Esp_now in microseconds
uint8_t espNowDeliveryStatus = 0xFF;

bool bluetoothConnected = false; // will be set when bluetooth connected

#define RGBLED 0 // set to 1 to show note colours 

//Adafruit_NeoPixel pixels(1, 48, NEO_GRB + NEO_KHZ800); // ESP32-S3 DevKitC
Adafruit_NeoPixel pixels(1, 48, NEO_RGB + NEO_KHZ800); // ESP32-S3 DevKitC

const int MPU_addr = 0x68; // for the 6050

uint32_t scriabinColourSequence[12] = {0xFF0000, 0xCD9AFF, 0xFFFF00, 0x656599, 0xE3FBFF, 0xAC1C02, 0x00CCFF,
  0xFF6501, 0xFF00FF, 0x33CC33, 0x8C8A8C, 0x0000FE};

void handleNoteOn(byte channel, byte pitch, byte velocity);
void handleNoteOff(byte channel, byte pitch, byte velocity);

const uint8_t numPins = 14;  // The MCU has 14 touch pins

const uint8_t notePins = 13;  // single MCU that has 13 note pins, 14th pin is for pitchbend (index notePins)

static const touch_pad_t pins[numPins] = 
{
    TOUCH_PAD_NUM3, // Centre pin
    TOUCH_PAD_NUM9, // first left pin
    TOUCH_PAD_NUM10,
    TOUCH_PAD_NUM11,
    TOUCH_PAD_NUM12,
    TOUCH_PAD_NUM14,
    TOUCH_PAD_NUM13, // last left pin
    TOUCH_PAD_NUM8,  // first right pin
    TOUCH_PAD_NUM7,
    TOUCH_PAD_NUM6,
    TOUCH_PAD_NUM5,
    TOUCH_PAD_NUM4,
    TOUCH_PAD_NUM1,  // last right pin
    TOUCH_PAD_NUM2   // option1 pin (pitch bend)
};

static uint32_t benchmark[numPins]; // to store the initial touch values of the pins

bool option1 = false; // right top (on PCB) option pin (pitchbend)
//bool option2 = false; // right middle (on PCB) option pin (mode value up)
//bool option3 = false; // right bottom (on PCB) option pin (mode value down)
//bool option4 = false; // left top (on PCB) option pin (3 functions: change relative scale, enable/disable chords, modwheel)
//bool option5 = false; // left middle (on PCB) option pin (not used)
//bool option6 = false; // left bottom (on PCB) option pin (2 functions: change mode and change config)

//bool notePlayedWhileOption4Touched = false;

//String config = "Adjacent Key Filt";
void displayScale();
void displayKey();
void displayOctave();
void displayMasterVolume();
void (*optionDisplayFunctions[])() = {displayScale, displayKey, displayOctave, displayMasterVolume};
bool toggleRelativeMajorMinor();
bool allNotesOff();

void pitchBend(double bendX)
{
  static bool bendActive = false;

  if(option1)
  {
    uint8_t *p =  (uint8_t *)&bendX; 
    uint8_t msgPitchbend[9];

    for(int i = 0; i < 8; i++)
    {
      msgPitchbend[i] = p[i];
    }

    msgPitchbend[8] = midiChannel;  // MIDI channel

    espNowMicrosAtSend = micros();
    esp_err_t outcome = wirelessSend((uint8_t *) &msgPitchbend, sizeof(msgPitchbend), useBluetooth);  
    bendActive = true;
  }
  else if(bendActive)
  {
    //if(allNotesOff())
    //{
      // Send pitchbend of 0 once when option1 removed but wait for all notes off - I didn't like this!
      double zero = 0.0;
      uint8_t *p =  (uint8_t *)&zero; 
      uint8_t msgPitchbend[9];

      for(int i = 0; i < 8; i++)
        msgPitchbend[i] = p[i];
        
      msgPitchbend[8] = midiChannel;  // MIDI channel

      esp_err_t outcome = wirelessSend((uint8_t *) &msgPitchbend, sizeof(msgPitchbend), useBluetooth);  
      bendActive = false;
    //}
  }
}

void modwheel(uint8_t modX)
{
  static bool modActive = false;

  uint8_t msgModwheel[3];

  if(option1)
  {
    uint8_t mod = modX;

    msgModwheel[0] = ccForModwheel;
    msgModwheel[1] = mod;
    msgModwheel[2] = midiChannel;  // MIDI channel

    espNowMicrosAtSend = micros();
    esp_err_t outcome = wirelessSend((uint8_t *) &msgModwheel, sizeof(msgModwheel), useBluetooth);  
    modActive = true;
  }
  else if(modActive)
  {
    // Send modwheel of 0 once when option1 removed
      
    msgModwheel[0] = ccForModwheel;

    msgModwheel[1] = 0;

    msgModwheel[2] = midiChannel;  // MIDI channel

    esp_err_t outcome = wirelessSend((uint8_t *) &msgModwheel, sizeof(msgModwheel), useBluetooth);  
    modActive = false;
  }
}

void sendCC(uint8_t cc, uint8_t value)
{
  uint8_t data[] = {cc, value, midiChannel};

  wirelessSend(data, sizeof(data), useBluetooth);
}

void setVolume(uint8_t data1)
{
  masterVolume = data1;
}

void setPixel(int index, uint32_t colour)
{
  pixels.setPixelColor(index, colour); 

  pixels.show(); 
}


void setup() 
{
  Serial.begin(115200);

  displaySetup();

  configSetup();

  // need to do this to force the scale to be loaded in case it isn't major scale...
  handleChangeRequest(176, 68, scaleIndex + 1); // it's +1 here because handleChangeRequest is going to decrement it

  //SERIALSLAVE.begin(2000000);

  Serial.println("Init encoder"); 

  setupEncoder();

  Serial.println("finished init");
 
  touch_pad_init();

  MPU6050Setup();

  // Try to prevent the touch sensor processor from shutting down from too much activity which happened at MFR 2023!
  // Divide the measurement times by 2 (from default of 500 to 250).
  // This gives calibration values more like the ESP32-S2 which was in the slave processor and had no problems...
  uint16_t sleep_cycle;
  uint16_t meas_times;
  touch_pad_get_meas_time(&sleep_cycle, &meas_times);
  Serial.printf("touch pad sleep_cycle: %d, meas_times: %d\n", sleep_cycle, meas_times);
  touch_pad_set_meas_time(sleep_cycle, meas_times / 2);

  touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);

  touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);

  touch_pad_fsm_start();

  for(int i = 0; i < numPins; i++) 
  {
      touch_pad_config(pins[i]);
  }

  delay(2000); // This is critical for proper initializaion!

  uint32_t touch_value;
  for (int i = 0; i < numPins; i++) 
  {
      //read benchmark value
      touch_pad_read_benchmark(pins[i], &touch_value);
      Serial.print(touch_value);
      Serial.print(" ");
      benchmark[i] = touch_value;

      if(/* i == 0 && */ touch_value > 30000) // is any note pin touched during startup?
      {
        binding = true; // set flag to wait for hub to bind!
        Serial.println("Waiting for new hub to bind");
      }
  }
  Serial.println();

  pixels.setBrightness(10);
  pixels.begin(); // INITIALIZE NeoPixel (REQUIRED)

  pixels.setPixelColor(0, 0x00FF00); // init to green

  pixels.show();   

  initWireless(useBluetooth);

  if(binding)
  {
    displayBinding();

    while(true)
    {
      delay(100);  
      Serial.println("waiting for bind to hub...");
    }

  }

  mode = "Volume";
  displayMasterVolume();
}

// A note will be dissonant if there is a note on that is 1 or 2 semitones
// higher or lower. We calculate indices into the keyname array and then check
// if any notes satify this keeping in mind that it wraps around by 12.

bool dissonantNoteOn(uint8_t midiValueIndex)
{
  //Serial.print("Pin: ");
  //Serial.println(pin);

  uint8_t refIdx = (midiValues[midiValueIndex] % 60) % 12;
 
  bool result = false;

  for(int i = 0; i < totalNotePins; i++)
  {
    if(notePinsOn[i])
    {
      uint8_t idx = (midiValues[i] % 60) % 12;

      if(idx == 0)
      {
        result = refIdx == 1 || refIdx == 2 || refIdx == 11 || refIdx == 10;
      }
      else if(idx == 11)
      {
        result = refIdx == 0 || refIdx == 1 || refIdx == 10 || refIdx == 9;
      }
      else
      {
        result = refIdx == idx + 1  || refIdx == idx + 2|| refIdx == idx - 1 || refIdx == idx - 2;
      }
    }
  }

  if(enableDissonantNotes)
    return false;
  else
    return result;
}

// Check if there are any adjacent pins on (note that the end pins only have one adjacent pin)
// The master has 9 note pins which correspond to the even midiValues[]

bool adjacentPinOn(int pin)
{
  bool result = false;

  switch(pin)
  {
    case 0:
      result = notePinsOn[1] || notePinsOn[7]; 
      break;

    case 1:
      result = notePinsOn[0] || notePinsOn[2];
      break;

    case 2:
      result = notePinsOn[1] || notePinsOn[3];
      break;

    case 3:
      result = notePinsOn[2] || notePinsOn[4];
      break;

    case 4:
      result = notePinsOn[3] || notePinsOn[5];
      break;

    case 5:
      result = notePinsOn[4] || notePinsOn[6];
      break;

    case 6:
      result = notePinsOn[5];
      break;

    case 7:
      result = notePinsOn[0] || notePinsOn[8];
      break;

    case 8:
      result = notePinsOn[7] || notePinsOn[9];
      break;

    case 9:
      result = notePinsOn[8] || notePinsOn[10];
      break;

    case 10:
      result = notePinsOn[9] || notePinsOn[11];
      break;

    case 11:
      result = notePinsOn[10] || notePinsOn[12];
      break;

    case 12:
      result = notePinsOn[11];
      break;

    default:
      result = false;
      break;
  }

  //Serial.print(pin);
  //Serial.print(" ");
  //Serial.println(result);

  //Serial.print(enableAdjacentPins);

  if(enableAdjacentPins)
    return false;
  else
    return result;
}

void sendChordEspNow2(uint8_t *chordData)
{
  uint8_t mv;
  
  if(chordData[1])
    mv = masterVolume; // use masterVolume for noteon
  else
    mv = 0; // and zero for note off

  uint8_t msgNote[8] = {(uint8_t)(chordData[0]), (uint8_t)(chordData[1]), (uint8_t)mv, midiChannel, 
    (uint8_t)(chordData[2]), (uint8_t)(chordData[3]), (uint8_t)mv, 1}; 

  espNowMicrosAtSend = micros();
  esp_err_t outcome = wirelessSend((uint8_t *) &msgNote, sizeof(msgNote), useBluetooth);  

  if(outcome)
  {
    pixels.setPixelColor(0, 0x00FFFF); // set LED to yellow
    pixels.show(); 
  }
}

void sendChordEspNow3(uint8_t *chordData)
{
  uint8_t mv;
  
  if(chordData[1])
    mv = masterVolume; // use masterVolume for noteon
  else
    mv = 0; // and zero for note off

  uint8_t msgNote[12] = {(uint8_t)(chordData[0]), (uint8_t)(chordData[1]), (uint8_t)mv, midiChannel, 
    (uint8_t)(chordData[2]), (uint8_t)(chordData[3]), (uint8_t)mv, midiChannel,
    (uint8_t)(chordData[4]), (uint8_t)(chordData[5]), (uint8_t)mv, midiChannel}; 

  espNowMicrosAtSend = micros();
  esp_err_t outcome = wirelessSend((uint8_t *) &msgNote, sizeof(msgNote), useBluetooth);  

  if(outcome)
  {
    pixels.setPixelColor(0, 0x00FFFF); // set LED to yellow
    pixels.show(); 
  }
}

// Note that notes for chords are sent in reverse order in case the instrument can't handle chords (only the last note sent will play)
void sendChordOn(uint8_t idx, uint8_t ofs)
{
  if(scales[scaleIndex] == "Major" || scales[scaleIndex] == "Minor")
  {
    uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 4] + ofs), 1, (uint8_t)(midiValues[idx + 2] + ofs), 1,
      (uint8_t)(midiValues[idx] + ofs), 1};

    sendChordEspNow3(chordData3);   
  }
  else if(scales[scaleIndex] == "Major Pentatonic")
  {
    switch(idx % 5)
    {
      case 0:
        // Scale root note (I)
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 1, (uint8_t)(midiValues[idx + 2] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow3(chordData3);
        }
        break;
      case 1:
      case 3:
      case 4:
        // Scale second, fourth and fifth note (II, IV, V), 
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 1, (uint8_t)(midiValues[idx + 1] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow3(chordData3);
        }
        break;
      case 2:
        // Scale third note (III) - no triad chord for this one so just play the diad
        {
          uint8_t chordData2[4] = {(uint8_t)(midiValues[idx + 1] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow2(chordData2);
        }
        break;
    }
  }
  else if(scales[scaleIndex] == "Minor Pentatonic")
  {
    switch(idx % 5)
    {
      case 0:
      case 2:
      case 4:
        // Scale root, third and fifth note (I, III, V)
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 1, (uint8_t)(midiValues[idx + 1] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow3(chordData3);
        }
        break;
      case 1:
        // Scale fourth note (IV) 
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 1, (uint8_t)(midiValues[idx + 2] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow3(chordData3);
        }
        break;
      case 3:
        // Scale third note (III) - no triad for this one so just play the diad
        {
          uint8_t chordData2[4] = {(uint8_t)(midiValues[idx + 1] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow2(chordData2);
        }
        break;
    }
  }
  else if(scales[scaleIndex] == "Minor Blues")
  {
    switch(idx % 6)
    {
      case 0:
        // Scale root(I)
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 4] + ofs), 1, (uint8_t)(midiValues[idx + 1] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow3(chordData3);
        }
        break;
      case 1:
        // Scale second note (II) 
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 4] + ofs), 1, (uint8_t)(midiValues[idx + 3] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow3(chordData3);
        }
        break;
      case 2:
        // Scale third note (III)
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 4] + ofs), 1, (uint8_t)(midiValues[idx + 2] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow3(chordData3);
        }
        break;
      case 3:
        // Scale third fourth note (IV)
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 1, (uint8_t)(midiValues[idx + 2] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow3(chordData3);
        }
        break;
      case 4:
        // Scale fifth note (V) - no triad for this one so just play the diad
       {
          uint8_t chordData2[4] = {(uint8_t)(midiValues[idx + 1] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow2(chordData2);
        }
        break;
      case 5:
        // Scale sixth note (VI) 
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 1, (uint8_t)(midiValues[idx + 1] + ofs), 1,
            (uint8_t)(midiValues[idx] + ofs), 1};

          sendChordEspNow3(chordData3);
        }
        break;
    }
  }
  else
  {
    // No chords supported, send root note only
    uint8_t msgNote[] = {(uint8_t)(midiValues[idx] + ofs), midiChannel, (uint8_t)masterVolume, midiChannel}; 

    espNowMicrosAtSend = micros();
    esp_err_t outcome = wirelessSend((uint8_t *) &msgNote, sizeof(msgNote), useBluetooth);  

    if(outcome)
    {
      //Serial.println("Error sending slave noteOn");
      pixels.setPixelColor(0, 0x00FFFF); // set LED to yellow
      pixels.show(); 
    }
  }
}

void sendChordOff(uint8_t idx, uint8_t ofs)
{
  if(scales[scaleIndex] == "Major" || scales[scaleIndex] == "Minor")
  {
    uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 4] + ofs), 0, (uint8_t)(midiValues[idx + 2] + ofs), 0,
      (uint8_t)(midiValues[idx] + ofs), 0};

    sendChordEspNow3(chordData3);
  }
  else if(scales[scaleIndex] == "Major Pentatonic")
  {
    switch(idx % 5)
    {
      case 0:
        // Scale root note (I)
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 0, (uint8_t)(midiValues[idx + 2] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow3(chordData3);
        }
        break;
      case 1:
      case 3:
      case 4:
        // Scale second, fourth and fifth note (II, IV, V), 
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 0, (uint8_t)(midiValues[idx + 1] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow3(chordData3);
        }
        break;
      case 2:
        // Scale third note (III) - no triad for this one so just turn off the diad
       {
          uint8_t chordData2[4] = {(uint8_t)(midiValues[idx + 1] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow2(chordData2);
        }
        break;
    }
  }
  else if(scales[scaleIndex] == "Minor Pentatonic")
  {
    switch(idx % 5)
    {
      case 0:
      case 2:
      case 4:
        // Scale root, third and fifth note (I, III, V)
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 0, (uint8_t)(midiValues[idx + 1] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow3(chordData3);
        }
        break;
      case 1:
        // Scale fourth note (IV) 
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 0, (uint8_t)(midiValues[idx + 2] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow3(chordData3);
        }
        break;
      case 3:
        // Scale third note (III) - no triad for this one so just turnoff the diad
        {
          uint8_t chordData2[4] = {(uint8_t)(midiValues[idx + 1] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow2(chordData2);
        }
        break;
    }
  }
  else if(scales[scaleIndex] == "Minor Blues")
  {
    switch(idx % 6)
    {
      case 0:
        // Scale root(I)
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 4] + ofs), 0, (uint8_t)(midiValues[idx + 1] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow3(chordData3);
        }
        break;
      case 1:
        // Scale second note (II) 
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 4] + ofs), 0, (uint8_t)(midiValues[idx + 3] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow3(chordData3);
        }
        break;
      case 2:
        // Scale third note (III)
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 4] + ofs), 0, (uint8_t)(midiValues[idx + 2] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow3(chordData3);
        }
        break;
      case 3:
        // Scale fourth note (IV)
        {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 0, (uint8_t)(midiValues[idx + 2] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow3(chordData3);
        }
        break;
      case 4:
        // Scale fifth note (V) - no triad for this one so just play the diad
        {
          uint8_t chordData2[4] = {(uint8_t)(midiValues[idx + 1] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow2(chordData2);
        }
        break;
      case 5:
        // Scale sixth note (VI) 
       {
          uint8_t chordData3[6] = {(uint8_t)(midiValues[idx + 3] + ofs), 0, (uint8_t)(midiValues[idx + 1] + ofs), 0,
            (uint8_t)(midiValues[idx] + ofs), 0};

          sendChordEspNow3(chordData3);
        }
        break;
    }
  }
  else
  {
  // No chords supported, turn off root note only
    uint8_t msgNote[] = {(uint8_t)(midiValues[idx] + ofs), 0, (uint8_t)0, midiChannel}; 

    espNowMicrosAtSend = micros();
    esp_err_t outcome = wirelessSend((uint8_t *) &msgNote, sizeof(msgNote), useBluetooth);  

    if(outcome)
    {
      //Serial.println("Error sending slave noteOn");
      pixels.setPixelColor(0, 0x00FFFF); // set LED to yellow
      pixels.show(); 
    }
  }
}


bool allNotesOff()
{
  bool result = true;

  for(int i = 0; i < totalNotePins; i++)
  {
    if(notePinsOn[i])
      result = false;
  }

  return result;
}

void displayRefresh()
{
  if(mode == "Scale")
  {
    displayScale();
  }
  else if(mode == "Key")
  {
    displayKey();
  }
  else if(mode == "Octave")
  {
    displayOctave();
  }
}


void processPitchBend()
{
  static bool offsetCaptured = false;
  static double pitchOffset = 0.0;
  static double lastBendxWithExpo = 0.0;

  double pitch;

  pitch = ypr[2] * 180/M_PI; // This is actually pitch the way it is mounted

  pitch *= -1.0;

  if(option1 && !offsetCaptured)
  {
    offsetCaptured = true;
    pitchOffset = pitch;
  }
  else if(!option1)
  {
    offsetCaptured = false;
  }

  // calculate the desired 0 position 
  pitch -= pitchOffset; 

  double bendX = pitch / 30.0; // 30 deg for full pitch range   

  if(bendX > 1.0)
    bendX = 1.0;
  else if(bendX < -1.0)
    bendX = -1.0;

  const double MaxExpo = 5.0;  
  const double a1 = 30.0; // 30%           
  double a2 = a1/100.0 * MaxExpo; 
  double a3 = 1.0 / exp(a2);         
  double bendxWithExpo = bendX * exp(fabs(a2 * bendX)) * a3;     
  
  //if(bendxWithExpo != lastBendxWithExpo)
  if(true)
  {
    lastBendxWithExpo = bendxWithExpo;

    pitchBend(bendxWithExpo);  
  }
}

void processModwheel()
{
  static bool offsetCaptured = false;
  static double rollOffset = 0.0;
  static uint8_t lastMod = 63;

  double roll = ypr[1] * 180/M_PI;  // ...and roll

  if(option1 && !offsetCaptured)
  {
    offsetCaptured = true;
    rollOffset = roll;
  }
  else if(!option1)
  {
    offsetCaptured = false;
  }

  // calculate the desired 0 position 
  roll -= rollOffset; 

  int iRoll = roll / 25.0 * 64; 
  if(iRoll < -64)
    iRoll = -64;
  else if (iRoll > 63)
    iRoll = 63;

  uint8_t mod;
  if(iRoll < 0)
    mod = -iRoll * 2;
  else
    mod =  iRoll * 2;

  if(mod > 127)
    mod = 127;

  // only send the modwheel data if it has changed
  //if(mod != lastMod)
  if(true)
  {
    lastMod = mod;

    modwheel(mod);
  }
}

void playNote(int midiValue)
{
  uint8_t msgNote[] = {(uint8_t)(midiValue + key + octave * 12), 1, (uint8_t)masterVolume, midiChannel}; 

  espNowMicrosAtSend = micros();
  esp_err_t outcome = wirelessSend((uint8_t *) &msgNote, sizeof(msgNote), useBluetooth);  
  //Serial.print(touch_value - benchmark[i]);
  //Serial.print(" ");

  //Serial.print(idx);
  //Serial.print(" ");
  //Serial.println(msgNote[0]);
  
  if(outcome)
  {
    //Serial.println("Error sending master noteOn");
    pixels.setPixelColor(0, 0x00FFFF); // set LED to yellow
    pixels.show(); 
  }
}

void loop() 
{
  buttonTick();

#if 0 // set to 1 to see loop timing
  static uint32_t loopCounter = 0;
  static uint32_t microsMax = 0;
  static uint32_t microsMin = 100000;
  static uint32_t lastTotalMicros = micros();
  static uint32_t lastMicros = micros();

  uint32_t m = micros();

  uint32_t elapsedMicros = m - lastMicros;

  lastMicros = m;

  if(elapsedMicros > microsMax)
  {
    microsMax = elapsedMicros;
  }

  if(elapsedMicros < microsMin)
  {
    microsMin = elapsedMicros;
  }

  if(loopCounter++ >= 10000)
  {
    loopCounter = 0;

    elapsedMicros = m - lastTotalMicros;
    lastTotalMicros = m;

    Serial.println(elapsedMicros);
    Serial.println(microsMax);
    Serial.println(microsMin);

    microsMax = 0;
    microsMin = 100000;
  }
#endif
  handleEncoder();

  uint32_t touch_value;

  touch_pad_read_raw_data(pins[notePins], &touch_value);   // right top (on PCB) option pin
  if(touch_value > benchmark[notePins] + (0.3 * benchmark[notePins]))
  {
    option1 = true;
    //Serial.println("pitch bend on");
  }
  else if(touch_value < benchmark[notePins] + (0.2 * benchmark[notePins]))
    option1 = false;

  uint8_t idx;

  for(int i = 0; i < notePins; i++)
  {
    // The following is because ot the pin arrangement on the Mini...
    if(i == 0)
    {
      idx = 0;
    }
    else if(i < 7)
    {
      idx = ((i - 1) * 2) + 1;
    }
    else
    {
      idx = (i - 6) * 2;
    }

    uint8_t ofs = key + octave * 12;

    touch_pad_read_raw_data(pins[i], &touch_value);

    static uint8_t chordPin = 0;

    if(touch_value > benchmark[i] + (0.3 * benchmark[i]) && !adjacentPinOn(i) && !dissonantNoteOn(idx))
    {
      //Serial.println(touch_value - benchmark[i]);
      //Serial.print(" ");
      //Serial.println(pins[i]);
      //Serial.println(i);
      //Serial.println(adjacentPinOn(i));

      if(adjacentPinsFilter)
        enableAdjacentPins = false;
      else
        enableAdjacentPins = true;

      if(dissonantNotesFilter)
        enableDissonantNotes = false;
      else
        enableDissonantNotes = true;

      if(dissonantNoteOn(idx))
      {
        //Serial.println("Dissonant note is already on!");
      }

      if(allNotesOff())
      {
        chordOn = false; // can't be playing a chord if all notes are off
        chordPin = 0;
      }

      if(!notePinsOn[i])
      {
        notePinsOn[i] = true;
       // Serial.println(i);

        //if(option4)
        //{
        //  notePlayedWhileOption4Touched = true; 
        //}

        if(playChords)
        {
          if(!chordOn)
          {
            sendChordOn(idx, ofs);
            chordOn = true;
            chordPin = idx;
          }
          else
          {
            // If a chord is already playing just play a single note
            // but if this new note is lower we want to turn the previous
            // chord off, make the new note the chord and then turn the old
            // one on as a note
            if(idx < chordPin)
            { 
              // new note is loweer
              sendChordOff(chordPin, ofs);
              playNote(midiValues[chordPin]);
              sendChordOn(idx, ofs);
              chordPin = idx;
            }
            else
            {
              // new note is higher or the same
              playNote(midiValues[idx]);
            }
          }
        }
        else
        {
          playNote(midiValues[idx]);
        }

        if(playChords  && chordSupported())
          displayChords(false);
        else
          displayNotes(false);
      }
    }
    else if(touch_value < benchmark[i] + (0.2 * benchmark[i]))
    {
      if(notePinsOn[i])
      {
        notePinsOn[i] = false;

        if(playChords && chordPin == idx)
        {
          sendChordOff(idx, ofs); // chords are handled seperately
          //Serial.println("chord turned off");

          chordOn = false; // since only one chord can play now, show that no chord on so we can play again
        }
        else 
        {
          //uint8_t msgNote[] = {(uint8_t)(midiValues[i * 2] + key + octave * 12), 0, (uint8_t)0, midiChannel}; // this one or below?
          uint8_t msgNote[] = {(uint8_t)(midiValues[idx] + key + octave * 12), 0, (uint8_t)0, midiChannel}; 

          espNowMicrosAtSend = micros();
          esp_err_t outcome = wirelessSend((uint8_t *) &msgNote, sizeof(msgNote), useBluetooth);  

          if(outcome)
          {
            //Serial.println("Error sending master noteOff");
            pixels.setPixelColor(0, 0x00FFFF); // set LED to yellow
            pixels.show(); 
          }
        }
        
      if(playChords && chordSupported())
        displayChords(false);
      else
        displayNotes(false);        
      }
    }
  }
   
  float ax;
  float ay; 
  float az;

  static uint32_t t0 = millis();

  static uint32_t lastOption1millis = t0; // for the option pin "click"
  static uint32_t option1Timer = 0;
  static bool option1Touched = false;
  static bool option1TimerEnabled = false;


  // This is the pitchbend and modwheel stuff. Only do every 25ms
  if(millis() - t0 > 25)
  {
    t0 = millis();

    // The MPU6050 processing. Pitch is used for pitch bend and roll for modwheel

    MPU6050Loop();
#if 0
    messageUpdate(false); // for pop-up message timing
#endif
    processPitchBend();

    processModwheel();
  }

  // This whole option1 thing is a big mess and needs to be done in a different way.
  // See the evernote journal for March 27, 2025
  // UPDATE: cleaned this up on March 28, 2025 plus changed the way the the effects
  // are reset.
  if(!allNotesOff())
    option1TimerEnabled = false;

  if(option1TimerEnabled)
    option1Timer = millis() - lastOption1millis;
  else
    lastOption1millis = millis();

  if(option1)
  {
    if(!option1Touched)
    {
      // Here if touched but wasn't last time
      option1Touched = true;
      //lastOption1millis = millis();

      if(allNotesOff())
        option1TimerEnabled = true;
    }
  }
  else
  {
    // Here if untouched but it was last time
    option1Touched = false;

    if(option1TimerEnabled)
    {
      option1TimerEnabled = false;

      if(option1Timer < 350)
      {
        //Serial.println("Toggle relative major/minor");
        bool success = toggleRelativeMajorMinor();
        //String msg;
        //if(success)
        //  msg = "To " + scales[scaleIndex];
        //else
        //  msg = "Scale not supported  or note on";

        //displayMessage(msg);
      }
      else if(option1Timer > 1000)
      {
        //Serial.println("Toggle playchords");
        changeChordsOn(!playChords);
        //String msg;
        //if(success)
        //  msg = "To " + scales[scaleIndex];
        //else
        //  msg = "Scale not supported  or note on";

        //displayMessage(msg);
      }
    }
  }

#if 0
  // Read two bytes from the slave asynchronously. The first byte has the MSB set and
  // each byte has data for 7 pins. Only 8 pins are note pins.
  if(SERIALSLAVE.available())
  {
    uint8_t c;
    static uint8_t c1 = 0;
    static uint8_t lastc = 0;
      
    SERIALSLAVE.read(&c, 1);

    if(c & 0x80) // is it the first byte?
    {
      c1 = c; // just save it
      if(c != lastc)
      {
        lastc = c;
        //Serial.print(c, 16);
        //Serial.print(' ');
      }
    }
    else 
    {
      // second byte received
      // first process the first byte
      int i;

      for(i = 0; i < 7; i++)
      {
        bool touched = c1 & (0x40 >> i);

        processRemoteNotes(touched, i);
      }

      i = 7;
      bool touched = c & 0x01; // now look at the note bit in the second byte

      option4 = c & 0x10;

      if(option4)
      {
        if(!option4Touched)
        {
          // Here if touched but wasn't last time
          option4Touched = true;
          lastOption4millis = millis();
        }
      }
      else
      {
        // Here if untouched but it was last time
        if(option4Touched)
        {
          option4Touched = false;

          if(millis() - lastOption4millis < 500)
          {
            Serial.println("Toggle relative major/minor");
            bool success = toggleRelativeMajorMinor();
            String msg;
            if(success)
              msg = "To " + scales[scaleIndex];
            else
              msg = "Scale not supported  or note on";

            displayMessage(msg);
          }
          else
          {
            if(!allNotesOff())
            {
              String msg;
              msg = "Can't toggle chords if note on";
              displayMessage(msg);
            }
            else if(!notePlayedWhileOption4Touched)
            {
              Serial.println("Toggle chords on/off"); 
              if(playChords)
                displayMessage("  Chords OFF");
              else
                displayMessage("  Chords ON");
              playChords = !playChords;
            }
            else
              notePlayedWhileOption4Touched = false;
          }
        }
      }

      option5 = c & 0x80;
      option6 = c & 0x04;

      processRemoteNotes(touched, i);

      static bool lastOption2 = false;
      static bool lastOption3 = false;
      static bool lastOption6 = false;

      if(option2 && !lastOption2 && allNotesOff())
      {
        //Serial.println(optionsMode);
        if(optionsMode)
        {
          if(mode == "Scale")
          {
            changeScale(true);
            displayScale();
          }
          else if(mode == "Key")
          {
            changeKey(true);
            displayKey();
          }
          else if(mode == "Octave")
          {
            changeOctave(true);
            displayOctave();
          }
        }
        else
        {
          // In config mode now
          (*configChangeFunctions[config])(true);

          if(!optionsMode)  // Note that exit will take us out of config mode so in that case don't displayConfig()
            displayConfig();
        }
      }
      
      lastOption2 = option2;

      if(option3 && !lastOption3 && allNotesOff())
      {
        if(optionsMode)
        {
          if(mode == "Scale")
          {
            changeScale(false);
            displayScale();
          }
          else if(mode == "Key")
          {
            changeKey(false);
            displayKey();
          }
          else if(mode == "Octave")
          {
            changeOctave(false);
            displayOctave();
          }
        }
        else
        {
          // In config mode now
          //Serial.println("config mode");
          (*configChangeFunctions[config])(false);

          if(!optionsMode)  // Note that exit will take us out of config mode so in that case don't displayConfig()
            displayConfig();
        }
      }

      lastOption3 = option3;

      // Handle the adjacentPins and dissonantNotes filters
      if(playChords)
      {
        // Force the filters on if we are playing chords
        enableAdjacentPins = false;
        enableDissonantNotes = false;
      }
      else
      {
        // If option4 ignore both filters
        if(option4)
        {
          // Ignore the filters if option4 set
          enableAdjacentPins = true;
          enableDissonantNotes = true;
        }
        else
        {
          // set the enables according to the filters
          //Serial.printf("%d ", adjacentPinsFilter);
          if(adjacentPinsFilter)
            enableAdjacentPins = false;
          else
            enableAdjacentPins = true;

          if(dissonantNotesFilter)
            enableDissonantNotes = false;
          else
            enableDissonantNotes = true;
        }
      }

      static uint32_t t1 = millis();
      static uint32_t lastOption6millis = t1;
      static bool option6TimerStarted = false;
      static bool option6Timeout = true;

      if(option6 && !lastOption6) // Was option6 just pressed?
      {
        lastOption6millis = millis();

        option6TimerStarted = true;
      }

      if(option6 && option6TimerStarted)
      {
        if(millis() - lastOption6millis > 2000) // Has option6 been pressed for more that 2 seconds?
        {
          //Serial.println("Entering config mode");

          optionsMode = false;

          displayConfig();

          option6Timeout = true;

          option6TimerStarted = false;
        }
      }

      if(!option6 && lastOption6) // was option6 just released?
      {
        {
          if(optionsMode)
            changeMode();
          else
          {
            if(option6Timeout)
            {
              option6Timeout = false;
            }
            else
              changeConfig();
          }
        }
      }

      lastOption6 = option6;      
    }
  } 
#endif

#if RGBLED
  if(allNotesOff())
  {
    pixels.setPixelColor(0, 0x000000); // set to black

    // Send the updated pixel color to the hardware.
    pixels.show();   
  }
#endif

  if(useBluetooth && bluetoothConnected)
  {
    uint8_t type, data1, data2;

    if(midiRead(&type, &data1, &data2))
    {
      handleChangeRequest(type, data1, data2);

      displayRefresh();
    }
  }
}

void handleNoteOn(byte channel, byte pitch, byte velocity)
{
  // Log when a note is pressed.
  //Serial.print("Note on: channel = ");
  //Serial.print(channel);

  //Serial.print(" pitch = ");
  //Serial.print(pitch);

  //Serial.print(" velocity = ");
  //Serial.println(velocity);
}

void handleNoteOff(byte channel, byte pitch, byte velocity)
{
  // Log when a note is released.
  //Serial.print("Note off: channel = ");
  //Serial.print(channel);

  //Serial.print(" pitch = ");
  //Serial.print(pitch);

  //Serial.print(" velocity = ");
  //Serial.println(velocity);
}

// MPU6050 Stuff
#include "I2Cdev.h"

#include "MPU6050_6Axis_MotionApps20.h"

#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif

MPU6050 mpu(0x68, &Wire1);

#define OUTPUT_READABLE_YAWPITCHROLL

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;           // [w, x, y, z]         quaternion container
VectorInt16 aa;         // [x, y, z]            accel sensor measurements
VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity;    // [x, y, z]            gravity vector
float euler[3];         // [psi, theta, phi]    Euler angle container

void MPU6050Setup() 
{
    // join I2C bus (I2Cdev library doesn't do this automatically)
    #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    
        Wire1.setPins(41, 42); // SDA, SCL // added to test on the EMMMA-K
        Wire1.begin();
        Wire1.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties
    #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
        Fastwire::setup(400, true);
    #endif

    // initialize device
    Serial.println(F("Initializing I2C devices..."));
    mpu.initialize();

    // verify connection
    Serial.println(F("Testing device connections..."));
    Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

    // load and configure the DMP
    Serial.println(F("Initializing DMP..."));
    devStatus = mpu.dmpInitialize();

    // supply your own gyro offsets here, scaled for min sensitivity
    // these seem to work fine for every MPU6050 I have tried...
    mpu.setXGyroOffset(220);
    mpu.setYGyroOffset(76);
    mpu.setZGyroOffset(-85);
    mpu.setZAccelOffset(1788); // 1688 factory default

    // make sure it worked (returns 0 if so)
    if(devStatus == 0) 
    {
        // Calibration Time: generate offsets and calibrate our MPU6050
        mpu.CalibrateAccel(6);
        mpu.CalibrateGyro(6);
        mpu.PrintActiveOffsets();

        // turn on the DMP, now that it's ready
        Serial.println(F("Enabling DMP..."));
        mpu.setDMPEnabled(true);

        mpuIntStatus = mpu.getIntStatus();

        // set our DMP Ready flag so the main loop() function knows it's okay to use it
        Serial.println(F("DMP ready!"));
        dmpReady = true;

        // get expected DMP packet size for later comparison
        packetSize = mpu.dmpGetFIFOPacketSize();
    } 
    else 
    {
        // ERROR!
        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        // (if it's going to break, usually the code will be 1)
        Serial.print(F("DMP Initialization failed (code "));
        Serial.print(devStatus);
        Serial.println(F(")"));
    }
}

void MPU6050Loop() 
{
    // if programming failed, don't try to do anything
    if (!dmpReady) return;

    // read a packet from FIFO
    if(option1 && mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) 
    { 
      // Get the Latest packet but only when option1 is touched
      #ifdef OUTPUT_READABLE_YAWPITCHROLL
          // Euler angles in degrees
          mpu.dmpGetQuaternion(&q, fifoBuffer);
          mpu.dmpGetGravity(&gravity, &q);
          mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
      #endif
    }
}
