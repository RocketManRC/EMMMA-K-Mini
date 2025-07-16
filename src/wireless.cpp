#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_ESP32_NimBLE.h>
#include <WiFi.h>
#include <esp_now.h>

extern bool bluetoothConnected;
extern uint32_t espNowMicrosAtSend;
extern uint32_t espNowReturnTime;  // this will hold the return time of Esp_now in microseconds
//uint8_t espNowDeliveryStatus;
extern void setPixel(int index, uint32_t colour);
extern bool binding;
extern void saveConfig(); // in ui.cpp
extern void handleChangeRequest(uint8_t type, uint8_t data1, uint8_t data2);
extern void displayRefresh();
extern String broadcastAddressMidiHub;

uint8_t broadcastAddressRgbMatrix[] = {0x4C, 0x75, 0x25, 0xA6, 0xD6, 0x34};   // Experiment with the Atom Lite and RGB LED matri


// BLE MIDI
BLEMIDI_CREATE_INSTANCE("EMMMA-K",MIDI)  

bool midiRead(uint8_t *type, uint8_t *data1, uint8_t *data2)
{
  bool result = false;

  if(MIDI.read())
  {
    *type = MIDI.getType();
    *data1 = MIDI.getData1();
    *data2 = MIDI.getData2();

    result = true;
  }

  return result;
}

void BleOnConnected()
{
  Serial.println("Connected");

  bluetoothConnected = true;
}

void BleOnDisconnected()
{
  Serial.println("Disconnected");

  bluetoothConnected = false;
}

// This is the callback for ESP-Now success/failure
void data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) 
{
  uint32_t m = micros();
  espNowReturnTime = m - espNowMicrosAtSend; // this is how many us it takes to get back the Esp-Now reply

  if(status) // did delivery fail?
  {
    // check to see if this is the rgb matrix
    bool isRbgMatrix = true;
    for(int i = 0; i < 6; i++)
    {
      if(mac_addr[i] != broadcastAddressRgbMatrix[i])
        isRbgMatrix = false;
    }

    if(isRbgMatrix)
    {
      // remove the rgb matrix from the sender list
      esp_now_del_peer(broadcastAddressRgbMatrix);
    }
    else
    {
      setPixel(0, 0xFF0000); // set LED to red

      espNowReturnTime = 0xFFFFFFFF; // to flag an error on the note display
    }
  }
}

void data_received(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
  if(binding) // are we waiting for the hub to bind?
  {
    // check if there is a valid id in the data
    const uint8_t id[] = {'E', 'M', 'M', 'M', 'A', '-', 'K'};

    if(data_len == sizeof(id) && !memcmp(id, data, data_len))
    {
      // initialize hub address, save config and reboot...
      memcpy((void *)broadcastAddressMidiHub.c_str(), mac_addr, 6);

      saveConfig();

      ESP.restart();
    }
  }

  handleChangeRequest(data[0], data[1], data[2]);

  displayRefresh();
}

void initWireless(bool useBluetooth)
{
    if(useBluetooth)
    {
      BLEMIDI.setHandleConnected(BleOnConnected);
      BLEMIDI.setHandleDisconnected(BleOnDisconnected);
    
      MIDI.begin(MIDI_CHANNEL_OMNI);
    }
    else
    {
      WiFi.mode(WIFI_MODE_STA);
      Serial.println(WiFi.macAddress());  // 84:F7:03:F8:3E:4E

      if (esp_now_init() != ESP_OK) 
      {
        Serial.println("Error initializing ESP-NOW");

        setPixel(0, 0xFF0000); // set LED to red

        return;
      }

      esp_now_register_send_cb(data_sent);
      esp_now_register_recv_cb(data_received);

      esp_now_peer_info_t peerInfo1 = {}; // must be initialized to 0
      memcpy(peerInfo1.peer_addr, (void *)broadcastAddressMidiHub.c_str(), 6);
      peerInfo1.channel = 0;  
      peerInfo1.encrypt = false;     

      if(esp_now_add_peer(&peerInfo1) != ESP_OK)
      {
        Serial.println("Failed to add peer");

        setPixel(0, 0xFF0000); // set LED to red

        return;  
      }

      setPixel(0, 0x0000FF); // set LED to blue

      esp_now_peer_info_t peerInfo2 = {}; // must be initialized to 0
      memcpy(peerInfo2.peer_addr, broadcastAddressRgbMatrix, 6);
      peerInfo2.channel = 0;  
      peerInfo2.encrypt = false;     

      if(esp_now_add_peer(&peerInfo2) != ESP_OK)
      {
        Serial.println("Failed to add peer");

        setPixel(0, 0xFF0000); // set LED to red

        return;  
      }

      setPixel(0, 0xFF0000); // set LED to blue 

      //WiFi.setTxPower(WIFI_POWER_19_5dBm); // I'm not sure this will do anything...
    }
}


int wirelessSend(uint8_t *incomingData, int len, bool useBluetooth)
{
  if(useBluetooth)
  {
    // The first type of packet is 4, 8 or 12 bytes for notes and chords. The first is the MIDI note value, the second is a flag 
    // for note on or off, the third is the volume and the fourth is the MIDI channel.
    // If it is a 9 byte packet (for a double) it is a pitch bend with the 9th byte being MIDI channel.
    // A 3 byte packet is for CCs with the first being the CC number, the second the value
    // and the third the MIDI channel

    if(bluetoothConnected)
    {
      if(len == 4 || len == 8 || len == 12) // Notes
      {
        if(incomingData[1]) // first note
        {
          MIDI.sendNoteOn(incomingData[0], incomingData[2], incomingData[3]);
        }
        else
        {
          MIDI.sendNoteOff(incomingData[0], incomingData[2], incomingData[3]);
        }
        
        if(len == 8 || len == 12) // second note if any
        {
          if(incomingData[5])
          {
            MIDI.sendNoteOn(incomingData[4], incomingData[6], incomingData[7]);
          }
          else
          {
            MIDI.sendNoteOff(incomingData[4], incomingData[6], incomingData[7]);
          }
        }
        
        if(len == 12) // third note if any
        {
          if(incomingData[9])
          {
            MIDI.sendNoteOn(incomingData[8], incomingData[10], incomingData[11]);
          }
          else
          {
            MIDI.sendNoteOff(incomingData[8], incomingData[10], incomingData[11]);
          }
        }
      }
      else if(len == 9) // Pitch bend
      {
        double bendData;
        uint8_t *dp = (uint8_t *)&bendData;
        for(int i = 0; i < 8; i++)
        {
          dp[i] = incomingData[i];
        }

        MIDI.sendPitchBend(bendData, incomingData[8]);
      }
      else if(len == 3) // CC
      {
        MIDI.sendControlChange(incomingData[0], incomingData[1], incomingData[2]);
      }
    }

    return 0;
  }
  else
  {
    // Use esp_now...
    return esp_now_send(0, incomingData, len);
  }
}

