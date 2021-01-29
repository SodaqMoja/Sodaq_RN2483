/*
* Copyright (c) 2015 SODAQ. All rights reserved.
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

#if defined(ARDUINO_AVR_SODAQ_MBILI) || defined(ARDUINO_AVR_SODAQ_TATU)
// MBili
#define debugSerial Serial
#define loraSerial Serial1
#define beePin 20
#elif defined(ARDUINO_SODAQ_AUTONOMO)
// Autonomo
#define debugSerial SerialUSB
#define loraSerial Serial1
#define beePin BEE_VCC
#elif defined(ARDUINO_SODAQ_ONE) || defined(ARDUINO_SODAQ_ONE_BETA)
// Sodaq One
#define debugSerial SerialUSB
#define loraSerial Serial1
#elif defined(ARDUINO_SODAQ_EXPLORER)
#define debugSerial SerialUSB
#define loraSerial Serial2
#else
#error "Please select Autonomo, Mbili, or Tatu"
#endif


const uint8_t devAddr[4] =
{
    0x00, 0x1A, 0x62, 0xAE
};

// USE YOUR OWN KEYS!
const uint8_t appSKey[16] =
{
    0x0D, 0x0E, 0x0A, 0x0D,
    0x0B, 0x0E, 0x0E, 0x0F,
    0x0C, 0x0A, 0x0F, 0x0E,
    0x0B, 0x0A, 0x0B, 0x0E,
};

// USE YOUR OWN KEYS!
const uint8_t nwkSKey[16] =
{
    0x0D, 0x0E, 0x0A, 0x0D,
    0x0B, 0x0E, 0x0E, 0x0F,
    0x0C, 0x0A, 0x0F, 0x0E,
    0x0B, 0x0A, 0x0B, 0x0E,
};

void setup()
{
    debugSerial.begin(57600);

    while ((!debugSerial) && (millis() < 10000)) {
        // wait 10 seconds for serial monitor
    }

#ifdef beePin
    digitalWrite(beePin, HIGH);
    pinMode(beePin, OUTPUT);
    delay(500);
#endif

    loraSerial.begin(LoRaBee.getDefaultBaudRate());

    if (LoRaBee.initABP(loraSerial, devAddr, appSKey, nwkSKey, true)) {
        debugSerial.println("Connection to the network was successful.");
    }
    else {
        debugSerial.println("Connection to the network failed!");
    }
}

void loop()
{
    while (debugSerial.available()) 
    {
        loraSerial.write((char)debugSerial.read());
    }

    while (loraSerial.available()) 
    {
        debugSerial.write((char)loraSerial.read());
    }
}
