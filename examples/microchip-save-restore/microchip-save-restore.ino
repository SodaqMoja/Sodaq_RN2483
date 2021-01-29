/*
* Copyright (c) 2019 SODAQ. All rights reserved.
*
* This file is part of Sodaq_RN2483.
*
* Sodaq_RN2483 is free software: you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as
* published by the Free Software Foundation, either version 3 of
* the License, or(at your option) any later version.
*
* Sodaq_RN2483 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with Sodaq_RN2483.  If not, see
* <http://www.gnu.org/licenses/>.
*/

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
