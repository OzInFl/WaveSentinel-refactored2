/*
  Example for different sending methods
  
  https://github.com/sui77/rc-switch/
  https://github.com/LSatan/SmartRC-CC1101-Driver-Lib
  ----------------------------------------------------------
  Mod by Little Satan. Have Fun!
  ----------------------------------------------------------
*/
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>

#define CC1101_TX 25

#define CC1101_SCLK 14
#define CC1101_MISO 12
#define CC1101_MOSI 13
#define CC1101_CSN 27

RCSwitch mySwitch = RCSwitch();

void setup() {
  Serial.begin(9600);
    InitCC1101();
  
}

void InitCC1101() {
ELECHOUSE_cc1101.setSpiPin(CC1101_SCLK, CC1101_MISO, CC1101_MOSI, CC1101_CSN);

  if (ELECHOUSE_cc1101.getCC1101()){       // Check the CC1101 Spi connection.
  Serial.println("Connection OK");
  }else{
  Serial.println("Connection Error");
  }

//CC1101 Settings:                (Settings with "//" are optional!)
  ELECHOUSE_cc1101.Init();            // must be set to initialize the cc1101!
//ELECHOUSE_cc1101.setRxBW(812.50);  // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
//ELECHOUSE_cc1101.setPA(10);       // set TxPower. The following settings are possible depending on the frequency band.  (-30  -20  -15  -10  -6    0    5    7    10   11   12)   Default is max!
  ELECHOUSE_cc1101.setMHZ(433.92); // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
  
  ELECHOUSE_cc1101.setModulation(4);
  
  ELECHOUSE_cc1101.setDRate(249.939);
  ELECHOUSE_cc1101.setChsp(405);
  ELECHOUSE_cc1101.setDeviation(380);

  // Transmitter on 
  mySwitch.enableTransmit(CC1101_TX);
  
  // cc1101 set Transmit on
  ELECHOUSE_cc1101.SetTx();
}

void loop() {
  //InitCC1101();
  
  /* Same switch as above, but using binary code */
  mySwitch.send("000000000001010100010001");
  
  // Transmitter off 
  //mySwitch.disableTransmit();
  //ELECHOUSE_cc1101.setSidle();
  
  //delay(5000);
}
