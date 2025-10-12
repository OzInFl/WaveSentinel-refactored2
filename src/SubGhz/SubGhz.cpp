#include "SubGhz.h"

// CC1101 Default Settings
float CC1101_MHZ = 433.92;
int CC1101_MODULATION = 2; // Modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK
float CC1101_DRATE = 3.79372;
float CC1101_DEVIATION = 1.58;
float CC1101_RX_BW = 650.00;
int CC1101_PKT_FORMAT = 3; // Format of RX and TX data. 0=Normal mode, use FIFOs for RX and TX
                           //                           1=Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins
                           //                           2=Random TX test mode; sends random data using PN9 generator. Works as normal mode, setting 0 in RX
                           //                           3=Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins
CC1101Preset CC1101_PRESET = CUSTOM;

// CC1101 - RCSW Apps Stuff
RCSwitch mySwitch = RCSwitch();
long tempValue = 0;
int tempBitLength = 0;
int tempDelay = 0;
int tempProtocol = 0;

// CC1101 - Scanner Stuff
float start_freq = 433;
float stop_freq = 434;
float freq = start_freq;
long compare_freq;
float mark_freq;
int rssi;
int mark_rssi = -100;

// ---------------------------------------------------------------------
// TESLA
// ---------------------------------------------------------------------
static const uint16_t pulseWidth = 400;
static const uint16_t messageDistance = 23;
static const uint8_t messageLength = 43;
static const uint8_t sequence[messageLength] = {
    0x02, 0xAA, 0xAA, 0xAA, // Preamble of 26 bits by repeating 1010
    0x2B,                   // Sync byte
    0x2C, 0xCB, 0x33, 0x33, 0x2D, 0x34, 0xB5, 0x2B, 0x4D, 0x32, 0xAD, 0x2C, 0x56, 0x59, 0x96, 0x66,
    0x66, 0x5A, 0x69, 0x6A, 0x56, 0x9A, 0x65, 0x5A, 0x58, 0xAC, 0xB3, 0x2C, 0xCC, 0xCC, 0xB4, 0xD2,
    0xD4, 0xAD, 0x34, 0xCA, 0xB4, 0xA0};

// ---------------------------------------------------------------------
// RAW
// ---------------------------------------------------------------------
#define SAMPLE_SIZE 4096

int receiverGPIO;

const int minsample = 30;
static unsigned long lastTime = 0;

int sample[SAMPLE_SIZE];
int samplecount;

bool receiverEnabled = false;

// ---------------------------------------------------------------------
// bool SubGhz::init()
// ---------------------------------------------------------------------
bool SubGhz::init()
{
    ELECHOUSE_cc1101.setSpiPin(CC1101_SCLK, CC1101_MISO, CC1101_MOSI, CC1101_CS);

    ELECHOUSE_cc1101.Init();

    // Check the CC1101 Spi connection.
    if (ELECHOUSE_cc1101.getCC1101())
    {
        ELECHOUSE_cc1101.setSidle();
        return true;
    }
    else
    {
        ELECHOUSE_cc1101.setSidle();
        return false;
    }
}

// ---------------------------------------------------------------------
// bool CheckReceived(void)
// ---------------------------------------------------------------------
bool CheckReceived(void)
{
    delay(1);
    if (samplecount >= minsample && micros() - lastTime > 100000)
    {
        receiverEnabled = false;
        return 1;
    }
    else
    {
        return 0;
    }
}

// ---------------------------------------------------------------------
// void IRAM_ATTR InterruptHandler()
// ---------------------------------------------------------------------
void IRAM_ATTR InterruptHandler()
{    
    if (!receiverEnabled)
    {
        return;
    }
    
    const long time = micros();
    const unsigned int duration = time - lastTime;

    if (duration > 100000)
    {
        samplecount = 0;
    }

    if (duration >= 100)
    {
        sample[samplecount++] = duration;
    }

    if (samplecount >= SAMPLE_SIZE)
    {
        return;
    }

    if (CC1101_MODULATION == 0)
    {
        if (samplecount == 1 && digitalRead(receiverGPIO) != HIGH)
        {
            samplecount = 0;
        }
    }

    lastTime = time;
}

// ---------------------------------------------------------------------
// void SubGhz::setPreset(CC1101Preset preset)
// ---------------------------------------------------------------------
void SubGhz::setPreset(CC1101Preset preset)
{
    switch (preset)
    {
    case AM650:
        CC1101_MODULATION = 2;
        CC1101_DRATE = 3.79372;
        CC1101_RX_BW = 650.00;
        CC1101_DEVIATION = 1.58;
        CC1101_PRESET = preset;
        break;
    case AM270:
        CC1101_MODULATION = 2;
        CC1101_DRATE = 3.79372;
        CC1101_RX_BW = 270.833333;
        CC1101_DEVIATION = 1.58;
        CC1101_PRESET = preset;
        break;
    case FM238:
        CC1101_MODULATION = 0;
        CC1101_DRATE = 4.79794;
        CC1101_RX_BW = 270.833333;
        CC1101_DEVIATION = 2.380371;
        CC1101_PRESET = preset;
        break;
    case FM476:
        CC1101_MODULATION = 0;
        CC1101_DRATE = 4.79794;
        CC1101_RX_BW = 270.833333;
        CC1101_DEVIATION = 47.60742;
        CC1101_PRESET = preset;
        break;
    case CUSTOM:
        CC1101_PRESET = preset;
    default:
        break;
    }
}

// ---------------------------------------------------------------------
// void SubGhz::setPacketFormat(int packetFormat)
// ---------------------------------------------------------------------
void SubGhz::setPacketFormat(int packetFormat)
{
    CC1101_PKT_FORMAT = packetFormat;
}

// ---------------------------------------------------------------------
// void SubGhz::setModulation(int modulation)
// ---------------------------------------------------------------------
void SubGhz::setModulation(int modulation)
{
    CC1101_MODULATION = modulation;
}

// ---------------------------------------------------------------------
// void SubGhz::setFrequency(float freq)
// ---------------------------------------------------------------------
void SubGhz::setFrequency(float freq)
{
    CC1101_MHZ = freq;
    ELECHOUSE_cc1101.setMHZ(CC1101_MHZ);
}

// ---------------------------------------------------------------------
// float SubGhz::getFrequency()
// ---------------------------------------------------------------------
float SubGhz::getFrequency()
{
    return CC1101_MHZ;
}

// ---------------------------------------------------------------------
// void SubGhz::enableRCSwitch()
// ---------------------------------------------------------------------
void SubGhz::enableRCSwitch()
{
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(CC1101_MHZ);               // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
    ELECHOUSE_cc1101.setModulation(CC1101_MODULATION); // set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    ELECHOUSE_cc1101.setDeviation(CC1101_DEVIATION);
    ELECHOUSE_cc1101.setDRate(CC1101_DRATE); // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
    ELECHOUSE_cc1101.setRxBW(CC1101_RX_BW);  // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
    ELECHOUSE_cc1101.setDcFilterOff(1);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setPktFormat(CC1101_PKT_FORMAT); // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX.
                                                      // 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX.
                                                      // 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // ELECHOUSE_cc1101.setSyncMode(3);        // Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.

    ELECHOUSE_cc1101.setPA(12);

    ELECHOUSE_cc1101.SetRx(); // set Receive on
    pinMode(CC1101_GDO0, INPUT);
    mySwitch.enableReceive(CC1101_GDO0); // Receiver on
}

// ---------------------------------------------------------------------
// void SubGhz::disableRCSwitch()
// ---------------------------------------------------------------------
void SubGhz::disableRCSwitch()
{
    mySwitch.disableReceive();
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.goSleep();
}

// ---------------------------------------------------------------------
// void SubGhz::enableReceiver()
// ---------------------------------------------------------------------
void SubGhz::enableReceiver()
{
        // Reset Current
    memset(sample, 0, sizeof(SAMPLE_SIZE));
    samplecount = 0;

    ELECHOUSE_cc1101.Init();

    if (CC1101_MODULATION == 2)
    {
        ELECHOUSE_cc1101.setDcFilterOff(0);
    }

    if (CC1101_MODULATION == 0)
    {
        ELECHOUSE_cc1101.setDcFilterOff(1);
    }

    // ELECHOUSE_cc1101.setDcFilterOff(1);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setPktFormat(CC1101_PKT_FORMAT); // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX.
                                                      // 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX.
                                                      // 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // ELECHOUSE_cc1101.setSyncMode(3);        // Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.
    ELECHOUSE_cc1101.setModulation(CC1101_MODULATION); // set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    ELECHOUSE_cc1101.setMHZ(CC1101_MHZ);               // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
    ELECHOUSE_cc1101.setDeviation(CC1101_DEVIATION);
    ELECHOUSE_cc1101.setDRate(CC1101_DRATE); // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
    ELECHOUSE_cc1101.setRxBW(CC1101_RX_BW);  // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
    
    pinMode(CC1101_GDO0, INPUT);

    receiverGPIO = digitalPinToInterrupt(CC1101_GDO0);
    
    ELECHOUSE_cc1101.SetRx(); // set Receive on

    receiverEnabled = true;

    attachInterrupt(receiverGPIO, InterruptHandler, CHANGE);
}

// ---------------------------------------------------------------------
// void SubGhz::disableReceiver()
// ---------------------------------------------------------------------
void SubGhz::disableReceiver()
{
    //detachInterrupt((uint8_t)receiverGPIO);
    receiverEnabled = false;
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.goSleep();
}

// ---------------------------------------------------------------------
// void SubGhz::enableTransmit()
// ---------------------------------------------------------------------
void SubGhz::enableTransmit()
{
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(CC1101_MHZ);               // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
    ELECHOUSE_cc1101.setModulation(CC1101_MODULATION); // set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    ELECHOUSE_cc1101.setDeviation(CC1101_DEVIATION);
    ELECHOUSE_cc1101.setDRate(CC1101_DRATE); // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
    ELECHOUSE_cc1101.setDcFilterOff(1);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setPktFormat(CC1101_PKT_FORMAT); // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX.
                                                      // 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX.
                                                      // 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // ELECHOUSE_cc1101.setSyncMode(3);        // Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.

    ELECHOUSE_cc1101.setPA(12);

    ELECHOUSE_cc1101.SetTx(); // set Transmit on

    mySwitch.enableTransmit(CC1101_GDO0);
}

// ---------------------------------------------------------------------
// void SubGhz::disableTransmit()
// ---------------------------------------------------------------------
void SubGhz::disableTransmit()
{
    digitalWrite(CC1101_GDO0, LOW);
    mySwitch.disableTransmit(); // set Transmit off
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.goSleep();
}

// ---------------------------------------------------------------------
// void SubGhz::enableScanner()
// ---------------------------------------------------------------------
void SubGhz::enableScanner(float start, float stop)
{
    start_freq = start;
    stop_freq = stop;
    freq = start_freq;

    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(freq);                     // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
    ELECHOUSE_cc1101.setModulation(CC1101_MODULATION); // set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    ELECHOUSE_cc1101.setDeviation(CC1101_DEVIATION);
    ELECHOUSE_cc1101.setDRate(CC1101_DRATE); // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
    ELECHOUSE_cc1101.setRxBW(CC1101_RX_BW);  // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
    ELECHOUSE_cc1101.setDcFilterOff(1);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setPktFormat(CC1101_PKT_FORMAT); // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX.
                                                      // 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX.
                                                      // 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // ELECHOUSE_cc1101.setSyncMode(3);        // Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.

    ELECHOUSE_cc1101.setPA(12);

    ELECHOUSE_cc1101.SetRx(); // Set Receive on
}

// ---------------------------------------------------------------------
// void SubGhz::disableScanner()
// ---------------------------------------------------------------------
void SubGhz::disableScanner()
{
    ELECHOUSE_cc1101.setSidle();
}

// ---------------------------------------------------------------------
// void SubGhz::switchOn(const char *sGroup, const char *sDevice)
// ---------------------------------------------------------------------
void SubGhz::switchOn(const char *sGroup, const char *sDevice)
{
    mySwitch.switchOn(String(sGroup).c_str(), String(sDevice).c_str());
}

// ---------------------------------------------------------------------
// void SubGhz::switchOff(const char *sGroup, const char *sDevice)
// ---------------------------------------------------------------------
void SubGhz::switchOff(const char *sGroup, const char *sDevice)
{
    mySwitch.switchOff(String(sGroup).c_str(), String(sDevice).c_str());
}

// ---------------------------------------------------------------------
// void SubGhz::sendLastSignal()
// ---------------------------------------------------------------------
void SubGhz::sendLastSignal()
{
    mySwitch.setProtocol(tempProtocol);      // send Received Protocol
    mySwitch.setPulseLength(tempDelay);      // send Received Delay
    mySwitch.send(tempValue, tempBitLength); // send Received value/bits
}

String SubGhz::generateRandomString(int length)
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    const std::string characters = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    std::stringstream ss;
    for (int i = 0; i < length; ++i) {
        int randomIndex = std::rand() % characters.size();
        char randomChar = characters[randomIndex];
        ss << randomChar;
    }

    return String(ss.str().c_str());
}

String SubGhz::generateFilename(float frequency, int modulation, float bandwidth)
{
    char filenameBuffer[100];

    sprintf(filenameBuffer, "%d_%s_%d_%s.sub", static_cast<int>(frequency * 100), modulation == 2 ? "AM" : "FM", static_cast<int>(bandwidth),
            generateRandomString(8).c_str());

    return String(filenameBuffer);
}




// ---------------------------------------------------------------------
// bool SubGhz::CaptureLoop()
// ---------------------------------------------------------------------
bool SubGhz::CaptureLoop()
{
    if (CheckReceived())
    {
        Print_Debug("CaptureLoop()");

        String rawString = "";

        for (int i = 1; i < samplecount; i++)
        {
            rawString += sample[i];
            rawString += ",";
        }

        Print_Debug(String(String("New Signal RAW: \n") + String(rawString)).c_str());

        rawString = "";

        return true;
    }
    else
    {
        return false;
    }
}

bool SubGhz::CaptureLoopSD()
{
    File outputFile;
    if (CheckReceived())
    {
        Print_Debug("CaptureLoop()");

       std::stringstream rawSignal;

        for (int i = 0; i < samplecount; i++) {
            rawSignal << (i > 0 ? (i % 2 == 1 ? " -" : " ") : "");
            rawSignal << sample[i];
        }
        
        if (SD.begin(SD_CS))
    {
        String filename = generateFilename(CC1101_MHZ, CC1101_MODULATION, CC1101_RX_BW);
        String fullPath = "/subghz/captures/" + filename; 
        outputFile = SD.open(fullPath.c_str(), "w");
        if (outputFile) {
            std::vector<byte> customPresetData;
            if (CC1101_PRESET == CUSTOM) {
                customPresetData.insert(customPresetData.end(), {
                    CC1101_MDMCFG4, ELECHOUSE_cc1101.SpiReadReg(CC1101_MDMCFG4),
                    CC1101_MDMCFG3, ELECHOUSE_cc1101.SpiReadReg(CC1101_MDMCFG3),
                    CC1101_MDMCFG2, ELECHOUSE_cc1101.SpiReadReg(CC1101_MDMCFG2),
                    CC1101_DEVIATN, ELECHOUSE_cc1101.SpiReadReg(CC1101_DEVIATN),
                    CC1101_FREND0,  ELECHOUSE_cc1101.SpiReadReg(CC1101_FREND0),
                    0x00, 0x00
                });

                std::array<byte,8> paTable;
                ELECHOUSE_cc1101.SpiReadBurstReg(0x3E, paTable.data(), paTable.size());
                customPresetData.insert(customPresetData.end(), paTable.begin(), paTable.end());
            }
            FlipperSubFile::generateRaw(outputFile, CC1101_PRESET, customPresetData, rawSignal, CC1101_MHZ);
            outputFile.close();
        } else {
            // @todo: Log/send error
            return false;
        }
        return true;
    }
}
    else
    {
        return false;
    }
}
////----Alt ORig



// ---------------------------------------------------------------------
// bool SubGhz::ProtAnalyzerLoop()
// ---------------------------------------------------------------------
bool SubGhz::ProtAnalyzerLoop()
{
    if (mySwitch.available())
    {
        Print_Debug(String(String("New Signal Received, value: ") + String(mySwitch.getReceivedValue()) + String(" (") + String(mySwitch.getReceivedBitlength()) + String("bit) - Protocol: ") + String(mySwitch.getReceivedProtocol())).c_str());
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------
// void SubGhz::resetProtAnalyzer()
// ---------------------------------------------------------------------
void SubGhz::resetProtAnalyzer()
{
    mySwitch.resetAvailable();
}

// ---------------------------------------------------------------------
// void SubGhz::ScannerLoop()
// ---------------------------------------------------------------------
void SubGhz::ScannerLoop()
{
    // SubGhz::enableReceive();

    lv_textarea_set_cursor_click_pos(ui_txtScannerData, false);

    int rxBW = lv_arc_get_value(ui_arcScanBW);

    ELECHOUSE_cc1101.setRxBW(rxBW); // orig = 58
    ELECHOUSE_cc1101.SetRx(freq);
    ELECHOUSE_cc1101.setMHZ(freq);
    rssi = ELECHOUSE_cc1101.getRssi();

    if (rssi > -200)
    {
        if (rssi > mark_rssi)
        {
            mark_rssi = rssi;
            mark_freq = freq;
        }
    }

    freq += 0.10;

    if (freq > stop_freq)
    {
        freq = start_freq;
        String threshVal = lv_label_get_text(ui_lblThreshold);
        int thVal = threshVal.toInt();

        // map(threshVal,-40,-80,40,80);
        if (mark_rssi > thVal)
        {
            long fr = mark_freq * 100;

            if (fr == compare_freq)
            {
                lv_textarea_add_text(ui_txtScannerData, String(String(mark_freq) + String(" MHZ | RSSI: ") + String(mark_rssi) + String("\n\0")).c_str());
                mark_rssi = -100;
                compare_freq = 0;
                mark_freq = 0;
            }
            else
            {
                compare_freq = mark_freq * 100;
                freq = mark_freq - 0.10;
                mark_freq = 0;
                mark_rssi = -100;
            }
        }
    }
}

// ---------------------------------------------------------------------
// void SubGhz::GeneratorLoop()
// ---------------------------------------------------------------------
void SubGhz::GeneratorLoop()
{
    digitalWrite(CC1101_GDO0, HIGH);
    delayMicroseconds(255);
    digitalWrite(CC1101_GDO0, LOW);
    delayMicroseconds(255);
}

// ---------------------------------------------------------------------
// void SubGhz::showResultProtAnalyzer()
// ---------------------------------------------------------------------
void SubGhz::showResultProtAnalyzer()
{
    tempValue = mySwitch.getReceivedValue();
    tempBitLength = mySwitch.getReceivedBitlength();
    tempDelay = mySwitch.getReceivedDelay();
    tempProtocol = mySwitch.getReceivedProtocol();

    const char *b = SubGhz::dec2binWzerofill(tempValue, tempBitLength);

    lv_textarea_set_text(ui_txtProtAnaReceived, String(tempValue).c_str());                      // Decimal Value
    lv_textarea_set_text(ui_txtProtAnaBitLength, String(tempBitLength).c_str());                 // Bit Legnth
    lv_textarea_set_text(ui_txtProtAnaBinary, String(b).c_str());                                // Binary
    lv_textarea_set_text(ui_txtProtAnaPulsLen, String(tempDelay).c_str());                       // Pulse Length
    lv_textarea_set_text(ui_txtProtAnaProtAnaTriState, String(SubGhz::bin2tristate(b)).c_str()); // TriState
    lv_textarea_set_text(ui_txtProtAnaProtocol, String(tempProtocol).c_str());                   // Protocol

    String rawString = "";

    for (unsigned int i = 0; i <= tempBitLength * 2; i++)
    {
        rawString += mySwitch.getReceivedRawdata()[i];
        rawString += ",";
    }

    lv_textarea_set_text(ui_txtProtAnaResults, String(rawString).c_str());

    rawString = "";
}

// ---------------------------------------------------------------------
// void SubGhz::showResultRecPlay()
// ---------------------------------------------------------------------
void SubGhz::showResultRecPlay()
{
    lv_obj_clear_flag(ui_indGreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_indRed, LV_OBJ_FLAG_HIDDEN);

    Print_Debug(String(String("New Signal RAW, Sample Count: ") + String(samplecount)).c_str());

    String rawString = "";

    for (int i = 1; i < samplecount; i++)
    {
        Printn_Debug(String(sample[i]).c_str());
        Printn_Debug(",");
        rawString += sample[i];
        rawString += ",";
    }

    lv_textarea_set_text(ui_txtRawData, String(rawString).c_str());

    lv_label_set_text(ui_lblRecPlayStatus, String("Capture Complete | Sample: " + String(samplecount)).c_str());
    Print_Debug(String("Capture Complete | Sample: " + String(samplecount)).c_str());

    rawString = "";
}

// ---------------------------------------------------------------------
// static const char *SubGhz::bin2tristate(const char *bin)
// ---------------------------------------------------------------------
const char *SubGhz::bin2tristate(const char *bin)
{
    static char returnValue[50];
    int pos = 0;
    int pos2 = 0;
    while (bin[pos] != '\0' && bin[pos + 1] != '\0')
    {
        if (bin[pos] == '0' && bin[pos + 1] == '0')
        {
            returnValue[pos2] = '0';
        }
        else if (bin[pos] == '1' && bin[pos + 1] == '1')
        {
            returnValue[pos2] = '1';
        }
        else if (bin[pos] == '0' && bin[pos + 1] == '1')
        {
            returnValue[pos2] = 'F';
        }
        else
        {
            return "N/A";
        }
        pos = pos + 2;
        pos2++;
    }
    returnValue[pos2] = '\0';
    return returnValue;
}

// ---------------------------------------------------------------------
// static char *SubGhz::dec2binWzerofill(unsigned long Dec, unsigned int bitLength)
// ---------------------------------------------------------------------
char *SubGhz::dec2binWzerofill(unsigned long Dec, unsigned int bitLength)
{
    static char bin[64];
    unsigned int i = 0;

    while (Dec > 0)
    {
        bin[32 + i++] = ((Dec & 1) > 0) ? '1' : '0';
        Dec = Dec >> 1;
    }

    for (unsigned int j = 0; j < bitLength; j++)
    {
        if (j >= bitLength - i)
        {
            bin[j] = bin[31 + i - (j - (bitLength - i))];
        }
        else
        {
            bin[j] = '0';
        }
    }
    bin[bitLength] = '\0';

    return bin;
}

// ---------------------------------------------------------------------
// bool SubGhz::send_tesla()
// ---------------------------------------------------------------------
bool SubGhz::send_tesla()
{
    for (uint8_t t = 0; t < 3; t++)
    {
        for (uint8_t i = 0; i < messageLength; i++)
            SubGhz::send_byte(sequence[i]);
        digitalWrite(CC1101_GDO0, LOW);
        delay(messageDistance);
    }

    return true;
}

// ---------------------------------------------------------------------
// void SubGhz::send_byte(uint8_t dataByte) {
// ---------------------------------------------------------------------
void SubGhz::send_byte(uint8_t dataByte)
{
    for (int8_t bit = 7; bit >= 0; bit--)
    { // MSB
        digitalWrite(CC1101_GDO0, (dataByte & (1 << bit)) != 0 ? HIGH : LOW);
        delayMicroseconds(pulseWidth);
    }
}

// ---------------------------------------------------------------------
// void SubGhz::sendSamples(int samples[], int samplesLength)
// ---------------------------------------------------------------------
void SubGhz::sendSamples(int samples[], int samplesLength)
{
    int delay = 0;
    unsigned long time;
    byte n = 0;

    for (int i = 0; i < samplesLength; i++)
    {
        // TRANSMIT
        n = 1;
        delay = samples[i];
        if (delay < 0)
        {
            // DONT TRANSMIT
            delay = delay * -1;
            n = 0;
        }

        digitalWrite(CC1101_GDO0, n);

        delayMicroseconds(delay);
    }

    // STOP TRANSMITTING
    digitalWrite(CC1101_GDO0, 0);
}

// ---------------------------------------------------------------------
// bool SubGhz::sendCapture()
// ---------------------------------------------------------------------
bool SubGhz::sendCapture()
{
    SubGhz::enableTransmit();

    for (int i = 1; i < samplecount; i += 2)
    {
        digitalWrite(CC1101_GDO0, HIGH);
        delayMicroseconds(sample[i]);
        digitalWrite(CC1101_GDO0, LOW);
        delayMicroseconds(sample[i + 1]);
    }

    SubGhz::disableTransmit();

    lv_label_set_text(ui_lblRecPlayStatus, String("Playback Complete ! Sample: " + String(samplecount)).c_str());

    return true;
}


