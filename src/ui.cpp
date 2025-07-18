#include <Arduino.h>
#include <LittleFS.h>
#include "TFT_eSPI.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <ArduinoJson.h> 
#include <PNGdec.h>
#include <ESP32Encoder.h> // https://github.com/madhephaestus/ESP32Encoder.git 
#include <OneButton.h>
#include "rocketdecoded.h" // Image is stored here in an 8-bit array

#define TFT_GREY 0x5AEB // New colour
#define SCREEN_WIDTH 64  // OLED display width, in pixels
#define SCREEN_HEIGHT 128 // OLED display height, in pixels
#define OLED_RESET -1     // can set an oled reset pin if desired
Adafruit_SH1107 display = Adafruit_SH1107(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET, 1000000, 100000);

ESP32Encoder encoder;
OneButton button;

#define CLK 35 // CLK ENCODER 
#define DT 36 // DT ENCODER 
#define BUTTONPIN 0 // use GPIO0 for the encoder switch (and also be used to set MCU to download mode on powerup)

extern uint32_t espNowReturnTime;
extern void handleChangeRequest(uint8_t type, uint8_t data1, uint8_t data2);
extern bool allNotesOff();

TFT_eSPI tft = TFT_eSPI();  // Invoke library
TFT_eSprite img = TFT_eSprite(&tft);

uint16_t textColour = TFT_GREEN; // used to change the text colour for different modes (standard, relative and chords)
uint16_t backgroundColour = TFT_BLACK;

int octave = -1;
int scale = 0;

String oldTitle; // these two are used to change colour of the text and restore it
String oldValue;

void displayValue(String title, String value);
String keyNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

String mode = "Scale";
String modes[] = {"Scale", "Key", "Octave", "Volume"};

String scales[] = {"Major", "Minor", "Major Pentatonic", "Minor Pentatonic", 
  "Major Blues", "Minor Blues", "Minor Harmonic  ", "Minor Melodic", "Minor PO-33", "Dorian",
  "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian",
  "Lydian Dominish  ", "Super Locrian", "Whole Half Dim", "Half Whole Dim", "Chromatic", "Custom", "Custom Alt"};

int scaleCount = sizeof(scales)/sizeof(scales[0]); 

// notes (13 total for Mini) and scales (choose one)
uint8_t majorscale[] = {2, 2, 1, 2, 2, 2, 1}; // case 1
uint8_t minorscale[] = {2, 1, 2, 2, 1, 2, 2}; // case 2
uint8_t pentascale[] = {2, 2, 3, 2, 3}; // case 3
uint8_t minorpentascale[] = {3, 2, 2, 3, 2};  // case 4
uint8_t minorbluesscale[] = {3, 2, 1, 1, 3, 2}; // case 5

uint8_t majorbluesscale[] = {2, 1, 1, 3, 2, 3}; // case 6
uint8_t minorharmonic[] = {2, 1, 2, 2, 1, 3, 1};  // case 7
uint8_t minormelodic[] = {2, 1, 2, 2, 2, 2, 1}; // case 8
uint8_t minorpo33[] = {2, 1, 2, 2, 1, 2, 1, 1}; // case 9

uint8_t dorian[] = {2, 1, 2, 2, 2, 1, 2}; // case 10
uint8_t phrygian[] = {1, 2, 2, 2, 1, 2, 2}; // case 11
uint8_t lydian[] = {2, 2, 2, 1, 2, 2, 1};  // case 12
uint8_t mixolydian[] = {2, 2, 1, 2, 2, 1, 2}; // case 13
uint8_t aeolian[] = {2, 1, 2, 2, 1, 2, 2};  // case 14
uint8_t locrian[] = {1, 2, 2, 1, 2, 2, 2};  // case 15
uint8_t lydiandomiant[] = {2, 2, 2, 1, 2, 1, 2};  // case 16
uint8_t superlocrian[] = {1, 2, 1, 2, 2, 2, 2}; // case 17

uint8_t wholehalfdiminished[] = {2, 1, 2, 1, 2, 1, 2, 1}; // case 18
uint8_t halfwholediminished[] = {1, 2, 1, 2, 1, 2, 1, 2}; // case 19
uint8_t chromatic[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}; // case 20
uint8_t custom[] = {2, 2, 1, 2, 2, 2, 1}; // case 21 - custom is major scale
uint8_t customalt[] = {2, 2, 3, 2, 3}; // case 22 - custom alternative is major pentatonice scale


#define OCTAVE 12
#define KEY G - OCTAVE // choose desired key and octave offset here

uint8_t config = 0; // init to display adjacent pin filter
String configs[] = {"Adjacent Pin Filt", "Dissnt Notes Filt", "MIDI Channel",
  "Modwheel CC", "Wireless Mode", "Exit NO Save", "Save & Exit"};

void displayRefresh(); // Should displayMode() be used instead???
void displayMode();

uint8_t numberOfConfigItems = sizeof(configs)/sizeof(configs[0]);
void displayAdjacentPinFilt();
void displayDissonantNotesFilt();
void displayMidiChannel();
void displayMasterVolume();
void displayCcForModwheel();
void displayWirelessMode();
void displaySaveExitPrompt();
void displayExitNoSavePrompt();
void (*configDisplayFunctions[])() = {displayAdjacentPinFilt, displayDissonantNotesFilt, displayMidiChannel,
  displayCcForModwheel, displayWirelessMode, displayExitNoSavePrompt, displaySaveExitPrompt};

void changeAdjacentPinFilt(bool up);
void changeDissonantNotesFilt(bool up);
void changeMidiChannel(bool up);
void changeMasterVolume(bool up);
void changeCcForModwheel(bool up);
void changeWirelessMode(bool up);
void saveExitConfig(bool up);
void exitNoSaveConfig(bool up);
void saveConfig();
void (*configChangeFunctions[])(bool) = {changeAdjacentPinFilt, changeDissonantNotesFilt, changeMidiChannel,
  changeCcForModwheel, changeWirelessMode, exitNoSaveConfig, saveExitConfig};

void changeScale(bool up);
void changeKey(bool up);
void changeOctave(bool up);
void (*optionChangeFunctions[])(bool) = {changeScale, changeKey, changeOctave, changeMasterVolume};

bool playChords = false;
bool relativeScale = false;
uint8_t uiMode = 1; // 0 = change either options or config depending on optionsMode, 1 = change mode
bool optionsMode = true; // if true UI changes options (scale, key, etc), else UI changes config
bool wirelessChanged = false; // this will be set when the wireless mode changed causing a restart
bool savedUseBluetooth; // this is so we can put back the wireless mode if we exit config with no save
bool enableAdjacentPins = false;
bool enableDissonantNotes = true;

uint8_t masterVolume = 127; 

extern const int totalNotePins; 
extern const int totalNotes;    

extern bool notePinsOn[];
extern uint8_t midiValues[]; 

extern void sendCC(uint8_t cc, uint8_t value);

// Configuration Default Values
// To change the config update one or more of these values and rebuild.
String configInit = "3.14159265359"; // if this is changed config will be initialized (default 3.14159265359)
bool useBluetooth = false;
int scaleIndex = 3; // default is minor pentatonic
uint8_t midiChannel = 1;
bool adjacentPinsFilter = true;
bool dissonantNotesFilter = false;
uint8_t ccForModwheel = 1;
int key  = 0; // Start in key of c
String broadcastAddressMidiHub = "123456";

#define BL 45 // Backlight pin (1 = on)

PNG png; // PNG decoder instance

//uint16_t imageBuffer[128 * 128];
static uint16_t *imageBuffer = (uint16_t *)rocketdecoded;

void pngDraw(PNGDRAW *pDraw) 
{
  // This is going to be called repeatedly by decode to fill the image buffer
  static uint8_t row = 0;
  png.getLineAsRGB565(pDraw, &imageBuffer[row++ * 128], PNG_RGB565_BIG_ENDIAN, 0xffffffff);

  if(row > 127)
    row = 0; // make sure to not be out of bounds if decode called again
}

void animate()
{
  static uint16_t x = 0;
  static uint16_t y = 0;
  static uint16_t count = 1;

  while(1)
  {
    if(x > 239)
    {
      x = 0;
      y = 0;

      return;
    }

    img.fillSprite(TFT_BLACK); 

    //img.drawRect(x, y, 128, 128, TFT_GREEN);
    img.pushImage(x, y, 128, 128, imageBuffer);

    img.pushSprite(0, 0);

    y += count / 10;
    x = y * 230 / 280;

    count++;
  }
}

void displayBinding()
{
  img.fillScreen(TFT_BLACK);
  img.setCursor(20,100);        
  img.setTextColor(TFT_RED, backgroundColour);      
  img.println(F("Binding to hub..."));
  img.pushSprite(0, 0);
}

void displayValue(String title, String value)
{
  oldTitle = title;
  oldValue = value;

  img.fillScreen(TFT_BLACK);
  if(uiMode == 0)
  {
    img.setTextColor(TFT_ORANGE, backgroundColour);  

    img.setCursor(80, 20, 4);

    if(optionsMode)
    {
      img.print("OPTIONS");
    }
    else
    {
      img.print("CONFIG");
    }
  }

  img.setCursor(40, 80, 4);
  img.setTextColor(textColour, backgroundColour);  
  img.print(title + ":");
  if(value.length() > 15)
    img.setCursor(40, 120);
  else
    img.setCursor(60, 120);
  img.print(value);

  // Special handling for volume display, save and exit, exit no save and wireless mode) to give info
  if(title == "Master Volume")
  {
    img.setTextColor(TFT_BLUE, backgroundColour);  
    img.setCursor(40, 160, 2);
    img.print("- Single Click to Toggle Vol");
    img.setCursor(40, 180, 2);
    img.print("- Double Click to Options");
    img.setCursor(40, 200, 2);
    img.print("- Long Press to Send CC80");
  }
  else if(title == "Save & Exit")
  {
    img.setTextColor(TFT_BLUE, backgroundColour);  
    img.setCursor(40, 160, 2);
    img.print("(Click then rotate knob)");    
  }
  else if(title == "Exit NO Save")
  {
    img.setTextColor(TFT_BLUE, backgroundColour);  
    img.setCursor(40, 160, 2);
    img.print("(Click then rotate knob)");        
  }
  else if(title == "Wireless Mode")
  {
    img.setTextColor(TFT_BLUE, backgroundColour);  
    img.setCursor(30, 160, 2);
    img.print("(Will reboot on exit if changed)");            
  }

  img.pushSprite(0, 0);
}

void displayScale()     
{
  displayValue(String("SCALE") + " " + String(scaleIndex + 1) + "/" + String(scaleCount), String(scales[scaleIndex]));
}

void displayKey()
{
  displayValue(String("KEY"), keyNames[key]);
}

void displayOctave()
{
  displayValue(String("OCTAVE"), String(octave));
}

void displayMasterVolume()     
{
  displayValue(String("Master Volume"), String(" ") + String(masterVolume)); // this forces correct label in optionsMode
}

void displayNotes(bool init)
{
  if(init)
  {
    displayValue(String("NOTES"), "none");
  }
  else if(mode == "Note")
  {
    String noteNames = "";

    uint8_t idx;

    for(int i = 0; i < totalNotePins; i++)
    {
      // The following is because ot the pin arrangement on the Mini...
      if(notePinsOn[i])
      {
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

        uint8_t midiValue = midiValues[idx] + octave * 12 + key;
        uint8_t idx = midiValue % 60;
        noteNames += String(keyNames[idx % 12]) + String(midiValue/12 - 2);

        if(i < totalNotePins - 1)
          noteNames += " ";
      }
    }

    if(noteNames != "") // Only display elapsed time if notes to display
    {
      if(espNowReturnTime) // are we using wireless (espNowReturnTime is nonzero)?
      {
        if(espNowReturnTime == 0xFFFFFFFF)
          noteNames += " Fail!";
        else
          noteNames += " " + String(espNowReturnTime);
      }
    }

    displayValue(String("NOTES"), noteNames);
    //Serial.println(espNowReturnTime);
  } 
}

void displayAdjacentPinFilt()     
{
  if(adjacentPinsFilter)
    displayValue(String(configs[config]), String(" On"));
  else
    displayValue(String(configs[config]), String(" Off"));
}

void displayDissonantNotesFilt()     
{
  if(dissonantNotesFilter)
    displayValue(String(configs[config]), String(" On"));
  else
    displayValue(String(configs[config]), String(" Off"));
}

void displayMidiChannel()     
{
  displayValue(String(configs[config]), String(" ") + String(midiChannel));
}

void displayCcForModwheel()     
{
  displayValue(String(configs[config]), String(" ") + String(ccForModwheel));
}

void displayWirelessMode()     
{
  if(useBluetooth)
    displayValue(String(configs[config]), String(" ") + "BLE");
  else
    displayValue(String(configs[config]), String(" ") + "ESP-Now");
}

void displaySaveExitPrompt()
{
  displayValue(String("Save & Exit"), String("->"));
}  

void displayExitNoSavePrompt()
{
  displayValue(String("Exit NO Save"), String("->"));
}  

bool chordSupported()
{
  String s = String(scales[scaleIndex]);

  bool result = (s == "Major") || (s == "Minor") || (s == "Major Pentatonic") ||
    (s == "Minor Pentatonic") || (s == "Minor Blues");

  return result;
}

void displayChords(bool init)
{
  if(init)
  {
    displayValue(String("CHORDS"), "none");
  }
  else if(mode == "Note")
  {
    String keyName = keyNames[key];
  
    String chordName = scales[scaleIndex];

    if(chordName == "Major Pentatonic") // Shorten names that are too long...
      chordName = "Major Penta";
    else if(chordName == "Minor Pentatonic")
      chordName = "Minor Penta";

    chordName = keyName + chordName + " ";

    String chordNames = "";

    uint8_t idx;

    for(int i = 0; i < totalNotePins; i++)
    {
#if 0
      if(notePinsOn[i])
      {
        uint8_t midiValue = midiValues[i] + octave * 12 + key;
        uint8_t idx = midiValue % 60;
        chordNames += String(keyNames[idx % 12]) + String(midiValue/12 - 2);

        if(i < totalNotePins - 1)
          chordNames += " ";
      }
#endif
      // The following is because ot the pin arrangement on the Mini...
      if(notePinsOn[i])
      {
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

        uint8_t midiValue = midiValues[idx] + octave * 12 + key;
        uint8_t idx = midiValue % 60;
        chordNames += String(keyNames[idx % 12]) + String(midiValue/12 - 2);

        if(i < totalNotePins - 1)
          chordNames += " ";
      }
    }

    if(chordNames != "")
      displayValue(String("CHORDS"), chordName + chordNames);
    else
     displayValue(String("CHORDS"), "");
   }
}

const uint16_t displaySize =  8 * SCREEN_WIDTH * ((SCREEN_HEIGHT + 7) / 8);
uint8_t displaySaveBuffer[displaySize];
uint8_t messageSaveBuffer[displaySize];

// LittleFS calls for config
void writeFile(String filename, String message)
{
    File file = LittleFS.open(filename, "w");

    if(!file)
    {
      Serial.println("writeFile -> failed to open file for writing");
      return;
    }

    if(file.print(message))
    {
      Serial.println("File written");
    } 
    else 
    {
      Serial.println("Write failed");
    }

    file.close();
}

String readFile(String filename)
{
  File file = LittleFS.open(filename);

  if(!file)
  {
    Serial.println("Failed to open file for reading");
    return "";
  }
  
  String fileText = "";
  while(file.available())
  {
    fileText = file.readString();
  }

  file.close();

  return fileText;
}

const String config_filename = "/config.json";

void saveConfig() 
{
  StaticJsonDocument<1024> doc;

  // write variables to JSON file
  doc["configInit"] = configInit;
  doc["useBluetooth"] = useBluetooth;
  doc["scaleIndex"] = scaleIndex;
  doc["midiChannel"] = midiChannel;
  //doc["masterVolume"] = masterVolume;
  doc["adjacentPinsFilter"] = adjacentPinsFilter;
  doc["dissonantNotesFilter"] = dissonantNotesFilter;
  doc["ccForModwheel"] = ccForModwheel;
  doc["key"] = key;
  doc["broadcastAddressMidiHub"] = broadcastAddressMidiHub;
  
  // write config file
  String tmp = "";
  serializeJson(doc, tmp);
  writeFile(config_filename, tmp);
}

bool readConfig() 
{
  String file_content = readFile(config_filename);

  int config_file_size = file_content.length();
  Serial.println("Config file size: " + String(config_file_size));

  if(config_file_size == 0)
  {
    Serial.println("Initializing config with defaults...");
    saveConfig();

    return false;
  }

  if(config_file_size > 1024) 
  {
    Serial.println("Config file too large");
    return false;
  }

  StaticJsonDocument<1024> doc;

  auto error = deserializeJson(doc, file_content);

  if(error) 
  { 
    Serial.println("Error interpreting config file");
    return false;
  }

  const String _configInit = doc["configInit"];
  const bool _useBluetooth = doc["useBluetooth"];
  const int _scaleIndex = doc["scaleIndex"];
  const int _midiChannel = doc["midiChannel"];
  //const int _masterVolume = doc["masterVolume"];
  const int _adjacentPinsFilter = doc["adjacentPinsFilter"];
  const int _dissonantNotesFilter = doc["dissonantNotesFilter"];
  const int _ccForModwheel = doc["ccForModwheel"];
  const int _key = doc["key"];
  const String _broadcastAddressMidiHub = doc["broadcastAddressMidiHub"];
  

  Serial.print("_configInit: ");
  Serial.println(_configInit);

  if(_configInit != configInit) // Have we initialized the config yet?
  {
    Serial.println("Initializing config to default values...");
    saveConfig(); // init with default values
  }
  else
  {
    configInit = _configInit;
    useBluetooth = _useBluetooth;
    scaleIndex = _scaleIndex;
    midiChannel = _midiChannel;
    //masterVolume = _masterVolume;
    adjacentPinsFilter = _adjacentPinsFilter;
    dissonantNotesFilter = _dissonantNotesFilter;
    ccForModwheel = _ccForModwheel;
    key = _key;
    memcpy((void *)broadcastAddressMidiHub.c_str(), _broadcastAddressMidiHub.c_str(), 6);
    
  }

  Serial.println(configInit);
  Serial.println(useBluetooth);
  Serial.println(scaleIndex);
  Serial.println(midiChannel);
  //Serial.println(masterVolume);

  return true;
}

void displayMode()
{
  if(mode == "Key")
  {
    displayKey();
  }
  else if(mode == "Octave")
  {
    displayOctave();
  }
  else if(mode == "Note")
  {
    if(playChords && chordSupported())
      displayChords(true);
    else
      displayNotes(true);
  }
  else if(mode == "Scale")
  {
    displayScale();
  }
  else if(mode == "Volume")
  {
    Serial.printf("display master volume: %d\n", masterVolume);
    displayMasterVolume();
  }
}

void changeMode(bool up)
{
  if(mode == "Scale")
  {
    if(up)
      mode = "Key";
    else
      mode = "Volume";
  }
  else if(mode == "Key")
  {
    if(up)
      mode = "Octave";
    else
      mode = "Scale";
  }
  else if(mode == "Octave")
  {
    if(up)
      mode = "Note";
    else
      mode = "Key";
  }
  else if(mode == "Note")
  {
    if(up)
      mode = "Volume";
    else
      mode = "Octave";
}
  else if(mode == "Volume")
  {
    if(up)
      mode = "Scale";
    else
      mode = "Note";
 }

  displayMode();
}

void changeScale(bool up)
{
  if(up)
  {
    if(scaleIndex == scaleCount - 1)
      scaleIndex = 0;
    else
      scaleIndex++;
  }
  else
  {
    if(scaleIndex == 0)
      scaleIndex = scaleCount - 1;
    else
      scaleIndex--;
  }
  
  handleChangeRequest(176, 68, scaleIndex + 1);
}

void changeKey(bool up)
{
  if(up)
  {
    if(key == 11)
      key = 0;
    else
      key++;
  }
  else
  {
    if(key == 0)
      key = 11;
    else
      key--;
  }
}

void changeOctave(bool up)
{
  if(up)
  {
    if(octave == 5)
      octave = -5;
    else
      octave++;
  }
  else
  {
    if(octave == -5)
      octave = 5;
    else
      octave--;
  }
}

void displayConfig()
{
  (*configDisplayFunctions[config])();
}

void changeConfig(bool encodeUp)
{
  if(encodeUp)
  {
    if(config >= numberOfConfigItems - 1)
      config = 0;
    else
      config++;
  }
  else
  {
    if(config == 0)
      config = numberOfConfigItems - 1;
    else
      config--;
  }

  //(*configChangeFunctions[config])(encodeUp);

  //displayConfig();
}

void changeAdjacentPinFilt(bool up)
{
  if(adjacentPinsFilter)
    adjacentPinsFilter = false;
  else
    adjacentPinsFilter = true;
}

void changeDissonantNotesFilt(bool up)
{
  if(dissonantNotesFilter)
    dissonantNotesFilter = false;
  else
    dissonantNotesFilter = true;
}

void changeMidiChannel(bool up)
{
  if(up)
  {
    if(midiChannel == 16)
      midiChannel = 1;
    else
      midiChannel++;
  }
  else
  {
    if(midiChannel == 1)
      midiChannel = 16;
    else
      midiChannel--;
  }
}

void changeMasterVolume(bool up)
{
#if 1
  uint8_t mv = masterVolume;

  if(up)
  {
    if(mv <= 122)
      mv += 5;
    else if(mv > 122)
      mv = 127;
  }
  else
  {
    if(mv >= 12)
      mv -= 5;
    else 
      mv = 7;
  }

  masterVolume = mv;
#else
  static uint8_t mv = masterVolume;
  Serial.println(mv);
  Serial.println(up);

  if(up)
  {
    if(mv <= 122)
      mv += 5;
    else if(mv > 122)
      mv = 127;
  }
  else
  {
    if(mv >= 52)
      mv -= 5;
    else if(mv < 52)
      mv = 47;
  }

  Serial.println(mv);

  float mvv = mv / 128.0;
  float mvvv = mvv * mvv * mvv * 128;
  uint8_t mvvvi = (uint8_t)mvvv;

  if(mvvvi < 7)
    mvvvi = 7;
  else if(mvvvi > 122)
    mvvvi = 127;
  Serial.println(mvvvi);

  masterVolume = mvvvi;

  //if(masterVolume < 5)
  //  masterVolume = 5;

  //Serial.printf("changed master volume: %d\n", masterVolume);
  #endif
}

void changeCcForModwheel(bool up)
{
  if(up)
  {
    if(ccForModwheel >= 127)
      ccForModwheel = 1;
    else
      ccForModwheel++;
  }
  else
  {
    if(ccForModwheel <= 1)
      ccForModwheel = 127;
    else
      ccForModwheel--;
  }
}

void changeWirelessMode(bool up)
{
  savedUseBluetooth = useBluetooth; // so we can put back wireless mode if exit NO save

  if(useBluetooth)
    useBluetooth = false;
  else
    useBluetooth = true;

  wirelessChanged = true; // this is going to cause a reset whether or not the config is saved!
}

void saveExitConfig(bool up)
{
  // save config here
  saveConfig();

  // exit
  optionsMode = true;
  uiMode = 1;
  config = 0; // so we get the start of config next time...

  mode = "Volume"; // back to default of mode is volume
  displayMasterVolume();

  if(wirelessChanged) // need to reboot if wireless was changed
    ESP.restart();
}

void exitNoSaveConfig(bool up)
{
  // exit without saving
  // This still changes the config but doesn't save it in flash
  optionsMode = true;
  uiMode = 1;
  config = 0; // so we get the start of config next time...

  Serial.println("save no exit");

  mode = "Volume"; // back to default of mode is volume
  displayMasterVolume();

  //if(wirelessChanged) // need to reboot if wireless was changed whether or not the config was saved - NOT DOING THIS ANYMORE
  //  ESP.restart();

  // Put back the wireless mode because we need to reboot to change it so the value displayed in config won't be correct
  useBluetooth = savedUseBluetooth;
}

void scaleToMidiValues(uint8_t *scale, uint8_t size)
{
  midiValues[0] = 60; // Scales tables always start at middle C thus the first value is 60

  for(int i = 1;  i < totalNotes; i++)
  {
    midiValues[i] = midiValues[i - 1] + scale[(i - 1) % size];
  }

  Serial.println();
}

//void playMidiValues()
//{
  //for(int i = 0;  i < totalNotes; i++)
  //{
    //USBMIDI.sendNoteOn(midiValues[i] +  key + octave * 12, 0, 1); 
    //delay(60);  
    //USBMIDI.sendNoteOff(midiValues[i] +  key + octave * 12, 0, 1); 
    //delay(60);
  //}
//}

void changeKey(int value)
{
  key = value; 
}

void changeOctave(int value)
{
  octave = value - 64; 
}

void changeMidiChannel(int value)
{
  midiChannel = value; 
}

void changeChordsOn(int value)
{
  if(chordSupported() && allNotesOff())
  {
    if(value > 0)
    {
      playChords = true;
      if(relativeScale)
        textColour = TFT_MAGENTA;
      else
        textColour = TFT_RED;
    }
    else
    {
      playChords = false;
      
      if(relativeScale)
        textColour = TFT_BLUE;
      else
        textColour = TFT_GREEN;
    }

    img.setTextColor(textColour, backgroundColour);  
    displayValue(oldTitle, oldValue);
  }
}

bool toggleRelativeMajorMinor()
{
  bool result = false;

  if(!allNotesOff())
    return result;  // don't want to do this if any notes are on...

  if(scales[scaleIndex] == "Major" || scales[scaleIndex] == "Major Pentatonic" || scales[scaleIndex] == "Custom")
  {
    result = true;

    scaleIndex++;

    if(scales[scaleIndex] != "Custom Alt") // Custom Alt scale doesn't need key or octave change
    {
      key -= 3;

      if(key == -1)
      {
        key = 11;
        octave--;
      }
      else if(key == -2)
      {
        key = 10;
        octave--;
      }
      else if(key == -3)
      {
        key = 9;
        octave--;
      }
    }

    //Serial.println(scales[scaleIndex]);

    if(scales[scaleIndex] == "Minor") // Note scale index has changed above so it was major it is now minor
    {
      scaleToMidiValues(minorscale, sizeof(minorscale));
    }
    else if(scales[scaleIndex] == "Minor Pentatonic")
    {
      scaleToMidiValues(minorpentascale, sizeof(minorpentascale));
    }
    else
    {
      scaleToMidiValues(customalt, sizeof(customalt));
    }

    if(relativeScale)
      relativeScale = false;
    else
      relativeScale = true;
  }
  else if(scales[scaleIndex] == "Minor" || scales[scaleIndex] == "Minor Pentatonic" || scales[scaleIndex] == "Custom Alt")
  {
    result = true;

    scaleIndex--;

    if(scales[scaleIndex] != "Custom") // Custom scale doesn't need key and octave change
    {
      key += 3;

      if(key == 12)
      {
        key = 0;
        octave++;
      }
      else if(key == 13)
      {
        key = 1;
        octave++;
      }
      else if(key == 14)
      {
        key = 2;
        octave++;
      }
    }
      
    //Serial.println(scales[scaleIndex]);
  
    if(scales[scaleIndex] == "Major") // Note scale index has changed above so it was minor it is now major
      scaleToMidiValues(majorscale, sizeof(majorscale));
    else if(scales[scaleIndex] == "Major Pentatonic")
      scaleToMidiValues(pentascale, sizeof(pentascale));
    else
      scaleToMidiValues(custom, sizeof(custom));
    
      if(relativeScale)
      relativeScale = false;
    else
      relativeScale = true;
  }

  if(result)
  {
    if(relativeScale)
    {
      if(playChords)
      {
        textColour = TFT_MAGENTA;
      }
      else
      {
        textColour = TFT_BLUE;
      }
    }
    else
    {
      if(playChords)
      {
        textColour = TFT_RED;
      }
      else
      {
        textColour = TFT_GREEN;
      }
    }

    displayValue(oldTitle, oldValue);
  }

  displayRefresh();

  return result;
}

void toggleRelativeScale(int value)
{
  toggleRelativeMajorMinor();  
}

void buttonClicked()
{
  Serial.println("button clicked");
  Serial.printf("uiMode: %d, optionsMode: %d\n", uiMode, optionsMode);
  
  if(mode != "Volume" || (mode == "Volume" && !optionsMode))
  {
    if(uiMode == 0)
      uiMode = 1;
    else
      uiMode = 0;

    if(optionsMode)
      displayMode();
    else
      displayConfig();
  }
  else
  {
    // Special processing in Volume mode and uiMode == 1: switch between low and high volume
    // and adjust each with knob. Double press to get out
    Serial.printf("uiMode: %d, optionsMode: %d\n", uiMode, optionsMode);
    if(uiMode == 0)
    {
      // this allows us to get into volume mode if we are out
      uiMode = 1;

      displayMode();
    }
    else
    {
      static uint8_t lowVolume = 12;
      static uint8_t highVolume = 127;
      static bool useHighVolume = false;

      if(useHighVolume)
      {
        // switching to high volume so adjust the low volume if it was changed
        if(masterVolume != lowVolume)
          lowVolume = masterVolume;

        masterVolume = highVolume;
        useHighVolume = false;
      }
      else
      {
        // switching to low volume so adjust the high volume if it was changed
        if(masterVolume != highVolume)
          highVolume = masterVolume;

        masterVolume = lowVolume;
        useHighVolume = true;
      }

      displayMode();
    }
  }
}

void buttonLongPress()
{
  Serial.println("button long press");
  Serial.printf("uiMode: %d, optionsMode: %d\n", uiMode, optionsMode);

  #if 0
  // Note that long press doesn't switch to config in Volume mode
  // need to use double press to get out

  if(mode != "Volume")
  {
    optionsMode = false;

    uiMode = 0;

    displayConfig();
  }
  else
  {
    // Long press in Volume mode sends CC80 toggled between 0 and 127 (off and on)
    static bool value = false;

    if(value == false)
    {
      sendCC(80, 127);
      value = true;  
    }
    else
    {
      sendCC(80, 0);
      value = false;
    }
  }
#else
  if(mode == "Volume" && optionsMode && uiMode)
  {
    // Long press in Volume mode sends CC80 toggled between 0 and 127 (off and on)
    static bool value = false;

    if(value == false)
    {
      sendCC(80, 127);
      value = true;  
    }
    else
    {
      sendCC(80, 0);
      value = false;
    }
  }
  else
  {
    optionsMode = false;

    uiMode = 0;

    displayConfig();
  }
#endif
}

void buttonDoubleClick()
{
  Serial.println("Button double click");

  if(mode == "Volume" && optionsMode)
  {
    uiMode = 0;
    
    displayMode();
  }
}

void setupEncoder()
{
    encoder.attachHalfQuad ( DT, CLK );
    encoder.setCount (38);
    button.setup(BUTTONPIN, INPUT_PULLUP, true);
    button.attachClick(buttonClicked);
    button.attachLongPressStart(buttonLongPress);  
    button.attachDoubleClick(buttonDoubleClick);
    button.setClickMs(200); // make the click work a little faster
}

void handleEncoder()
{
    uint32_t newPosition = encoder.getCount() / 2;
    static uint32_t lastPosition = newPosition;
    bool encoderUp = false;
  
    if(newPosition != lastPosition)
    {
      //Serial.println(newPosition);
  
      if(newPosition > lastPosition)
        encoderUp = true;
      else
        encoderUp = false;
  
      lastPosition = newPosition;
  
      Serial.printf("optionsMode: %d, uiMode: %d\n", optionsMode, uiMode);
  
      if(allNotesOff() && uiMode == 0)
      {
        // Change options or config depending on optionsMode
  
        if(optionsMode)
        {
          changeMode(encoderUp);
        }
        else
        {
          Serial.printf("in config mode, config: %d\n", config);
          // In config mode now
          //(*configChangeFunctions[config])(encoderUp);
  
          //displayConfig();
          changeConfig(encoderUp);
          displayConfig();
          Serial.println("changeConfig");
        }
      }
      else if(uiMode == 1)
      {
        if(optionsMode)
        {
          int modeNumber = 0;
  
          for(modeNumber = 0; modeNumber < sizeof(modes)/sizeof(modes[0]); modeNumber++)
          {
            if(mode == modes[modeNumber])
              break;
          }
  
          Serial.printf("modeNumber %d, number of nodes: %d\n", modeNumber, sizeof(modes)/sizeof(modes[0]));
          (*optionChangeFunctions[modeNumber])(encoderUp);
  
          displayMode();
  
          //displayConfig();
         // changeMode(encoderUp);
          Serial.println("changeMode");
        }
        else
        {
          //changeConfig(encoderUp);
          Serial.println("changeConfig2");
          (*configChangeFunctions[config])(encoderUp);
  
          // check to see if we changed optionsMode above which s pretty ugly but works
          if(!optionsMode)
            displayConfig();
        }
      }
    }
  }

void handleChangeRequest(uint8_t type, uint8_t data1, uint8_t data2)
{
  if(type == 176 && data1 == 68) // is this control change #68?
  {
    // if so data2 contains the scale: 1 to number of scales 
    scaleIndex = data2 - 1; // scaleIndex won't be set properly if called from external MIDI or ESP-Now...

    Serial.printf("scaleIndex in handleChangeRequest: %d\n", scaleIndex);

    switch(data2)
    {
      case 1:
        scaleToMidiValues(majorscale, sizeof(majorscale));
        break;
        
      case 2:
        scaleToMidiValues(minorscale, sizeof(minorscale));
        break;
        
      case 3:
        scaleToMidiValues(pentascale, sizeof(pentascale));
        break;
        
      case 4:
        scaleToMidiValues(minorpentascale, sizeof(minorpentascale));
        break;
        
      case 5:
        scaleToMidiValues(majorbluesscale, sizeof(majorbluesscale));
        break;        

      case 6:
        scaleToMidiValues(minorbluesscale, sizeof(minorbluesscale));
        break;        

      case 7:
        scaleToMidiValues(minorharmonic, sizeof(minorharmonic));
        break;        

      case 8:
        scaleToMidiValues(minormelodic, sizeof(minormelodic));
        break;        

      case 9:
        scaleToMidiValues(minorpo33, sizeof(minorpo33));
        break;        

      case 10:
        scaleToMidiValues(dorian, sizeof(dorian));
        break;        

      case 11:
        scaleToMidiValues(phrygian, sizeof(phrygian));
        break;        

      case 12:
        scaleToMidiValues(lydian, sizeof(lydian));
        break;        

      case 13:
        scaleToMidiValues(mixolydian, sizeof(mixolydian));
        break;        

      case 14:
        scaleToMidiValues(aeolian, sizeof(aeolian));
        break;        

      case 15:
        scaleToMidiValues(locrian, sizeof(locrian));
        break;        

      case 16:
        scaleToMidiValues(lydiandomiant, sizeof(lydiandomiant));
        break;        

      case 17:
        scaleToMidiValues(superlocrian, sizeof(superlocrian));
        break;        

      case 18:
        scaleToMidiValues(wholehalfdiminished, sizeof(wholehalfdiminished));
        break;        

      case 19:
        scaleToMidiValues(halfwholediminished, sizeof(halfwholediminished));
        break;        

      case 20:
        scaleToMidiValues(chromatic, sizeof(chromatic));
        break;

      case 21:
        scaleToMidiValues(custom, sizeof(custom));
        break;

      case 22:
        scaleToMidiValues(customalt, sizeof(customalt));
        break;        

      default:
        break;  
    }
  }
  else if(type == 176 && data1 == 69) // is this control change #69?
  {
    //playMidiValues(); // If so play the current scale
  }
  else if(type == 176 && data1 == 70) // is this control change #70?
  {
    changeKey(data2); // If so change the key
  }
  else if(type == 176 && data1 == 71) // is this control change #71?
  {
    changeOctave(data2); // If so play change the octave
  }
  else if(type == 176 && data1 == 72) // is this control change #72?
  {
    changeMidiChannel(data2); // If so change the midi channel
  }
  else if(type == 176 && data1 == 73) // is this control change #73?
  {
    changeChordsOn(data2); // If so change chords on
  }
  else if(type == 176 && data1 == 74) // is this control change #74?
  {
    toggleRelativeScale(data2); // If so toggle relative scale
  }
}

void displaySetup()
{
    pinMode(BL, OUTPUT); // GPIO 45 controls the LCD backlight
    digitalWrite(BL, 0); // Start with backlight off to avoid white flash on init
  
    tft.begin();
    tft.setRotation(2); // for splash screen
    tft.fillScreen(TFT_BLACK);
    
    img.setColorDepth(16);
    img.createSprite(240, 280);
    img.fillSprite(TFT_BLACK); 
  
    delay(1000);
    digitalWrite(BL, 1); // turn the backlight back on
  
    img.pushImage(0, 0, 128, 128, imageBuffer); // put the image in the sprite
  
    img.pushSprite(0, 0); // finally show it on the screen
  
    delay(1000);
  
    animate();
  
    tft.setRotation(3);
    img.setCursor(45, 100, 4);
    img.setTextColor(textColour, backgroundColour);  
    img.fillScreen(TFT_BLACK);
  
    img.println("EMMMA-K Mini");
    img.print("                   v2.0.2");
    img.pushSprite(0, 0);
}

void configSetup()
{
    if(!LittleFS.begin(true))
    {
        Serial.println("LittleFS Mount Failed");
        delay(1000);
        return;
    }
    else
    {
      Serial.println("readConfig");
      readConfig(); 
    }  
}

void buttonTick()
{
  button.tick(); // for the encoder button
}
