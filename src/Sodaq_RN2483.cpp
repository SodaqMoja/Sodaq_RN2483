/*
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
*/

#include "Sodaq_RN2483.h"
#include "Sodaq_RN2483_internal.h"
#include "Utils.h"
#include <Sodaq_wdt.h>

//#define DEBUG

#ifdef DEBUG
#define debugPrintLn(...) do { if (this->_diagStream) this->_diagStream->println(__VA_ARGS__); } while(0)
#define debugPrint(...) do { if (this->_diagStream) this->_diagStream->print(__VA_ARGS__); } while (0)
#warning "Debug mode is ON"
#else
#define debugPrintLn(...)
#define debugPrint(...)
#endif

// Structure for mapping error response strings and error codes.
typedef struct StringEnumPair {
    const char* stringValue;
    uint8_t enumValue;
} StringEnumPair_t;

Sodaq_RN2483 LoRaBee;

// Creates a new Sodaq_RN2483 instance.
Sodaq_RN2483::Sodaq_RN2483() :
    _loraStream(0),
    _diagStream(0),
    _inputBufferSize(DEFAULT_INPUT_BUFFER_SIZE),
    _receivedPayloadBufferSize(DEFAULT_RECEIVED_PAYLOAD_BUFFER_SIZE),
    _receivedPayloadSize(0),
    _packetReceived(false),
    _isRN2903(false),
    _resetPin(-1),
    _appendCommand(false),
    _receiveCallback(0)
{
#ifdef USE_DYNAMIC_BUFFER
    this->_isBufferInitialized = false;
#endif
}

uint8_t Sodaq_RN2483::getVersion(char* versionBuffer, uint8_t size)
{
    uint8_t versionLen = strlen(this->_version);

    if (versionLen + 1 > size) {
        // space not enough, just abort, after clearing the version
        debugPrintLn("[getVersion] version buffer size is not enough. Aborting.");
        memset(versionBuffer, 0, size);

        return 0;
    }

    strncpy(versionBuffer, this->_version, versionLen);
    versionBuffer[versionLen] = '\0'; // make sure it is null terminated

    return versionLen + 1;
}

void Sodaq_RN2483::fillVersionFromReceivedBuffer()
{
    // start is after the first space after the RNxxxx string
    char* start = strchr(strstr(_inputBuffer, STR_DEVICE_TYPE_RN), ' ') + 1;

    // end is at the first space after the version
    char* end = strchr(start, ' ');

    size_t len = (end - start);

    // make sure there is space for null termination
    len = len > sizeof(_version) - 1 ? sizeof(_version) - 1 : len;

    memcpy(_version, start, len);
    _version[len] = '\0';
}

// Takes care of the init tasks common to both initOTA() and initABP.
// If hardware reset is available, the module is re-set, otherwise it is woken up if possible.
// Returns true if the module replies to a device reset command.
bool Sodaq_RN2483::init(SerialType& stream, int8_t resetPin, bool needInitParams, bool needMacReset)
{
    debugPrintLn("[init]");

    this->_loraStream = &stream;
    this->_loraStream->setTimeout(DEFAULT_CHAR_TIMEOUT);

    if (resetPin >= 0) {
        enableHardwareReset(resetPin);
    }

#ifdef USE_DYNAMIC_BUFFER

    // make sure the buffers are only initialized once
    if (!_isBufferInitialized) {
        this->_inputBuffer = static_cast<char*>(malloc(this->_inputBufferSize));
        this->_receivedPayloadBuffer = static_cast<char*>(malloc(this->_receivedPayloadBufferSize));

        _isBufferInitialized = true;
    }

#endif

    if (isHardwareResetEnabled()) {
        hardwareReset();
    }
    else {
        // make sure the module's state is synced and woken up
#ifdef ENABLE_SLEEP
        sleep();
        sodaq_wdt_safe_delay(10);
        wakeUp();
#endif
    }

    if (!resetDevice(needInitParams)) {
        return false;
    }

    return !needMacReset || sendCommand(_isRN2903 ? STR_CMD_MAC_RESET : STR_CMD_MAC_RESET_868);
}

// Initializes the device and connects to the network using Over-The-Air Activation.
// Returns true on successful connection.
bool Sodaq_RN2483::initOTA(SerialType& stream, const uint8_t devEUI[8], const uint8_t appEUI[8], const uint8_t appKey[16], bool adr, int8_t resetPin)
{
    debugPrintLn("[initOTA]");

    return init(stream, resetPin, true, true)
           && initOTA(devEUI, appEUI, appKey, adr);
}

bool Sodaq_RN2483::initOTA(const uint8_t devEUI[8], const uint8_t appEUI[8], const uint8_t appKey[16], bool adr)
{
    return setMacParam(STR_DEV_EUI, devEUI, 8)
        && setMacParam(STR_APP_EUI, appEUI, 8)
        && setMacParam(STR_APP_KEY, appKey, 16)
        && sendCommand("mac set devaddr 00000000")
        && sendCommand("mac set nwkskey 00000000000000000000000000000000")
        && sendCommand("mac set appskey 00000000000000000000000000000000")
        && setMacParam(STR_ADR, BOOL_TO_ONOFF(false))
        && joinNetwork(STR_OTAA)
        && (!adr || setMacParam(STR_ADR, BOOL_TO_ONOFF(adr)))
        && saveState();
}

// Initializes the device and connects to the network using Activation By Personalization.
// Returns true on successful connection.
bool Sodaq_RN2483::initABP(SerialType& stream, const uint8_t devAddr[4], const uint8_t appSKey[16], const uint8_t nwkSKey[16], bool adr, int8_t resetPin)
{
    debugPrintLn("[initABP]");

    return init(stream, resetPin, true, false)
           && initABP(devAddr, appSKey, nwkSKey, adr);
}

bool Sodaq_RN2483::initABP(const uint8_t devAddr[4], const uint8_t appSKey[16], const uint8_t nwkSKey[16], bool adr)
{
    return setMacParam(STR_DEV_ADDR, devAddr, 4)
        && setMacParam(STR_APP_SESSION_KEY, appSKey, 16)
        && setMacParam(STR_NETWORK_SESSION_KEY, nwkSKey, 16)
        && setMacParam(STR_ADR, BOOL_TO_ONOFF(false))
        && joinNetwork(STR_ABP)
        && (!adr || setMacParam(STR_ADR, BOOL_TO_ONOFF(adr)))
        && saveState();
}

// Tries to initialize device with previously stored configuration parameters and state.
// Returns true if initialization successful.
bool Sodaq_RN2483::initResume(SerialType& stream, int8_t resetPin)
{
  if (!init(stream, resetPin, false, false)) { return false; }

  return joinNetwork(STR_ABP);
}

// Saves the LoRaWAN Class A protocol configuration parameters to the user EEPROM.
bool Sodaq_RN2483::saveState()
{
  println(STR_CMD_SAVE);

  return expectString(STR_RESULT_OK, 5000);
}

// Sends the given payload without acknowledgement.
// Returns 0 (NoError) when transmission is successful or one of the MacTransmitErrorCodes otherwise.
uint8_t Sodaq_RN2483::send(uint8_t port, const uint8_t* payload, uint8_t size)
{
    debugPrintLn("[send]");

    uint8_t i = macTransmit(STR_UNCONFIRMED, port, payload, size);

    saveState();

    return i;
}

// Sends the given payload with acknowledgement.
// Returns 0 (NoError) when transmission is successful or one of the MacTransmitErrorCodes otherwise.
uint8_t Sodaq_RN2483::sendReqAck(uint8_t port, const uint8_t* payload,
                                 uint8_t size, uint8_t maxRetries)
{
    debugPrintLn("[sendReqAck]");

    if (!setMacParam(STR_RETRIES, maxRetries)) {
        // not a fatal error -just show a debug message
        debugPrintLn("[sendReqAck] Non-fatal error: setting number of retries failed.");
    }

    uint8_t i = macTransmit(STR_CONFIRMED, port, payload, size);

    saveState();

    return i;
}

// Copies the latest received packet (optionally starting from the "payloadStartPosition"
// position of the payload) into the given "buffer", up to "size" number of bytes.
// Returns the number of bytes written or 0 if no packet is received since last transmission.
uint16_t Sodaq_RN2483::receive(uint8_t* buffer, uint16_t size,
                               uint16_t payloadStartPosition)
{
    debugPrintLn("[receive]");

    if (!this->_packetReceived) {
        debugPrintLn("[receive]: There is no packet received!");
        return 0;
    }

    // check that the requested starting position is within bounds
    if (payloadStartPosition >= this->_receivedPayloadBufferSize) {
        debugPrintLn("[receive]: Out of bounds start position!");
        return 0;
    }

    uint16_t len = this->_receivedPayloadSize <= size ? this->_receivedPayloadSize : size;
    memcpy(buffer, this->_receivedPayloadBuffer, len);

    debugPrintLn("[receive]: Done");
    return len;
}

// Gets the preprogrammed EUI node address from the module.
// Returns the number of bytes written or 0 in case of error.
uint8_t Sodaq_RN2483::getHWEUI(uint8_t* buffer, uint8_t size)
{
    debugPrintLn("[getHWEUI]");

    println(STR_CMD_GET_HWEUI);

    // TODO move to general "read hex" method
    uint8_t inputIndex = 0;
    uint8_t outputIndex = 0;

    size_t len;
    unsigned long start = millis();

    while (millis() - start < DEFAULT_TIMEOUT) {
        sodaq_wdt_reset();
        debugPrint(".");

        len = readLn();

        if (len > 0) {
            debugPrintLn(this->_inputBuffer);

            if (strncmp(this->_inputBuffer, "invalid", 7) == 0) {
                return 0;
            }

            while (outputIndex < size
                    && inputIndex + 1 < this->_inputBufferSize
                    && this->_inputBuffer[inputIndex] != 0
                    && this->_inputBuffer[inputIndex + 1] != 0) {
                buffer[outputIndex] = HEX_PAIR_TO_BYTE(
                                          this->_inputBuffer[inputIndex],
                                          this->_inputBuffer[inputIndex + 1]);
                inputIndex += 2;
                outputIndex++;
            }

            debugPrint("[getHWEUI] count: ");
            debugPrintLn(outputIndex);
            return outputIndex;
        }
    }

    debugPrint("[getHWEUI] Timed out without a response!");
    return 0;
}

#ifdef ENABLE_SLEEP

void Sodaq_RN2483::wakeUp()
{
    debugPrintLn("[wakeUp]");

    // "emulate" break condition
    this->_loraStream->flush();

    this->_loraStream->begin(300);
    this->_loraStream->write((uint8_t)0x00);
    this->_loraStream->flush();

    sodaq_wdt_safe_delay(50);

    // set baudrate
    this->_loraStream->begin(getDefaultBaudRate());
    this->_loraStream->write((uint8_t)0x55);
    this->_loraStream->flush();

    readLn();
}

void Sodaq_RN2483::sleep()
{
    debugPrintLn("[sleep]");

    println(STR_CMD_SLEEP);
}

#endif

// Reads a line from the device stream into the "buffer" starting at the "start" position of the buffer.
// Returns the number of bytes read.
uint16_t Sodaq_RN2483::readLn(char* buffer, uint16_t size, uint16_t start)
{
    int len = this->_loraStream->readBytesUntil('\n', buffer + start, size);

    if (len > 0) {
        this->_inputBuffer[start + len - 1] = 0; // bytes until \n always end with \r, so get rid of it (-1)
    }

    return len;
}

// Waits for the given string. Returns true if the string is received before a timeout.
// Returns false if a timeout occurs or if another string is received.
bool Sodaq_RN2483::expectString(const char* str, uint16_t timeout)
{
    debugPrint("[expectString] (\"");
    debugPrint(str);
    debugPrint("\") ");

    bool retval = false;
    unsigned long start = millis();

    while (millis() - start < timeout) {
        sodaq_wdt_reset();
        debugPrint(".");

        if (readLn() > 0) {
            debugPrint("--> \"");
            debugPrint(this->_inputBuffer);
            debugPrint('"');

            if (strstr(this->_inputBuffer, str) != NULL) {
                debugPrintLn(" found a match!");
                retval = true;
            }

            break;
        }
    }

    debugPrintLn();
    return retval;
}

bool Sodaq_RN2483::expectOK()
{
    return expectString(STR_RESULT_OK);
}

void Sodaq_RN2483::hardwareReset()
{
    debugPrintLn("[hardwareReset]");

    if (_resetPin < 0) {
        debugPrintLn("[hardwareReset] The reset pin is not set. Skipping.");
        return;
    }

    // set pin mode every time to avoid constraining the user about when the pin is initialized
    pinMode(_resetPin, OUTPUT);
    digitalWrite(_resetPin, LOW);
    sodaq_wdt_safe_delay(150);
    digitalWrite(_resetPin, HIGH);

    expectString(STR_DEVICE_TYPE_RN);
}

void Sodaq_RN2483::writeProlog()
{
    if (!_appendCommand) {
        debugPrint(">> ");
        _appendCommand = true;
    }
}

// Write a byte, as binary data
size_t Sodaq_RN2483::writeByte(uint8_t value)
{
    return this->_loraStream->write(value);
}

size_t Sodaq_RN2483::print(const String& buffer)
{
    writeProlog();
    debugPrint(buffer);

    return this->_loraStream->print(buffer);
}

size_t Sodaq_RN2483::print(const char buffer[])
{
    writeProlog();
    debugPrint(buffer);

    return this->_loraStream->print(buffer);
}

size_t Sodaq_RN2483::print(char value)
{
    writeProlog();
    debugPrint(value);

    return this->_loraStream->print(value);
};

size_t Sodaq_RN2483::print(unsigned char value, int base)
{
    writeProlog();
    debugPrint(value, base);

    return this->_loraStream->print(value, base);
};

size_t Sodaq_RN2483::print(int value, int base)
{
    writeProlog();
    debugPrint(value, base);

    return this->_loraStream->print(value, base);
};

size_t Sodaq_RN2483::print(unsigned int value, int base)
{
    writeProlog();
    debugPrint(value, base);

    return this->_loraStream->print(value, base);
};

size_t Sodaq_RN2483::print(long value, int base)
{
    writeProlog();
    debugPrint(value, base);

    return this->_loraStream->print(value, base);
};

size_t Sodaq_RN2483::print(unsigned long value, int base)
{
    writeProlog();
    debugPrint(value, base);

    return this->_loraStream->print(value, base);
};

size_t Sodaq_RN2483::println(const __FlashStringHelper* ifsh)
{
    size_t n = print(ifsh);
    n += println();
    return n;
}

size_t Sodaq_RN2483::println(const String& s)
{
    size_t n = print(s);
    n += println();
    return n;
}

size_t Sodaq_RN2483::println(const char c[])
{
    size_t n = print(c);
    n += println();
    return n;
}

size_t Sodaq_RN2483::println(char c)
{
    size_t n = print(c);
    n += println();
    return n;
}

size_t Sodaq_RN2483::println(unsigned char b, int base)
{
    size_t i = print(b, base);
    return i + println();
}

size_t Sodaq_RN2483::println(int num, int base)
{
    size_t i = print(num, base);
    return i + println();
}

size_t Sodaq_RN2483::println(unsigned int num, int base)
{
    size_t i = print(num, base);
    return i + println();
}

size_t Sodaq_RN2483::println(long num, int base)
{
    size_t i = print(num, base);
    return i + println();
}

size_t Sodaq_RN2483::println(unsigned long num, int base)
{
    size_t i = print(num, base);
    return i + println();
}

size_t Sodaq_RN2483::println(double num, int digits)
{
    writeProlog();
    debugPrint(num, digits);

    return this->_loraStream->println(num, digits);
}

size_t Sodaq_RN2483::println(const Printable& x)
{
    size_t i = print(x);
    return i + println();
}

size_t Sodaq_RN2483::println(void)
{
    size_t i = print(CRLF);
    _appendCommand = false;
    return i;
}

// Sends a reset command to the module and waits for the success response (or timeout).
// Also sets-up some initial parameters like power index, SF and FSB channels.
// Returns true on success.
bool Sodaq_RN2483::resetDevice(bool needInitParams)
{
    debugPrintLn("[resetDevice]");

    memset(_version, 0, sizeof(_version));
    println(STR_CMD_RESET);

    if (expectString(STR_DEVICE_TYPE_RN)) {
        if (strstr(this->_inputBuffer, STR_DEVICE_TYPE_RN2483) != NULL) {
            debugPrintLn("The device type is RN2483");
            _isRN2903 = false;
            fillVersionFromReceivedBuffer();

            return !needInitParams
                   || (setPowerIndex(DEFAULT_PWR_IDX_868)
                   && setSpreadingFactor(DEFAULT_SF_868));
        }
        else if (strstr(this->_inputBuffer, STR_DEVICE_TYPE_RN2903) != NULL) {
            debugPrintLn("The device type is RN2903");
            // TODO move into init once it is decided how to handle RN2903-specific operations
            _isRN2903 = true;
            fillVersionFromReceivedBuffer();

            return !needInitParams
                   || (setFsbChannels(DEFAULT_FSB)
                   && setPowerIndex(DEFAULT_PWR_IDX_915)
                   && setSpreadingFactor(DEFAULT_SF_915));
        }
        else {
            debugPrintLn("Unknown device type!");

            return false;
        }
    }

    return false;
}

// Enables all the channels that belong to the given Frequency Sub-Band (FSB)
// and disables the rest.
// fsb is [1, 8] or 0 to enable all channels.
// Returns true if all channels were set successfully.
bool Sodaq_RN2483::setFsbChannels(uint8_t fsb)
{
    debugPrintLn("[setFsbChannels]");

    uint8_t first125kHzChannel = fsb > 0 ? (fsb - 1) * 8 : 0;
    uint8_t last125kHzChannel = fsb > 0 ? first125kHzChannel + 7 : 71;
    uint8_t fsb500kHzChannel = fsb + 63;

    bool allOk = true;

    for (uint8_t i = 0; i < 72; i++) {
        print(STR_CMD_SET_CHANNEL_STATUS);
        print(i);
        print(" ");
        print(BOOL_TO_ONOFF(((i == fsb500kHzChannel) || (i >= first125kHzChannel && i <= last125kHzChannel))));
        println();

        allOk &= expectOK();
    }

    return allOk;
}

// Sets the spreading factor.
// In reality it sets the datarate of the module according to the
// LoraWAN specs mapping for 868MHz and 915MHz,
// using the given spreading factor parameter.
bool Sodaq_RN2483::setSpreadingFactor(uint8_t spreadingFactor)
{
    debugPrintLn("[setSpreadingFactor]");

    int8_t datarate;

    if (!_isRN2903) {
        // RN2483 SF(DR) = 7(5), 8(4), 9(3), 10(2), 11(1), 12(0)
        datarate = 12 - spreadingFactor;
    }
    else {
        // RN2903 SF(DR) = 7(3), 8(2), 9(1), 10(0)
        datarate = 10 - spreadingFactor;
    }

    if (datarate > -1) {
        return setMacParam(STR_DATARATE, datarate);
    }

    return false;
}

// Sets the power index (868MHz: 1 to 5 / 915MHz: 5, 7, 8, 9 or 10)
// Returns true if succesful.
bool Sodaq_RN2483::setPowerIndex(uint8_t powerIndex)
{
    debugPrintLn("[setPowerIndex]");

    return setMacParam(STR_PWR_IDX, powerIndex);
}

// Sends the command together with the given paramValue (optional)
// to the device and awaits for the response.
// Returns true on success.
// NOTE: command should include a trailing space if paramValue is set
bool Sodaq_RN2483::sendCommand(const char* command, const uint8_t* paramValue, uint16_t size)
{
    print(command);

    for (uint16_t i = 0; i < size; ++i) {
        print(static_cast<char>(NIBBLE_TO_HEX_CHAR(HIGH_NIBBLE(paramValue[i]))));
        print(static_cast<char>(NIBBLE_TO_HEX_CHAR(LOW_NIBBLE(paramValue[i]))));
    }

    println();

    return expectOK();
}

// Sends the command together with the given paramValue (optional)
// to the device and awaits for the response.
// Returns true on success.
// NOTE: command should include a trailing space if paramValue is set
bool Sodaq_RN2483::sendCommand(const char* command, uint8_t paramValue)
{
    print(command);
    print(paramValue);
    println();

    return expectOK();
}

// Sends the command together with the given paramValue (optional)
// to the device and awaits for the response.
// Returns true on success.
// NOTE: command should include a trailing space if paramValue is set
bool Sodaq_RN2483::sendCommand(const char* command, const char* paramValue)
{
    print(command);

    if (paramValue != NULL) {
        print(paramValue);
    }

    println();

    return expectOK();
}

// Sends a join network command to the device and waits for the response (or timeout).
// Returns true on success.
bool Sodaq_RN2483::joinNetwork(const char* type)
{
    debugPrintLn("[joinNetwork]");

    print(STR_CMD_JOIN);
    print(type);
    println();

    return expectOK() && expectString(STR_ACCEPTED, 30000);
}

// Returns mac parameter.
void Sodaq_RN2483::getMacParam(const char* paramName, char* buffer, uint8_t size)
{
    print(STR_CMD_GET);
    println(paramName);

    unsigned long start = millis();

    while (millis() - start < DEFAULT_TIMEOUT) {
        sodaq_wdt_reset();
        debugPrint('.');

        if (readLn() > 0) {
            debugPrint("--> \"");
            debugPrint(this->_inputBuffer);
            debugPrint("\"");

            strncpy(buffer, this->_inputBuffer, size);

            break;
        }
    }

    debugPrintLn();
}

// Sends the given mac command together with the given paramValue
// to the device and awaits for the response.
// Returns true on success.
// NOTE: paramName should include a trailing space
bool Sodaq_RN2483::setMacParam(const char* paramName, const uint8_t* paramValue, uint16_t size)
{
    print(STR_CMD_SET);
    print(paramName);

    for (uint16_t i = 0; i < size; ++i) {
        print(static_cast<char>(NIBBLE_TO_HEX_CHAR(HIGH_NIBBLE(paramValue[i]))));
        print(static_cast<char>(NIBBLE_TO_HEX_CHAR(LOW_NIBBLE(paramValue[i]))));
    }

    println();

    return expectOK();
}

// Sends the given mac command together with the given paramValue
// to the device and awaits for the response.
// Returns true on success.
// NOTE: paramName should include a trailing space
bool Sodaq_RN2483::setMacParam(const char* paramName, uint8_t paramValue)
{
    print(STR_CMD_SET);
    print(paramName);
    print(paramValue);
    println();

    return expectOK();
}

// Sends the given mac command together with the given paramValue
// to the device and awaits for the response.
// Returns true on success.
// NOTE: paramName should include a trailing space
bool Sodaq_RN2483::setMacParam(const char* paramName, const char* paramValue)
{
    print(STR_CMD_SET);
    print(paramName);
    print(paramValue);
    println();

    return expectOK();
}

// Returns the enum that is mapped to the given "error" message.
uint8_t Sodaq_RN2483::lookupMacTransmitError(const char* error)
{
    debugPrint("[lookupMacTransmitError]: ");
    debugPrintLn(error);

    if (error[0] == 0) {
        debugPrintLn("[lookupMacTransmitError]: The string is empty!");
        return NoResponse;
    }

    StringEnumPair_t errorTable[] = {
        { STR_RESULT_INVALID_PARAM, InternalError },
        { STR_RESULT_NOT_JOINED, NotConnected },
        { STR_RESULT_NO_FREE_CHANNEL, Busy },
        { STR_RESULT_SILENT, Busy },
        { STR_RESULT_FRAME_COUNTER_ERROR, NetworkFatalError },
        { STR_RESULT_BUSY, Busy },
        { STR_RESULT_MAC_PAUSED, InternalError },
        { STR_RESULT_INVALID_DATA_LEN, PayloadSizeError },
        { STR_RESULT_MAC_ERROR, NoAcknowledgment },
    };

    for (StringEnumPair_t* p = errorTable; p->stringValue != NULL; ++p) {
        if (strcmp(p->stringValue, error) == 0) {
            debugPrint("[lookupMacTransmitError]: found ");
            debugPrintLn(p->enumValue);

            return p->enumValue;
        }
    }

    debugPrintLn("[lookupMacTransmitError]: not found!");
    return NoResponse;
}

uint8_t Sodaq_RN2483::macTransmit(const char* type, uint8_t port, const uint8_t* payload, uint8_t size)
{
    debugPrintLn("[macTransmit]");

    bool status = false;

    for (size_t ix = 0; ix < 3 && !status; ix++) {
        print(STR_CMD_MAC_TX);
        print(type);
        print(port);
        print(" ");

        for (int i = 0; i < size; ++i) {
            print(static_cast<char>(NIBBLE_TO_HEX_CHAR(HIGH_NIBBLE(payload[i]))));
            print(static_cast<char>(NIBBLE_TO_HEX_CHAR(LOW_NIBBLE(payload[i]))));
        }

        println();
        status = expectOK();
    }

    if (!status) {
        return lookupMacTransmitError(this->_inputBuffer); // inputBuffer still has the last line read
    }

    // prepare for receiving a new packet
    this->_packetReceived = false;
    this->_receivedPayloadSize = 0;

    debugPrint("Waiting for server response");
    unsigned long start = millis();

    while (millis() - start < RECEIVE_TIMEOUT) {
        sodaq_wdt_reset();
        debugPrint(".");

        if (readLn() > 0) {
            debugPrintLn(".");
            debugPrint("(");
            debugPrint(this->_inputBuffer);
            debugPrintLn(")");

            if (strstr(this->_inputBuffer, " ") != NULL) { // to avoid double delimiter search
                // there is a splittable line -only case known is mac_rx
                debugPrintLn("Splittable response found");
                return onMacRX();
            }
            else if (strstr(this->_inputBuffer, STR_RESULT_MAC_TX_OK)) {
                // done
                debugPrintLn("Received mac_tx_ok");
                return NoError;
            }
            else {
                // lookup the error message
                debugPrintLn("Some other string received (error)");
                return lookupMacTransmitError(this->_inputBuffer);
            }
        }
    }

    debugPrintLn("Timed-out waiting for a response!");
    return Timeout;
}

// Parses the input buffer and converts and copies the received payload into the "received payload" buffer
// when a "mac rx" message has been received. It is called internally by macTransmit().
// Returns 0 (NoError) or otherwise one of the MacTransmitErrorCodes.
uint8_t Sodaq_RN2483::onMacRX()
{
    debugPrintLn("[onMacRX]");

    // parse inputbuffer, put payload into packet buffer
    char* token = strtok(this->_inputBuffer, " ");

    // sanity check
    if (strcmp(token, STR_RESULT_MAC_RX) != 0) {
        debugPrintLn("[onMacRX]: mac_rx keyword not found!");
        return InternalError;
    }

    // port
    token = strtok(NULL, " ");

    // payload
    token = strtok(NULL, " "); // until end of string

    if (!token) {
        debugPrintLn("[onMacRX]: packet contains no payload.");
        return NoError;
    }

    uint16_t len = strlen(token) + 1; // include termination char
    uint16_t start = (token - _inputBuffer);
    uint16_t inputIndex = start;

    uint16_t outputIndex = 0;

    // stop at the first string termination char, or if payload buffer is over, or if input buffer is over
    while ((this->_inputBuffer[inputIndex] != 0)
            && (this->_inputBuffer[inputIndex + 1] != 0)
            && (inputIndex + 1 < this->_inputBufferSize)
            && (inputIndex - start < len)
            && (outputIndex + 1 < this->_receivedPayloadBufferSize)) {
        _receivedPayloadBuffer[outputIndex] = HEX_PAIR_TO_BYTE(
                this->_inputBuffer[inputIndex],
                this->_inputBuffer[inputIndex + 1]);

        inputIndex += 2;
        outputIndex++;
    }

    // Note: if the payload has an odd length, the last char is discarded

    this->_receivedPayloadSize = outputIndex;
    this->_packetReceived = true; // enable receive() again

    if ((this->_receivedPayloadSize > 0) && _receiveCallback) {
        _receiveCallback((const uint8_t*)_receivedPayloadBuffer, this->_receivedPayloadSize); // TODO fix uint8_t vs char
    }

    return NoError;
}

#ifdef DEBUG
int freeRam()
{
    extern int __heap_start;
    extern int* __brkval;
    int v;
    return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}
#endif

// Provides a quick test of several methods as a pseudo-unit test.
void Sodaq_RN2483::runTestSequence(SerialType& loraStream, Stream& debugStream)
{
#ifdef DEBUG
    debugPrint("free ram: ");
    debugPrintLn(freeRam());

    init(loraStream);

    this->_loraStream = &loraStream;
    this->_diagStream = &debugStream;

    // expectString
#if 0
    debugPrintLn("write \"testString\" and then CRLF");

    if (expectString("testString", 5000)) {
        debugPrintLn("[expectString] positive case works!");
    }

    debugPrintLn("");
    debugPrintLn("write something other than \"testString\" and then CRLF");

    if (!expectString("testString", 5000)) {
        debugPrintLn("[expectString] negative case works!");
    }

#endif

#if 0
    debugPrint("free ram: ");
    debugPrintLn(freeRam());

    // setMacParam(array)
    debugPrintLn("");
    debugPrintLn("");
    uint8_t testValue[] = { 0x01, 0x02, 0xDE, 0xAD, 0xBE, 0xEF };
    setMacParam("testParam ", testValue, ARRAY_SIZE(testValue));

    // macTransmit
    debugPrintLn("");
    debugPrintLn("");
    uint8_t testValue2[] = { 0x01, 0x02, 0xDE, 0xAD, 0xBE, 0xEF };
    macTransmit(STR_CONFIRMED, 1, testValue2, ARRAY_SIZE(testValue2));

    debugPrint("free ram: ");
    debugPrintLn(freeRam());
#endif

#if 0
    {
        // onMacRX
        debugPrintLn("");
        debugPrintLn("==== onMacRX");
        char mockRx[] = "mac_rx 1 313233343536373839";
        uint8_t passArray[] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39 };
        size_t len = strlen(mockRx) + 1;
        strncpy(this->_inputBuffer, mockRx, len);
        _inputBuffer[len] = '\0';
        this->_packetReceived = false;// reset
        debugPrint("Input buffer now is: ");
        debugPrintLn(this->_inputBuffer);
        debugPrint("onMacRX result code: ");
        debugPrintLn(onMacRX());

        if (memcmp(_receivedPayloadBuffer, passArray, _receivedPayloadSize) == 0) {
            debugPrintLn("PASS");
        }
        else {
            debugPrintLn("FAIL");
        }
    }
#endif

#if 0
    {
        // receive
        debugPrintLn("");
        debugPrintLn("==== receive");
        uint8_t mockResult[] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x00 };
        memcpy(this->_receivedPayloadBuffer, mockResult, sizeof(mockResult));
        _receivedPayloadSize = sizeof(mockResult);
        uint8_t payload[64];
        debugPrintLn("* without having received packet");
        uint8_t length = receive(payload, sizeof(payload));
        debugPrintLn(reinterpret_cast<char*>(payload));
        debugPrint("Length: ");
        debugPrintLn(length);
        debugPrintLn("* having received packet");
        this->_packetReceived = true;
        length = receive(payload, sizeof(payload));
        debugPrintLn(reinterpret_cast<char*>(payload));
        debugPrint("Length: ");
        debugPrintLn(length);
    }
#endif

#if 0
    // lookup error
    debugPrintLn("");
    debugPrintLn("");

    debugPrint("empty string: ");
    debugPrintLn((lookupMacTransmitError("") == NoResponse) ? "passed" : "wrong");

    debugPrint("\"random\": ");
    debugPrintLn((lookupMacTransmitError("random") == NoResponse) ? "passed" : "wrong");

    debugPrint("\"invalid_param\": ");
    debugPrintLn((lookupMacTransmitError("invalid_param") == InternalError) ? "passed" : "wrong");

    debugPrint("\"not_joined\": ");
    debugPrintLn((lookupMacTransmitError("not_joined") == NotConnected) ? "passed" : "wrong");

    debugPrint("\"busy\": ");
    debugPrintLn((lookupMacTransmitError("busy") == Busy) ? "passed" : "wrong");

    debugPrint("\"invalid_param\": ");
    debugPrintLn((lookupMacTransmitError("invalid_param") == InternalError) ? "passed" : "wrong");

    debugPrint("free ram: ");
    debugPrintLn(freeRam());
#endif
#else
    (void)loraStream;
    (void)debugStream;
#endif
}
