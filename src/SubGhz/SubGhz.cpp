#include "SubGhz.h"
#include "ProtocolID.h"
#include "WorldMode.h"
#include "Display/ScannerScreen.h"
#include <SPI.h>

// External main-loop state so we can refuse RAW capture while scanner is running.
// (Event.h defines this at file scope, so we forward-declare it here instead of
// including Event.h to avoid multiple-definition link errors.)
extern uint8_t currentState;
// Mirror of STATE_SCANNER enum value from Display/Event.h (WaveSentinelState).
// If the enum order changes in Event.h, update this constant too.
static const uint8_t kStateScanner = 3;

// SD card uses a dedicated SPI bus (HSPI) defined in SDCard.h
extern SPIClass sdSPI;

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
int CC1101_PA = 12;        // TX power in dBm. Common values: -30,-20,-15,-10,-6,0,5,7,10,12
int CC1101_SYNC_MODE = 0;  // 0=No sync, 1=16bit, 2=16/16, 3=30/32, 4-7=with carrier-sense
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

// Forward declarations for RAW capture state — defined later in this file —
// so defensive cleanup at RX-mode entry points (enableScanner, enableReceiver,
// enableRCSwitch, enableTransmit, CaptureLoopSD) can tear down a lingering
// ISR / running flag before reprogramming the radio and SPI bus.
static volatile bool        s_rawRunning = false;
static int                  s_rawIsrPin  = -1;
static portMUX_TYPE         s_rawMux     = portMUX_INITIALIZER_UNLOCKED;

// Centralized defensive teardown: detach the GDO0 pin-change ISR (if any),
// clear the running flag, and reset edge bookkeeping. Safe to call even
// when no capture is in flight — detachInterrupt is idempotent.
static void rawCaptureForceStop()
{
    if (s_rawIsrPin >= 0) {
        detachInterrupt(s_rawIsrPin);
    }
    portENTER_CRITICAL(&s_rawMux);
    s_rawRunning = false;
    portEXIT_CRITICAL(&s_rawMux);
}

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
    vTaskDelay(1);  // Yield to RTOS scheduler instead of blocking delay
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
// void SubGhz::setRxBandwidth(float bw)
// ---------------------------------------------------------------------
void SubGhz::setRxBandwidth(float bw)
{
    CC1101_RX_BW = bw;
}

// ---------------------------------------------------------------------
// void SubGhz::setDeviation(float dev)
// ---------------------------------------------------------------------
void SubGhz::setDeviation(float dev)
{
    CC1101_DEVIATION = dev;
}

// ---------------------------------------------------------------------
// void SubGhz::setDataRate(float drate)
// ---------------------------------------------------------------------
void SubGhz::setDataRate(float drate)
{
    CC1101_DRATE = drate;
}

// ---------------------------------------------------------------------
// void SubGhz::setPower(int pa)
// ---------------------------------------------------------------------
void SubGhz::setPower(int pa)
{
    CC1101_PA = pa;
}

// ---------------------------------------------------------------------
// void SubGhz::setSyncMode(int mode)
// ---------------------------------------------------------------------
void SubGhz::setSyncMode(int mode)
{
    CC1101_SYNC_MODE = mode;
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
    rawCaptureForceStop();   // defensive: drop any stale Read RAW ISR

    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(CC1101_MHZ);               // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
    ELECHOUSE_cc1101.setModulation(CC1101_MODULATION); // set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    ELECHOUSE_cc1101.setDeviation(CC1101_DEVIATION);
    ELECHOUSE_cc1101.setDRate(CC1101_DRATE); // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
    ELECHOUSE_cc1101.setRxBW(CC1101_RX_BW);  // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
    ELECHOUSE_cc1101.setDcFilterOff(1);
    ELECHOUSE_cc1101.setSyncMode(CC1101_SYNC_MODE);
    ELECHOUSE_cc1101.setPktFormat(CC1101_PKT_FORMAT); // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX.
                                                      // 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX.
                                                      // 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // ELECHOUSE_cc1101.setSyncMode(3);        // Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.

    ELECHOUSE_cc1101.setPA(CC1101_PA);

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
    // Defensive: tear down any lingering Read RAW pin-change ISR before we
    // bind our own InterruptHandler to the same GDO0 pin.
    rawCaptureForceStop();

    // Reset capture buffer — sizeof(sample) gives full array size (4096 * sizeof(int)).
    // BUG FIX: was sizeof(SAMPLE_SIZE) which is sizeof(int)=4, only zeroing 4 bytes!
    memset(sample, 0, sizeof(sample));
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
    ELECHOUSE_cc1101.setSyncMode(CC1101_SYNC_MODE);
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
    rawCaptureForceStop();   // defensive: GDO0 is about to be driven as output

    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(CC1101_MHZ);               // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
    ELECHOUSE_cc1101.setModulation(CC1101_MODULATION); // set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    ELECHOUSE_cc1101.setDeviation(CC1101_DEVIATION);
    ELECHOUSE_cc1101.setDRate(CC1101_DRATE); // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
    ELECHOUSE_cc1101.setDcFilterOff(1);
    ELECHOUSE_cc1101.setSyncMode(CC1101_SYNC_MODE);
    ELECHOUSE_cc1101.setPktFormat(CC1101_PKT_FORMAT); // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX.
                                                      // 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX.
                                                      // 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // ELECHOUSE_cc1101.setSyncMode(3);        // Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.

    ELECHOUSE_cc1101.setPA(CC1101_PA);

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
// void SubGhz::initScannerScreen() — wipe SquareLine widgets, build new UI.
// Called once from setup() so the Scanner tab is ready before user enters.
// ---------------------------------------------------------------------
void SubGhz::initScannerScreen()
{
    scanner_init();
}

// ---------------------------------------------------------------------
// void SubGhz::enableScanner()
// ---------------------------------------------------------------------
void SubGhz::enableScanner(float start, float stop)
{
    // Region gate — bail out before touching the radio if either endpoint
    // is outside the current region's allowed CC1101 bands.
    if (!WorldMode::freqAllowed(start) || !WorldMode::freqAllowed(stop)) {
        Serial.printf("[SubGhz] freq %.2f MHz outside region\n", start);
        return;
    }

    // Defensive: a Read RAW capture left its pin-change ISR attached on
    // CC1101_GDO0 will fire while we drive SPI register writes below, which
    // can corrupt SPI transactions and trip the WDT → reboot. Tear it down
    // unconditionally so we can't enter scanner mode with a stale ISR bound.
    rawCaptureForceStop();

    start_freq = start;
    stop_freq = stop;
    freq = start_freq;

    // Initialize scanner screen on first use
    scanner_init();

    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(freq);                     // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
    ELECHOUSE_cc1101.setModulation(CC1101_MODULATION); // set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    ELECHOUSE_cc1101.setDeviation(CC1101_DEVIATION);
    ELECHOUSE_cc1101.setDRate(CC1101_DRATE); // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
    ELECHOUSE_cc1101.setRxBW(CC1101_RX_BW);  // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
    ELECHOUSE_cc1101.setDcFilterOff(1);
    ELECHOUSE_cc1101.setSyncMode(CC1101_SYNC_MODE);
    ELECHOUSE_cc1101.setPktFormat(CC1101_PKT_FORMAT); // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX.
                                                      // 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX.
                                                      // 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
                                                      // ELECHOUSE_cc1101.setSyncMode(3);        // Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.

    ELECHOUSE_cc1101.setPA(CC1101_PA);

    ELECHOUSE_cc1101.SetRx(); // Set Receive on
}

// (ScannerScreen.h moved to top of file)

// ---------------------------------------------------------------------
// void SubGhz::disableScanner()
// ---------------------------------------------------------------------
void SubGhz::disableScanner()
{
    ELECHOUSE_cc1101.setSidle();
    scn.running = false;
    scanner_update_display();
}

// ---------------------------------------------------------------------
// void SubGhz::switchOn(const char *sGroup, const char *sDevice)
// ---------------------------------------------------------------------
void SubGhz::switchOn(const char *sGroup, const char *sDevice)
{
    mySwitch.switchOn(sGroup, sDevice);
}

// ---------------------------------------------------------------------
// void SubGhz::switchOff(const char *sGroup, const char *sDevice)
// ---------------------------------------------------------------------
void SubGhz::switchOff(const char *sGroup, const char *sDevice)
{
    mySwitch.switchOff(sGroup, sDevice);
}

// ---------------------------------------------------------------------
// Type B (Rotary)
// ---------------------------------------------------------------------
void SubGhz::switchOnB(int nAddress, int nChannel)
{
    mySwitch.switchOn(nAddress, nChannel);
}
void SubGhz::switchOffB(int nAddress, int nChannel)
{
    mySwitch.switchOff(nAddress, nChannel);
}

// ---------------------------------------------------------------------
// Type C (Intertechno)
// ---------------------------------------------------------------------
void SubGhz::switchOnC(char sFamily, int nGroup, int nDevice)
{
    mySwitch.switchOn(sFamily, nGroup, nDevice);
}
void SubGhz::switchOffC(char sFamily, int nGroup, int nDevice)
{
    mySwitch.switchOff(sFamily, nGroup, nDevice);
}

// ---------------------------------------------------------------------
// Type D (REV)
// ---------------------------------------------------------------------
void SubGhz::switchOnD(char sGroup, int nDevice)
{
    mySwitch.switchOn(sGroup, nDevice);
}
void SubGhz::switchOffD(char sGroup, int nDevice)
{
    mySwitch.switchOff(sGroup, nDevice);
}

// ---------------------------------------------------------------------
// Raw code send
// ---------------------------------------------------------------------
void SubGhz::sendRaw(unsigned long code, unsigned int bitLength, int protocol, int pulseLength, int repeatCount)
{
    mySwitch.setProtocol(protocol);
    mySwitch.setPulseLength(pulseLength);
    mySwitch.setRepeatTransmit(repeatCount);
    mySwitch.send(code, bitLength);
    mySwitch.setRepeatTransmit(10);  // reset default
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

void SubGhz::generateRandomString(char* buf, size_t bufSize, int length)
{
    static const char characters[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    int maxLen = (length < (int)bufSize - 1) ? length : (int)bufSize - 1;
    for (int i = 0; i < maxLen; ++i) {
        buf[i] = characters[std::rand() % (sizeof(characters) - 1)];
    }
    buf[maxLen] = '\0';
}

void SubGhz::generateFilename(char* buf, size_t bufSize, float frequency, int modulation, float bandwidth)
{
    char rnd[9];
    generateRandomString(rnd, sizeof(rnd), 8);
    snprintf(buf, bufSize, "%d_%s_%d_%s.sub",
             (int)(frequency * 100), modulation == 2 ? "AM" : "FM",
             (int)bandwidth, rnd);
}

void SubGhz::getDefaultFilename(char* buf, size_t bufSize)
{
    char rnd[9];
    generateRandomString(rnd, sizeof(rnd), 8);
    snprintf(buf, bufSize, "%d_%s_%d_%s",
             (int)(CC1101_MHZ * 100), CC1101_MODULATION == 2 ? "AM" : "FM",
             (int)CC1101_RX_BW, rnd);
}




// ---------------------------------------------------------------------
// bool SubGhz::CaptureLoop()
// ---------------------------------------------------------------------
bool SubGhz::CaptureLoop()
{
    if (CheckReceived())
    {
        char dbg[64];
        snprintf(dbg, sizeof(dbg), "CaptureLoop() - New Signal RAW, %d samples", samplecount);
        Print_Debug(dbg);
        return true;
    }
    return false;
}

bool SubGhz::CaptureLoopSD()
{
    // Defensive: if a Read RAW capture is still running here, it owns the
    // GDO0 ISR + the CC1101's SPI bus — colliding with our SD SPI work
    // below would crash. Tear it down before we touch the radio.
    if (s_rawRunning) {
        rawCaptureForceStop();
    }

    File outputFile;
    if (CheckReceived())
    {
        Print_Debug("CaptureLoop()");

       std::stringstream rawSignal;

        for (int i = 0; i < samplecount; i++) {
            rawSignal << (i > 0 ? (i % 2 == 1 ? " -" : " ") : "");
            rawSignal << sample[i];
        }
        
        sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
        if (SD.begin(SD_CS, sdSPI))
    {
        char filename[64];
        generateFilename(filename, sizeof(filename), CC1101_MHZ, CC1101_MODULATION, CC1101_RX_BW);
        char fullPath[96];
        snprintf(fullPath, sizeof(fullPath), "/captures/%s", filename);
        outputFile = SD.open(fullPath, FILE_WRITE, true);
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
            outputFile.flush();
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
// bool SubGhz::saveCaptureToSD()
// Saves already-captured sample[] data to SD card in Flipper .sub format
// ---------------------------------------------------------------------
bool SubGhz::saveCaptureToSD()
{
    char filename[64];
    generateFilename(filename, sizeof(filename), CC1101_MHZ, CC1101_MODULATION, CC1101_RX_BW);
    return saveCaptureToSD(filename);
}

bool SubGhz::saveCaptureToSD(const char* customFilename)
{
    if (samplecount < minsample) {
        lv_label_set_text(ui_lblRecPlayStatus, "No capture data to save");
        return false;
    }

    lv_label_set_text(ui_lblRecPlayStatus, "Saving to SD...");
    vTaskDelay(1); // yield to let refresh task update display

    // Init SD card on dedicated HSPI bus (separate from CC1101's default SPI)
    sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

    if (!SD.begin(SD_CS, sdSPI)) {
        lv_label_set_text(ui_lblRecPlayStatus, "SD Card not found");
        return false;
    }

    // Create directory if it doesn't exist
    if (!SD.exists("/captures")) {
        SD.mkdir("/captures");
    }

    char fullPath[96];
    snprintf(fullPath, sizeof(fullPath), "/captures/%s", customFilename);

    File outputFile = SD.open(fullPath, FILE_WRITE, true);
    if (!outputFile) {
        Serial.printf("Failed to create file: %s\n", fullPath);
        lv_label_set_text(ui_lblRecPlayStatus, "Failed to create file");
        SD.end();
        return false;
    }

    // Build raw signal data in Flipper format
    std::stringstream rawSignal;
    for (int i = 1; i < samplecount; i++) {
        if (i > 1) rawSignal << " ";
        if (i % 2 == 0) {
            rawSignal << "-" << sample[i];
        } else {
            rawSignal << sample[i];
        }
        if (i % 512 == 0) {
            vTaskDelay(1);
        }
    }

    // Build custom preset data if needed
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

        std::array<byte, 8> paTable;
        ELECHOUSE_cc1101.SpiReadBurstReg(0x3E, paTable.data(), paTable.size());
        customPresetData.insert(customPresetData.end(), paTable.begin(), paTable.end());
    }

    FlipperSubFile::generateRaw(outputFile, CC1101_PRESET, customPresetData, rawSignal, CC1101_MHZ);
    outputFile.flush();
    Serial.printf("File written: %s (%lu bytes)\n", fullPath, (unsigned long)outputFile.size());
    outputFile.close();
    SD.end();

    char statusBuf[80];
    snprintf(statusBuf, sizeof(statusBuf), "Saved: %s", customFilename);
    lv_label_set_text(ui_lblRecPlayStatus, statusBuf);
    return true;
}

// ---------------------------------------------------------------------
// bool SubGhz::ProtAnalyzerLoop()
// ---------------------------------------------------------------------
bool SubGhz::ProtAnalyzerLoop()
{
    if (mySwitch.available())
    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg), "New Signal Received, value: %lu (%dbit) - Protocol: %d",
                 mySwitch.getReceivedValue(), mySwitch.getReceivedBitlength(), mySwitch.getReceivedProtocol());
        Print_Debug(dbg);
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
// Flipper-style fast RSSI sweep with FFT bar graph display
// ---------------------------------------------------------------------
void SubGhz::ScannerLoop()
{
    scn.running = true;
    scn.start_freq = start_freq;
    scn.stop_freq = stop_freq;

    float range = stop_freq - start_freq;
    if (range < 0.01) range = 1.0;
    float step = range / FFT_BINS;

    // Fast RSSI sweep
    scn.peak_rssi = -128;
    scn.peak_freq = 0;

    for (int i = 0; i < FFT_BINS; i++) {
        float f = start_freq + (i * step);
        // SetRx(f) does setSidle + setMHZ + Calibrate + SRX strobe.
        // Need ~800us for cal + ~500us for AGC/RSSI to settle.
        ELECHOUSE_cc1101.SetRx(f);
        delayMicroseconds(1500);
        int r = ELECHOUSE_cc1101.getRssi();
        scn.rssi[i] = (int8_t)constrain(r, -128, 0);
        if (r > scn.peak_rssi) {
            scn.peak_rssi = r;
            scn.peak_freq = f;
        }
    }
    scn.sweep_count++;

    // Log peak every 8 sweeps so a USB serial monitor can verify RSSI flow
    if (scn.sweep_count % 8 == 0) {
        Serial.printf("[SCAN] sweep=%lu peak=%.2fMHz %ddBm\n",
            scn.sweep_count, scn.peak_freq, scn.peak_rssi);
    }

    // Push to LVGL only when the mutex is free; skip this frame otherwise.
    extern SemaphoreHandle_t lvgl_mutex;
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        scanner_update_display();
        xSemaphoreGive(lvgl_mutex);
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

    char numBuf[16];
    snprintf(numBuf, sizeof(numBuf), "%ld", tempValue);
    lv_textarea_set_text(ui_txtProtAnaReceived, numBuf);                                         // Decimal Value
    snprintf(numBuf, sizeof(numBuf), "%d", tempBitLength);
    lv_textarea_set_text(ui_txtProtAnaBitLength, numBuf);                                        // Bit Length
    lv_textarea_set_text(ui_txtProtAnaBinary, b);                                                // Binary
    snprintf(numBuf, sizeof(numBuf), "%d", tempDelay);
    lv_textarea_set_text(ui_txtProtAnaPulsLen, numBuf);                                          // Pulse Length
    lv_textarea_set_text(ui_txtProtAnaProtAnaTriState, SubGhz::bin2tristate(b));                 // TriState
    snprintf(numBuf, sizeof(numBuf), "%d", tempProtocol);
    lv_textarea_set_text(ui_txtProtAnaProtocol, numBuf);                                         // Protocol

    // Build raw data using pre-sized buffer instead of String concatenation
    unsigned int rawCount = tempBitLength * 2 + 2;
    size_t bufSize = rawCount * 12 + 1;
    char *rawBuf = (char *)malloc(bufSize);
    if (rawBuf) {
        size_t pos = 0;
        for (unsigned int i = 0; i < rawCount && pos < bufSize - 12; i++)
        {
            pos += snprintf(rawBuf + pos, bufSize - pos, "%u,", mySwitch.getReceivedRawdata()[i]);
        }
        lv_textarea_set_text(ui_txtProtAnaResults, rawBuf);
        free(rawBuf);
    }

    // Protocol identification from raw timing data
    int *rawSamples = (int *)malloc(rawCount * sizeof(int));
    if (rawSamples) {
        for (unsigned int i = 0; i < rawCount; i++) {
            rawSamples[i] = (int)mySwitch.getReceivedRawdata()[i];
        }
        ProtocolMatch match = identifyProtocol(rawSamples, rawCount);
        char protBuf[48];
        snprintf(protBuf, sizeof(protBuf), "Protocol: %s", match.name);
        lv_label_set_text(ui_lblProtAnaProtID, protBuf);
        free(rawSamples);
    }
}

// ---------------------------------------------------------------------
// void SubGhz::showResultRecPlay()
// ---------------------------------------------------------------------
void SubGhz::showResultRecPlay()
{
    lv_obj_clear_flag(ui_indGreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_indRed, LV_OBJ_FLAG_HIDDEN);

    char dbg[64];
    snprintf(dbg, sizeof(dbg), "New Signal RAW, Sample Count: %d", samplecount);
    Print_Debug(dbg);

    // Build raw data string using a pre-sized buffer instead of String concatenation
    // Max ~12 chars per sample ("−2147483648,") * samplecount
    size_t bufSize = (size_t)samplecount * 12 + 1;
    char *rawBuf = (char *)malloc(bufSize);
    if (rawBuf) {
        size_t pos = 0;
        for (int i = 1; i < samplecount && pos < bufSize - 12; i++)
        {
            pos += snprintf(rawBuf + pos, bufSize - pos, "%d,", sample[i]);
        }
        lv_textarea_set_text(ui_txtRawData, rawBuf);
        free(rawBuf);
    }

    // Identify protocol from captured samples
    ProtocolMatch match = identifyProtocol(sample, samplecount);
    char protBuf[48];
    snprintf(protBuf, sizeof(protBuf), "Protocol: %s", match.name);
    lv_label_set_text(ui_lblProtocolID, protBuf);

    snprintf(dbg, sizeof(dbg), "Capture Complete | Sample: %d", samplecount);
    lv_label_set_text(ui_lblRecPlayStatus, dbg);
    Print_Debug(dbg);
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
// bool SubGhz::send_tesla(float freqMhz)
// Self-contained: forces correct CC1101 config, sends, restores settings
// ---------------------------------------------------------------------
bool SubGhz::send_tesla(float freqMhz)
{
    // Save current CC1101 settings
    float savedFreq = CC1101_MHZ;
    int savedMod = CC1101_MODULATION;
    int savedPkt = CC1101_PKT_FORMAT;
    int savedSync = CC1101_SYNC_MODE;
    int savedPA = CC1101_PA;

    // Force correct settings for Tesla charge port signal
    CC1101_MHZ = freqMhz;
    CC1101_MODULATION = 2;   // ASK/OOK
    CC1101_PKT_FORMAT = 3;   // Async serial mode (bit-bang GDO0)
    CC1101_SYNC_MODE = 0;    // No preamble/sync
    CC1101_PA = 12;           // Max TX power (+12 dBm)

    SubGhz::enableTransmit();

    for (uint8_t t = 0; t < 5; t++)
    {
        for (uint8_t i = 0; i < messageLength; i++)
            SubGhz::send_byte(sequence[i]);
        digitalWrite(CC1101_GDO0, LOW);
        delay(messageDistance);
    }

    SubGhz::disableTransmit();

    // Restore previous CC1101 settings
    CC1101_MHZ = savedFreq;
    CC1101_MODULATION = savedMod;
    CC1101_PKT_FORMAT = savedPkt;
    CC1101_SYNC_MODE = savedSync;
    CC1101_PA = savedPA;

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

    char pbBuf[48];
    snprintf(pbBuf, sizeof(pbBuf), "Playback Complete ! Sample: %d", samplecount);
    lv_label_set_text(ui_lblRecPlayStatus, pbBuf);

    return true;
}

// =====================================================================
// Read RAW capture — Flipper-style OOK async serial timing capture
// =====================================================================
//
// The CC1101 is configured in OOK + Async Serial mode (PKTCTRL0=3) which
// streams the demodulated bitstream directly on GDO0. A GPIO change ISR
// time-stamps every edge with micros() and stores signed deltas:
//
//   +duration  → previous interval was a MARK (HIGH ended → LOW starting)
//   -duration  → previous interval was a SPACE (LOW ended → HIGH starting)
//
// This matches the Flipper RAW_Data convention.

#define RAW_CAP_SIZE 4096

static volatile int32_t     s_rawBuf[RAW_CAP_SIZE];
static volatile int         s_rawCount = 0;
// s_rawRunning + s_rawIsrPin + s_rawMux are forward-declared near the top of
// this file so the defensive rawCaptureForceStop() helper can see them.
static volatile uint32_t    s_rawLastEdgeUs = 0;
static volatile uint32_t    s_rawLastEdgeMs = 0;
static volatile bool        s_rawHavePrev = false;
static char                 s_rawFilename[96] = {0};
static float                s_rawFreqMhz = 0;
// Preset actually applied to the CC1101 for the in-flight capture. Captured
// in startRawCapture() and consumed by stopRawCapture() when emitting the
// Flipper "Preset:" header line in the .sub file.
static CC1101Preset         s_rawActivePreset = AM650;

// ISR — capture an edge of GDO0 and record signed transition delta.
static void IRAM_ATTR rawCaptureISR()
{
    if (!s_rawRunning) return;

    uint32_t now = micros();
    int level = digitalRead(CC1101_GDO0); // current level AFTER edge

    if (!s_rawHavePrev) {
        s_rawHavePrev = true;
        s_rawLastEdgeUs = now;
        s_rawLastEdgeMs = millis();
        return;
    }

    uint32_t delta = now - s_rawLastEdgeUs;
    s_rawLastEdgeUs = now;
    s_rawLastEdgeMs = millis();

    if (delta > (uint32_t)(INT32_MAX / 2)) {
        delta = (uint32_t)(INT32_MAX / 2);
    }

    int32_t value;
    // If we just went LOW, the interval we just measured was the HIGH
    // duration → MARK → +delta. If we went HIGH, it was the LOW
    // duration → SPACE → -delta.
    if (level == LOW) {
        value = (int32_t)delta;     // mark
    } else {
        value = -(int32_t)delta;    // space
    }

    // ESP32-S3 is dual-core. Touching head/tail of the ring buffer without
    // a critical section risks a torn write or out-of-bounds index if the
    // main loop reads s_rawCount mid-update from the other core.
    portENTER_CRITICAL_ISR(&s_rawMux);
    int c = s_rawCount;
    if (c < RAW_CAP_SIZE) {
        s_rawBuf[c] = value;
        s_rawCount = c + 1;
    }
    portEXIT_CRITICAL_ISR(&s_rawMux);
}

// Apply one of the four canonical Flipper presets to the CC1101. Caller
// must have already invoked ELECHOUSE_cc1101.Init() + setMHZ(). The radio
// is left in IDLE — caller should call SetRx() / SetTx() afterwards.
void SubGhz::applyPreset(CC1101Preset preset)
{
    // Common to all four presets: async serial out on GDO0, no sync word,
    // DC blocking filter off (Flipper convention for RAW capture/replay).
    int modulation = 2;     // ASK/OOK by default
    float drate    = 3.794f;
    int rxbw       = 650;
    float deviation = 0.0f;

    switch (preset) {
        case CC1101_PRESET_OOK_270:
            modulation = 2;
            drate      = 3.794f;
            rxbw       = 270;
            deviation  = 0.0f;
            break;
        case CC1101_PRESET_OOK_650:
            modulation = 2;
            drate      = 3.794f;
            rxbw       = 650;
            deviation  = 0.0f;
            break;
        case CC1101_PRESET_FSK_238:
            modulation = 0;
            drate      = 4.797f;
            rxbw       = 238;
            deviation  = 47.0f;
            break;
        case CC1101_PRESET_FSK_476:
            modulation = 0;
            drate      = 4.797f;
            rxbw       = 476;
            deviation  = 47.0f;
            break;
        default:
            // Unknown preset: leave radio in current config.
            return;
    }

    ELECHOUSE_cc1101.setModulation(modulation);
    ELECHOUSE_cc1101.setDRate(drate);
    ELECHOUSE_cc1101.setRxBW(rxbw);
    ELECHOUSE_cc1101.setDeviation(deviation);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setPktFormat(3);
    ELECHOUSE_cc1101.setDcFilterOff(1);

    // Track the active preset for the .sub header line.
    CC1101_PRESET = preset;
}

const char *SubGhz::presetName(CC1101Preset p)
{
    switch (p) {
        case CC1101_PRESET_OOK_270: return "OOK 270kHz";
        case CC1101_PRESET_OOK_650: return "OOK 650kHz";
        case CC1101_PRESET_FSK_238: return "FSK 238kHz";
        case CC1101_PRESET_FSK_476: return "FSK 476kHz";
        default:                    return "?";
    }
}

// Legacy 2-arg wrapper — defaults to the OOK 650 kHz preset that the
// original implementation hard-coded. main.cpp's fp_subghz_raw_start
// trampoline still calls this signature.
bool SubGhz::startRawCapture(float freq_mhz, const char *filename)
{
    return startRawCapture(freq_mhz, filename, CC1101_PRESET_OOK_650);
}

bool SubGhz::startRawCapture(float freq_mhz, const char *filename, CC1101Preset preset)
{
    if (!WorldMode::freqAllowed(freq_mhz)) {
        Serial.printf("[SubGhz] freq %.2f MHz outside region\n", freq_mhz);
        return false;
    }
    if (s_rawRunning) return false;

    // Refuse to start while scanner is sweeping the radio.
    if (currentState == kStateScanner) {
        return false;
    }

    // Reset capture buffer state.
    portENTER_CRITICAL(&s_rawMux);
    s_rawCount = 0;
    portEXIT_CRITICAL(&s_rawMux);
    s_rawHavePrev = false;
    s_rawLastEdgeUs = micros();
    s_rawLastEdgeMs = millis();
    s_rawFreqMhz = freq_mhz;
    s_rawActivePreset = preset;
    if (filename) {
        snprintf(s_rawFilename, sizeof(s_rawFilename), "%s", filename);
    } else {
        s_rawFilename[0] = '\0';
    }

    // Configure CC1101 via the canonical preset table, then go to RX.
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(freq_mhz);
    applyPreset(preset);
    ELECHOUSE_cc1101.SetRx();

    pinMode(CC1101_GDO0, INPUT);
    s_rawIsrPin = digitalPinToInterrupt(CC1101_GDO0);

    portENTER_CRITICAL(&s_rawMux);
    s_rawRunning = true;
    portEXIT_CRITICAL(&s_rawMux);
    attachInterrupt(s_rawIsrPin, rawCaptureISR, CHANGE);

    Serial.printf("[RAW] startRawCapture @ %.3f MHz (%s) -> %s\n",
                  freq_mhz, presetName(preset), s_rawFilename);
    return true;
}

int SubGhz::rawCaptureCount()
{
    return (int)s_rawCount;
}

bool SubGhz::rawCaptureRunning()
{
    return s_rawRunning;
}

uint32_t SubGhz::rawCaptureLastTransitionMs()
{
    return s_rawLastEdgeMs;
}

void SubGhz::stopRawCapture()
{
    if (!s_rawRunning) return;

    // Stop ISR FIRST so the buffer stops growing while we flush.
    if (s_rawIsrPin >= 0) {
        detachInterrupt(s_rawIsrPin);
    }
    portENTER_CRITICAL(&s_rawMux);
    s_rawRunning = false;
    int count = s_rawCount;
    portEXIT_CRITICAL(&s_rawMux);

    // Park the radio.
    ELECHOUSE_cc1101.setSidle();
    Serial.printf("[RAW] stopRawCapture — %d transitions\n", count);

    if (s_rawFilename[0] == '\0' || count < 2) {
        return; // nothing to write
    }

    // Mount SD (same pattern as saveCaptureToSD).
    sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, sdSPI)) {
        Serial.println("[RAW] SD mount failed");
        return;
    }

    if (!SD.exists("/captures")) {
        SD.mkdir("/captures");
    }

    char fullPath[160];
    if (s_rawFilename[0] == '/') {
        snprintf(fullPath, sizeof(fullPath), "%s", s_rawFilename);
    } else {
        snprintf(fullPath, sizeof(fullPath), "/captures/%s", s_rawFilename);
    }

    File f = SD.open(fullPath, FILE_WRITE, true);
    if (!f) {
        Serial.printf("[RAW] failed to open %s\n", fullPath);
        SD.end();
        return;
    }

    // Flipper RAW header.
    uint32_t freq_hz = (uint32_t)(s_rawFreqMhz * 1000000.0f);
    f.println("Filetype: Flipper SubGhz RAW File");
    f.println("Version: 1");
    f.printf("Frequency: %lu\n", (unsigned long)freq_hz);
    // Map the preset that was active for this capture to its Flipper name.
    const char *flipperPreset = "FuriHalSubGhzPresetOok650Async";
    switch (s_rawActivePreset) {
        case CC1101_PRESET_OOK_270: flipperPreset = "FuriHalSubGhzPresetOok270Async";   break;
        case CC1101_PRESET_OOK_650: flipperPreset = "FuriHalSubGhzPresetOok650Async";   break;
        case CC1101_PRESET_FSK_238: flipperPreset = "FuriHalSubGhzPreset2FSKDev238Async"; break;
        case CC1101_PRESET_FSK_476: flipperPreset = "FuriHalSubGhzPreset2FSKDev476Async"; break;
        default: break;
    }
    f.printf("Preset: %s\n", flipperPreset);
    f.println("Protocol: RAW");

    // Wrap RAW_Data at ~512 chars per line, prefixed with "RAW_Data: " each.
    const size_t MAX_LINE = 512;
    char lineBuf[MAX_LINE + 32];
    size_t pos = 0;
    pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "RAW_Data: ");

    bool firstOnLine = true;
    for (int i = 0; i < count; i++) {
        char num[16];
        int n;
        if (firstOnLine) {
            n = snprintf(num, sizeof(num), "%ld", (long)s_rawBuf[i]);
        } else {
            n = snprintf(num, sizeof(num), " %ld", (long)s_rawBuf[i]);
        }

        if (pos + (size_t)n >= MAX_LINE) {
            // Flush current line, start a new RAW_Data: chunk.
            lineBuf[pos] = '\0';
            f.println(lineBuf);
            pos = 0;
            pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "RAW_Data: ");
            n = snprintf(num, sizeof(num), "%ld", (long)s_rawBuf[i]);
            firstOnLine = true;
        }

        memcpy(lineBuf + pos, num, n);
        pos += n;
        firstOnLine = false;

        if ((i & 0xFF) == 0) {
            vTaskDelay(1); // yield occasionally during long writes
        }
    }

    if (pos > strlen("RAW_Data: ")) {
        lineBuf[pos] = '\0';
        f.println(lineBuf);
    }

    f.flush();
    Serial.printf("[RAW] wrote %s (%lu bytes)\n", fullPath, (unsigned long)f.size());
    f.close();
    SD.end();
}


