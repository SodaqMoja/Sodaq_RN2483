# Sodaq_RN2483

Arduino library for using the Microchip RN2483 LoRaWAN module (Class A).


## Usage

Quick example:

```c
#include <Sodaq_RN2483.h>

#if defined(ARDUINO_SODAQ_EXPLORER)
#define CONSOLE_STREAM  SerialUSB
#define LORA_STREAM     Serial2
#define LORA_RESET_PIN  LORA_RESET
#elif defined(ARDUINO_SODAQ_ONE)
#define CONSOLE_STREAM  SerialUSB
#define LORA_STREAM     Serial1
#define LORA_RESET_PIN  LORA_RESET
#else
#error "Please select Sodaq ExpLoRer or SodaqOne board"
#endif

#define FORCE_FULL_JOIN 0
#define LORA_PORT       1
#define USE_OTAA        1
#define USE_ABP         0

#if USE_ABP
static const uint8_t DEV_ADDR[4]  = { 0x00, 0x00, 0x00, 0x00 };
static const uint8_t NWK_SKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t APP_SKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
#elif USE_OTAA
static const uint8_t APP_EUI[8]   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t APP_KEY[16]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
#else
#error "Please use ABP or OTAA"
#endif

void setup()
{
    while (!CONSOLE_STREAM && millis() < 10000);

    LORA_STREAM.begin(LoRaBee.getDefaultBaudRate());
    LoRaBee.setDiag(CONSOLE_STREAM);

    CONSOLE_STREAM.println("Booting...");

    if (FORCE_FULL_JOIN || !LoRaBee.initResume(LORA_STREAM, LORA_RESET_PIN)) {
        LoRaBee.init(LORA_STREAM, LORA_RESET_PIN, true, true);

        uint8_t eui[8];
        if (LoRaBee.getHWEUI(eui, sizeof(eui)) != 8) { return; }

        #if USE_ABP
        if (!LoRaBee.initABP(DEV_ADDR, APP_SKEY, NWK_SKEY, false)) { return; }
        #else
        if (!LoRaBee.initOTA(eui, APP_EUI, APP_KEY, false)) { return; }
        #endif
    }

    CONSOLE_STREAM.println("Done");
}

void loop()
{
    // get frame counters
    char dnbuf[16];
    char upbuf[16];

    LoRaBee.getMacParam("dnctr", dnbuf, 16);
    LoRaBee.getMacParam("upctr", upbuf, 16);

    CONSOLE_STREAM.print("Downlink frame counter: ");
    CONSOLE_STREAM.println(dnbuf);
    CONSOLE_STREAM.print("Uplink frame counter: ");
    CONSOLE_STREAM.println(upbuf);

    // send test
    CONSOLE_STREAM.println("Sending test message...");

    uint8_t buf[] = {'t', 'e', 's', 't'};

    uint8_t i = LoRaBee.send(LORA_PORT, buf, sizeof(buf));

    CONSOLE_STREAM.print("LoRa sendTest: ");
    CONSOLE_STREAM.println(i);

    // wait 5 minutes
    CONSOLE_STREAM.println("Need wait 5 minutes...");
    delay(5 * 60 * 1000);
}
```

Method|Description
------|------
**getDefaultBaudRate()**|Returns the correct baudrate for the serial port that connects to the device.
**setDiag(Stream& stream)**|Sets the optional "Diagnostics and Debug" stream.
**init(SerialType& stream, int8_t resetPin = -1, bool needInitParams = true, bool needMacReset = false)**|Takes care of the initialization tasks common to both initOTA() and initABP(). If hardware reset is available, the module is re-set, otherwise it is woken up if possible. Returns true if the module replies to a device reset command.
**initOTA(Stream& stream, const uint8_t devEUI[8], const uint8_t appEUI[8], const uint8_t appKey[16], bool adr = true, int8_t resetPin = -1)**|Initializes the device and connects to the network using Over-The-Air Activation. Returns true on successful connection.
**initABP(Stream& stream, const uint8_t devAddr[4], const uint8_t appSKey[16], const uint8_t nwkSKey[16], bool adr = true, int8_t resetPin = -1)**|Initializes the device and connects to the network using Activation By Personalization. Returns true on successful connection.
**initResume(SerialType& stream, int8_t resetPin)**|Tries to initialize device with previously stored configuration parameters and state. Returns true if initialization successful.
**send(uint8_t port, const uint8_t\* payload, uint8_t size)**|Sends the given payload without acknowledgement. Returns 0 (NoError) when transmission is successful or one of the MacTransmitErrorCodes otherwise.
**sendReqAck(uint8_t port, const uint8_t\* payload, uint8_t size, uint8_t maxRetries)**|Sends the given payload with acknowledgement. Returns 0 (NoError) when transmission is successful or one of the MacTransmitErrorCodes otherwise.
**receive(uint8_t\* buffer, uint16_t size, uint16_t payloadStartPosition = 0)**|Copies the latest received packet (optionally starting from the "payloadStartPosition" position of the payload) into the given "buffer", up to "size" number of bytes. Returns the number of bytes written or 0 if no packet is received since last transmission.
**hardwareReset()**|Performs a hardware reset (using the reset pin -if available).
**getHWEUI(uint8_t\* buffer, uint8_t size)**|Gets the preprogrammed EUI node address from the module. Returns the number of bytes written or 0 in case of error.
**setFsbChannels(uint8_t fsb)**|Enables all the channels that belong to the given Frequency Sub-Band (FSB) and disables the rest. fsb is [1, 8] or 0 to enable all channels. Returns true if all channels were set successfully.
**setSpreadingFactor(uint8_t spreadingFactor)**|Sets the spreading factor. In reality it sets the datarate of the module according to the LoraWAN specs mapping for 868MHz and 915MHz, using the given spreading factor parameter.
**setPowerIndex(uint8_t powerIndex)**|Sets the power index (868MHz: 1 to 5 / 915MHz: 5, 7, 8, 9 or 10). Returns true if successful.
**getMacParam(const char\* paramName, char\* buffer, uint8_t size)**|Returns mac parameter.
**wakeUp()**|Wakes up the module from sleep (if supported).
**sleep()**|Puts the module to sleep (if supported).


###### And only if using dynamic buffers (USE_DYNAMIC_BUFFERS is defined in the library - not by default)
Method|Description
------|------
**setInputBufferSize (uint16_t value)**|Sets the size of the input buffer. Needs to be called before initOTA()/initABP().
**setReceivedPayloadBufferSize (uint16_t value)**|Sets the size of the "Received Payload" buffer. Needs to be called before initOTA()/initABP().


## Contributing

1. Fork it!
2. Create your feature branch: `git checkout -b my-new-feature`
3. Commit your changes: `git commit -am 'Add some feature'`
4. Push to the branch: `git push origin my-new-feature`
5. Submit a pull request


## License

Copyright (c) 2015-2019 SODAQ. All rights reserved.

This file is part of Sodaq_RN2483.

Sodaq_RN2483 is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation, either version 3 of
the License, or(at your option) any later version.

Sodaq_RN2483 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with Sodaq_RN2483.  If not, see
<http://www.gnu.org/licenses/>.
