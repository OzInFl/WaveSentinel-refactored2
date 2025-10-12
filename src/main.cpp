// WAVE SENTINEL - SUBGHZ - BT5 - WiFi Testing Tool
extern "C" {
  #include "esp_wifi.h"
}
extern "C" {
  #include "nvs_flash.h"
  #include "esp_wifi.h"
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// INCLUDE
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
#include "Misc/Config.h"
#include "Display/Display.h"
#include "SubGhz/SubGhz.h"
#include "WiFi/WiFix.h"
//#include "BLE/BLE.h"
#include "SD/SDCard.h"


#include "Arduino.h"
#include "Audio.h"
#include <Preferences.h>
#include "AppleBLESpam.h"

 
 // ——————————————————————————————————————————————————————————————————————————
 // (Apple BLESpam includes…)
 // ——————————————————————————————————————————————————————————————————————————

// Fake 6-byte BLE address for the Apple-style advert
static const uint8_t fakeMac[31] = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x02, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
// Our spammer instance
static AppleBLESpam bleSpammer(fakeMac);




// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// DECLARE
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// SubGhz Class
SubGhz SUBGHZ;

// Audio I2S Definitions
Audio audio;

// Preferences Library
Preferences prefs;

//Temp Keyboard value
string kybdvalue;


// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// FUNCTIONS
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void Task_Refresh_Screen(void *parameter)
// ---------------------------------------------------------------------
void Task_Refresh_Screen(void *parameter)
{
  while (true)
  {
    lv_timer_handler();
    vTaskDelay(1);
  }

  vTaskDelete(NULL);
}

// ---------------------------------------------------------------------
// void setup()
// ---------------------------------------------------------------------
void setup()
{
  Print_Debug("Initializing Stuff...");

  
  // I2S Stuff
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);

  /* this callback function will be invoked when starting */
  ArduinoOTA.onStart([]()
                     { 
                    currentState=STATE_OTA; 
    lv_label_set_text(ui_lblSettingsStatus, "UPDATE STARTED"); });

  /* this callback function will be invoked when updating end */
  ArduinoOTA.onEnd([]()
                   {
                    currentState = STATE_IDLE;
    OTAInProgress=0;
    lv_label_set_text(ui_lblSettingsStatus,"COMPLETE - RESTARTING");
    delay(5000);  
    ESP.restart(); });

  /* this callback function will be invoked when updating error */
  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) lv_label_set_text(ui_lblSettingsStatus,"Auth Failed");
    else if (error == OTA_BEGIN_ERROR) lv_label_set_text(ui_lblSettingsStatus,"Begin Failed");
    else if (error == OTA_CONNECT_ERROR) lv_label_set_text(ui_lblSettingsStatus,"Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) lv_label_set_text(ui_lblSettingsStatus,"Receive Failed");
    else if (error == OTA_END_ERROR) lv_label_set_text(ui_lblSettingsStatus,"End Failed"); });
  /* this callback function will be invoked when a number of chunks of software was flashed
    so we can use it to calculate the progress of flashing */
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        {
    String UpdProgress="Progress: ";
    UpdProgress+=String((progress / (total / 100))).c_str();
    lv_label_set_text(ui_lblSettingsStatus,UpdProgress.c_str());
    lv_bar_set_value(ui_barProgress,progress / (total / 100),LV_ANIM_ON); });

  // Start The Serial Debug Port
  Serial.begin(115200); /* prepare for possible serial debug */
  
  // Start The Bluetoth Stack
  //BLEsetup();


  Print_Debug("Initializing Display...");

  Init_Display();

  Print_Debug("Initializing Default Value...");

  String FirmwareVer = "Version ";
  FirmwareVer += APP_VERSION_MAJOR;
  FirmwareVer += ".";
  FirmwareVer += APP_VERSION_MINOR;
  FirmwareVer += ".";
  FirmwareVer += APP_VERSION_PATCH;

  lv_label_set_text(ui_lblVersion, String(FirmwareVer).c_str());
  lv_label_set_text(ui_lblSplashStatus, "TAP ANYWHERE TO BEGIN");

  Print_Debug("Initializing CC1101...");

  if (SUBGHZ.init())
  {
    lv_label_set_text(ui_lblSplash, "CC1101: Init Success");
    Print_Debug("CC1101 successfully initialized.");
  }
  else
  {
    lv_label_set_text(ui_lblSplash, "CC1101: Init Fail");
    Print_Debug("CC1101 not initialized.");
  }

  xTaskCreatePinnedToCore(Task_Refresh_Screen, "Task_Refresh_Screen", 20000, NULL, 1, NULL, 0);

  Print_Debug("Setup done.");

  //Hide wifi flags
  lv_obj_add_flag(ui_imgWifi, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_imgWifi2, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------
// void loop()
// ---------------------------------------------------------------------
void loop()
{
 
if (currentState == STATE_OTA)
  {
    ArduinoOTA.handle();
    server.handleClient();
    //currentState = STATE_IDLE;

  }else if (currentState == STATE_AUDIO_TEST)
  {
    audio.loop();
  }
  else if (currentState == STATE_WIFI_SCAN)
  {

    while (!scanFinished)
    {
      delay(1);
    }

    int16_t wifiNetwork = WiFi.scanComplete();

    lv_label_set_text(ui_lblWifiScanNetsFound, String(String("WiFi Networks Found: ") + String(wifiNetwork)).c_str());

    // Update dropdown with the scanned networks
    for (int i = 0; i < wifiNetwork; i++)
    {
      // Update the dropdown
      lv_dropdown_add_option(ui_ddlWifiSSID, WiFi.SSID(i).c_str(), LV_DROPDOWN_POS_LAST);
    }

    // Update the textarea with the first result
    String ssid;
    int32_t rssi;
    uint8_t encryptionType;
    uint8_t *bssid;
    int32_t channel;

    WiFi.getNetworkInfo(0, ssid, encryptionType, rssi, bssid, channel);

    String MAC = String(bssid[0], HEX) + ":" + String(bssid[1], HEX) + ":" + String(bssid[2], HEX) + ":" + String(bssid[3], HEX) + ":" + String(bssid[4], HEX) + ":" + String(bssid[5], HEX);
    MAC.toUpperCase();

    Print_Debug(String(String("View Network, SSID: ") + String(ssid) + String(" | MAC: ") + String(MAC) + String(" | RSSI: ") + String(rssi) + String(" dBm | Channel: ") + String(channel) + String(" | Encryption Type: ") + String(GetEncryptionTypeString(encryptionType))).c_str());

    // Update the textarea
    lv_textarea_add_text(ui_txtWifiScanNetsFound, String(String("SSID: ") + String(ssid) + String("\nMAC: ") + String(MAC) + String("\nRSSI: ") + String(rssi) + String(" dBm\n") + String("Channel: ") + String(channel) + String("\nEncryption Type: ") + String(GetEncryptionTypeString(encryptionType)) + String("\n\n")).c_str());

    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_ANALYZER)
  {
    if (SUBGHZ.ProtAnalyzerLoop())
    {
      SUBGHZ.showResultProtAnalyzer();
      delay(1000); 
      SUBGHZ.resetProtAnalyzer();
    }
    delay(1);
  }
  else if (currentState == STATE_CAPTURE)
  {
    if (SUBGHZ.CaptureLoop())
    {
      SUBGHZ.disableReceiver();
      //delay(1000); //- OZ CHANGES FOR TESTING
      SUBGHZ.showResultRecPlay();
      currentState = STATE_IDLE;
    }
    delay(1);
  }
  else if (currentState == STATE_PLAYBACK)
  {
    if (SUBGHZ.sendCapture())
    {
      currentState = STATE_IDLE;
    }
  }
  else if (currentState == STATE_SCANNER)
  {
    SUBGHZ.ScannerLoop();
  }
  else if (currentState == STATE_GENERATOR)
  {
    while (currentState == STATE_GENERATOR)
    {
      SUBGHZ.GeneratorLoop();
    }
    SUBGHZ.disableTransmit();
  }
  else if (currentState == STATE_TESLA_US)
  {
    SUBGHZ.setFrequency(310.00);
    SUBGHZ.enableTransmit();
    if (SUBGHZ.send_tesla())
    {
      lv_label_set_text(ui_lblPresetsStatus, "Sending EU Tesla..");
    }
    SUBGHZ.disableTransmit();
    currentState = STATE_TESLA_EU;
  }
  else if (currentState == STATE_TESLA_EU)
  {
    SUBGHZ.setFrequency(433.92);
    SUBGHZ.enableTransmit();
    if (SUBGHZ.send_tesla())
    {
      lv_label_set_text(ui_lblPresetsStatus, "Sending Tesla Complete !");
    }
    SUBGHZ.disableTransmit();
    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_SEND_FLIPPER)
  {
    Print_Debug(String("Send RAW Data, sample count: " + String(tempSampleCount) + String(" | Frequency: ") + String(tempFreq)).c_str());

    SUBGHZ.setFrequency(tempFreq);
    SUBGHZ.enableTransmit();
    SUBGHZ.sendSamples(tempSample, tempSampleCount);
    SUBGHZ.disableTransmit();

    lv_label_set_text(ui_lblPresetsStatus, String("Sending Flipper Complete ! \n\nSample: " + String(tempSampleCount) + String(" | Freq: ") + String(tempFreq) + String(" mHz")).c_str());

    currentState = STATE_IDLE;
  }else if (currentState == STATE_SEND_BLESPAM)
  {
    Print_Debug(String("Send BLE SPAM, sample count: " + String(tempSampleCount) + String(" | Frequency: ") + String(tempFreq)).c_str());

    //BLEspam();
    bleSpammer.begin();

    lv_label_set_text(ui_lblPresetsStatus, String("Sending BLE SPAM! \n\nSample: " + String(tempSampleCount) + String(" | Freq: ") + String(tempFreq) + String(" mHz")).c_str());

    currentState = STATE_IDLE;
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Event Present in Main Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_load_screen_scan(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_scan(lv_event_t *e)
{
  Print_Debug("event_load_screen_scan");
  lv_scr_load(ui_scrCC1101Stuff);
}

// ---------------------------------------------------------------------
// void event_load_screen_rcsw(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_rcsw(lv_event_t *e)
{
  Print_Debug("event_load_screen_rcsw");
  lv_scr_load(ui_scrRCSWMain);
}

// ---------------------------------------------------------------------
// void event_stop_audio(lv_event_t *e)
// ---------------------------------------------------------------------
void event_stop_audio(lv_event_t *e)
{
  Print_Debug("event_stop_audio");

  if (currentState == STATE_AUDIO_TEST)
  {
    currentState = STATE_IDLE;

    audio.stopSong();

    // Hide Stop Button / Volume
    lv_obj_add_flag(ui_btnMainStopMp3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_lblMainVolumeMp3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_sliderMainVolumeMp3, LV_OBJ_FLAG_HIDDEN);

    now_close_sd_card();
  }
}

// ---------------------------------------------------------------------
// void event_set_volume_audio(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_volume_audio(lv_event_t *e)
{
  Print_Debug("event_set_volume_audio");

  if (currentState == STATE_AUDIO_TEST)
  {
    // Set Volume from slider
    audio.setVolume(lv_slider_get_value(ui_sliderMainVolumeMp3));
  }
}

// ---------------------------------------------------------------------
// void event_play_audio_test(lv_event_t *e)
// ---------------------------------------------------------------------
void event_play_audio_test(lv_event_t *e)
{
  Print_Debug("event_play_audio_test");

  if (sd_card_is_present())
  {
    // Set Volume from slider
    audio.setVolume(lv_slider_get_value(ui_sliderMainVolumeMp3));

    // Open and play music test file
    if (!audio.connecttoFS(SD, "/test.mp3"))
    {
      lv_label_set_text(ui_lblMainStatus, "Missing test.mp3");
      now_close_sd_card();
    }

    // Show Stop Button / Volume
    lv_obj_clear_flag(ui_btnMainStopMp3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_lblMainVolumeMp3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_sliderMainVolumeMp3, LV_OBJ_FLAG_HIDDEN);

    // Place the device in adequat mode
    currentState = STATE_AUDIO_TEST;
  }
}

// ---------------------------------------------------------------------
// void event_load_screen_wifi_apps(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_wifi_apps(lv_event_t *e)
{
  Print_Debug("event_load_screen_wifi_apps");
  lv_scr_load(ui_scrWifiApps);
}

// ---------------------------------------------------------------------
// void event_load_screen_protocol_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_protocol_analyzer(lv_event_t *e)
{
  Print_Debug("event_load_screen_protocol_analyzer");
  lv_scr_load(ui_scrProtAna);
}

// ---------------------------------------------------------------------
// void event_load_screen_flipper(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_flipper(lv_event_t *e)
{
  Print_Debug("event_load_screen_flipper");

  if (sd_card_is_present())
  {
    refresh_sd_card_folder(ui_ddPresetsFolder, String("/").c_str());

    refresh_sd_card_file(ui_ddPresetsFile, String("/").c_str(), ".sub", true);

    now_close_sd_card();
  }

  lv_scr_load(ui_scrPresets);
}

// ---------------------------------------------------------------------
// void event_load_screen_settings(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_settings(lv_event_t *e)
{
  Print_Debug("event_load_screen_settings");
  lv_scr_load(ui_scrSettings);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Event Present in Flipper Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_refresh_flipper_file(lv_event_t *e)
// ---------------------------------------------------------------------
void event_refresh_flipper_file(lv_event_t *e)
{
  Print_Debug("event_refresh_flipper_file");

  lv_label_set_text(ui_lblPresetsStatus, "-Status-");

  if (sd_card_is_present())
  {

    char *currentFolder = (char *)malloc(generaleSize * sizeof(char));
    lv_dropdown_get_selected_str(ui_ddPresetsFolder, currentFolder, generaleSize);

    if (strcmp(currentFolder, "/") == 0)
    {
      refresh_sd_card_file(ui_ddPresetsFile, String("/").c_str(), ".sub", true);
    }
    else
    {
      refresh_sd_card_file(ui_ddPresetsFile, String(String("/") + String(currentFolder)).c_str(), ".sub", true);
    }

    now_close_sd_card();

    free(currentFolder);
  }
}

// ---------------------------------------------------------------------
// void event_select_flipper_file(lv_event_t *e)
// ---------------------------------------------------------------------
void event_select_flipper_file(lv_event_t *e)
{
  Print_Debug("event_select_flipper_file");

  lv_label_set_text(ui_lblPresetsStatus, "-Status-");
}

// ---------------------------------------------------------------------
// void event_send_tesla(lv_event_t *e)
// ---------------------------------------------------------------------
void event_send_tesla(lv_event_t *e)
{
  Print_Debug("event_send_tesla");

  if (currentState == STATE_IDLE)
  {
    lv_label_set_text(ui_lblPresetsStatus, "Sending US Tesla..");
    currentState = STATE_TESLA_US;
  }
}

// ---------------------------------------------------------------------
// void event_send_flipper_file(lv_event_t *e)
// ---------------------------------------------------------------------
void event_send_flipper_file(lv_event_t *e)
{
  Print_Debug("event_send_flipper_file");

  lv_label_set_text(ui_lblPresetsStatus, "Please wait..");

  // Get the currently selected option
  int index = lv_dropdown_get_selected(ui_ddPresetsFolder);
  char *folderbuffer = (char *)malloc(generaleSize * sizeof(char));
  char *filebuffer = (char *)malloc(generaleSize * sizeof(char));

  lv_dropdown_get_selected_str(ui_ddPresetsFolder, folderbuffer, generaleSize);
  lv_dropdown_get_selected_str(ui_ddPresetsFile, filebuffer, generaleSize);

  String fullfilename = "";

  if (index == 0) // root path "/"
  {
    fullfilename += String(String("/") + String(filebuffer));
  }
  else // root path "/" + folder
  {
    fullfilename += String("/") + String(folderbuffer) + String("/") + String(filebuffer);
  }

  free(folderbuffer);
  free(filebuffer);
  // End get selected option

  bool parsed = false;

  if (sd_card_is_present())
  {
    if (read_sd_card_flipper_file(String(fullfilename).c_str()))
    {
      parsed = true;
    }
    now_close_sd_card();
  }

  if (parsed)
  {
    currentState = STATE_SEND_FLIPPER;
  }
  else
  {
    lv_label_set_text(ui_lblPresetsStatus, "ERROR: File Invalid !");
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Event Present in Settings Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_restart_device(lv_event_t *e)
// ---------------------------------------------------------------------
void event_restart_device(lv_event_t *e)
{
  Print_Debug("event_restart_device");

  ESP.restart();
}

// ---------------------------------------------------------------------
// void event_rotate_device(lv_event_t *e)
// ---------------------------------------------------------------------
void event_rotate_device(lv_event_t *e)
{
  Print_Debug("event_rotate_device");

  // Rotate the LCD
  if (tft.getRotation() == 2)
  {
    tft.clearDisplay();
    tft.setRotation(0);
    ui_init();
  }
  else if (tft.getRotation() == 0)
  {
    tft.clearDisplay();
    tft.setRotation(2);
    ui_init();
  }
}

// ---------------------------------------------------------------------
// void event_set_brightness_device(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_brightness_device(lv_event_t *e)
{
  Print_Debug("event_set_brightness_device");

  tft.setBrightness(lv_slider_get_value(ui_sldBrightness));
}

// ---------------------------------------------------------------------
// void event_save_settings_device(lv_event_t *e)
// ---------------------------------------------------------------------
void event_save_settings_device(lv_event_t *e)
{
  Print_Debug("event_save_settings_device");
}

// ---------------------------------------------------------------------
// void event_enable_ota_device(lv_event_t *e)
// ---------------------------------------------------------------------

void prepare_wifi_stack_clean() {
  // Initialize NVS (required before using WiFi)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.println("[DEBUG] NVS invalid, erasing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  // Proper shutdown of WiFi stack (if previously used)
  esp_wifi_stop();    // Will return error if not started yet, safe to call
  esp_wifi_deinit();  // Required before re-init
  delay(200);
}

void task_enable_ota_device(void *pvParameters) {
  vTaskDelay(100 / portTICK_PERIOD_MS); // Small delay to let things settle
  prepare_wifi_stack_clean();  // ✅ Full reset
  Print_Debug("OTA Task started on Core %d | RAM: %dKB", xPortGetCoreID(), ESP.getFreeHeap() / 1024);

  const char* ota_ssid = "WAVESENTINEL";
  const char* ota_password = "987654321";
  int ota_channel = 1;

  // Safety checks
  if (!ota_ssid || strlen(ota_ssid) == 0 || !ota_password || strlen(ota_password) < 8) {
    Serial.println("[ERROR] Invalid SSID or password.");
    vTaskDelete(NULL);
    return;
  }

// Clean and prepare NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      Serial.println("NVS error, erasing...");
      nvs_flash_erase();
      nvs_flash_init();
  }


  OTAInProgress = 1;

  

  // Completely shut down WiFi first
WiFi.disconnect(true, true);
delay(100);

// Full deinit
esp_wifi_stop();
esp_wifi_deinit();
delay(200);

// Start fresh
WiFi.enableAP(true);
WiFi.mode(WIFI_AP);
delay(100);

  bool apStarted = WiFi.softAP(ota_ssid, ota_password, ota_channel);
  if (!apStarted) {
    Serial.println("[ERROR] Failed to start SoftAP!");
    vTaskDelete(NULL);
    return;
  }

  IPAddress ip = WiFi.softAPIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

  // UI (done safely, assuming Core 1 calling LVGL is OK after startup)
  lv_label_set_text(ui_lblSettingsStatus, "OTA READY");
  lv_label_set_text(ui_lblSettingsIPAddr, ipStr);

  ArduinoOTA.begin();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.onNotFound(handleRoot);
  server.begin();

  Serial.println("OTA ready and HTTP server started.");

  vTaskDelete(NULL); // Done
}



void event_enable_ota_device(lv_event_t *e)
{
  Print_Debug("Launching OTA setup on Core 1...");

  // Pin task to Core 1 (the "APP" core)
  xTaskCreatePinnedToCore(
    task_enable_ota_device,   // Function
    "OTA Setup Task",         // Name
    10000,                     // Stack size (adjust if needed)
    NULL,                     // Parameters
    1,                        // Priority
    NULL,                     // Task handle
    1                         // Core 1
  );
}




// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Events in CC1101 Stuff Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_tab_change_cc1101_stuff(lv_event_t *e)
// ---------------------------------------------------------------------
void event_tab_change_cc1101_stuff(lv_event_t *e)
{
  Print_Debug("event_tab_change_cc1101_stuff");

  switch (lv_tabview_get_tab_act(ui_tabCC1101Stuff))
  {
  case 0: // Scan Tab
    if (currentState == STATE_GENERATOR)
    {
      // Stop
      lv_obj_clear_state(ui_swGenEnable, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblGenEnable, "ON/OFF");
      SUBGHZ.disableTransmit();
      currentState = STATE_IDLE;
    }

    if (currentState != STATE_SCANNER)
    {
      lv_obj_clear_state(ui_swScannerOn, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblScanEnable, "ON/OFF");
    }
    else
    {
      lv_obj_add_state(ui_swScannerOn, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblScanEnable, "SCAN ON");
    }

    break;
  case 1: // Gen Tab
    if (currentState == STATE_SCANNER)
    {
      // Stop
      lv_obj_clear_state(ui_swScannerOn, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblScanEnable, "ON/OFF");
      SUBGHZ.disableScanner();
      currentState = STATE_IDLE;
    }

    if (currentState != STATE_GENERATOR)
    {
      lv_obj_clear_state(ui_swGenEnable, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblGenEnable, "ON/OFF");
    }
    else
    {
      lv_obj_add_state(ui_swGenEnable, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblGenEnable, "GEN ON");
    }

    break;
  case 2: // Rec/Play Tab
    break;
  case 3: // Config Tab
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void event_exit_cc1101_stuff(lv_event_t *e)
// ---------------------------------------------------------------------
void event_exit_cc1101_stuff(lv_event_t *e)
{
  Print_Debug("event_exit_cc1101_stuff");

  currentState = STATE_IDLE;
  lv_obj_clear_state(ui_swScannerOn, LV_STATE_CHECKED);
  lv_label_set_text(ui_lblScanEnable, "ON/OFF");

  lv_obj_clear_state(ui_swGenEnable, LV_STATE_CHECKED);
  lv_label_set_text(ui_lblGenEnable, "ON/OFF");

  lv_scr_load(ui_scrMain);
}

// Scan TAB
// ---------------------------------------------------------------------
// void event_set_scan_preset_freq(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_scan_preset_freq(lv_event_t *e)
{
  Print_Debug("event_set_scan_preset_freq");

  char *currentFreq = (char *)malloc(generaleSize * sizeof(char));
  lv_dropdown_get_selected_str(ui_ddl1101ScanPreset, currentFreq, generaleSize);

  if (strcmp(currentFreq, "< Manual") == 0)
  {
    Print_Debug("Manual Mode Selected");
  }
  else
  {
    lv_textarea_set_text(ui_txtScanStartFq, currentFreq);
    lv_textarea_set_text(ui_txtScanStopFq, String(String(currentFreq).toFloat() + 10.00).c_str());
  }

  free(currentFreq);
}

// ---------------------------------------------------------------------
// void event_clear_scanner(lv_event_t *e)
// ---------------------------------------------------------------------
void event_clear_scanner(lv_event_t *e)
{
  Print_Debug("event_clear_scanner");

  lv_textarea_set_text(ui_txtScannerData, "");
}

// ---------------------------------------------------------------------
// void event_start_stop_scanner(lv_event_t *e)
// ---------------------------------------------------------------------
void event_start_stop_scanner(lv_event_t *e)
{
  Print_Debug("event_start_stop_scanner");

  if (currentState == STATE_IDLE)
  {
    // Start
    lv_textarea_set_cursor_click_pos(ui_txtScannerData, false);
    float start = String(lv_textarea_get_text(ui_txtScanStartFq)).toFloat();
    float stop = String(lv_textarea_get_text(ui_txtScanStopFq)).toFloat();
    lv_obj_add_state(ui_swScannerOn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblScanEnable, "SCAN ON");
    SUBGHZ.enableScanner(start, stop);
    currentState = STATE_SCANNER;
  }
  else
  {
    // Stop
    lv_obj_clear_state(ui_swScannerOn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblScanEnable, "SCAN OFF");
    SUBGHZ.disableScanner();
    currentState = STATE_IDLE;
  }
}

// Packet Gen TAB
// ---------------------------------------------------------------------
// void event_set_gen_preset_freq(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_gen_preset_freq(lv_event_t *e)
{
  Print_Debug("event_set_gen_preset_freq");

  char *currentFreq = (char *)malloc(generaleSize * sizeof(char));
  lv_dropdown_get_selected_str(ui_ddl1101GenPreset, currentFreq, generaleSize);

  if (strcmp(currentFreq, "< Manual") == 0)
  {
    Print_Debug("Manual Mode Selected");
  }
  else
  {
    lv_textarea_set_text(ui_txt1101GenFreq, currentFreq);
  }

  free(currentFreq);
}

// ---------------------------------------------------------------------
// void event_start_stop_packet_gen(lv_event_t *e)
// ---------------------------------------------------------------------
void event_start_stop_packet_gen(lv_event_t *e)
{
  Print_Debug("event_start_stop_packet_gen");

  if (currentState == STATE_IDLE)
  {
    // Start
    float freq = String(lv_textarea_get_text(ui_txt1101GenFreq)).toFloat();
    lv_obj_add_state(ui_swGenEnable, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblGenEnable, "GEN ON");
    SUBGHZ.setFrequency(freq);
    SUBGHZ.enableTransmit();
    currentState = STATE_GENERATOR;
  }
  else
  {
    // Stop
    lv_obj_clear_state(ui_swGenEnable, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblGenEnable, "GEN OFF");
    SUBGHZ.disableTransmit();
    currentState = STATE_IDLE;
  }
}

// Rec/Play TAB
// ---------------------------------------------------------------------
// void event_set_preset_rec_play(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_preset_rec_play(lv_event_t *e)
{
  Print_Debug("event_set_preset_rec_play");

  uint8_t index = lv_dropdown_get_selected(ui_Preset);

  switch (index)
  {
  case 0:
    SUBGHZ.setPreset(AM650);
    break;
  case 1:
    SUBGHZ.setPreset(AM270);
    break;
  case 2:
    SUBGHZ.setPreset(FM238);
    break;
  case 3:
    SUBGHZ.setPreset(FM476);
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void event_capture_rec_play(lv_event_t *e)
// ---------------------------------------------------------------------
void event_capture_rec_play(lv_event_t *e)
{
  Print_Debug("event_capture_rec_play");

  if (currentState == STATE_IDLE)
  {
    // Start
    float freq = String(lv_textarea_get_text(ui_txtRecPlayFq)).toFloat();
    lv_obj_add_flag(ui_indGreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_indRed, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(ui_txtRawData, "");
    lv_label_set_text(ui_lblRecPlayStatus, String("Capture Started..").c_str());

    SUBGHZ.setFrequency(freq);
    SUBGHZ.enableReceiver();
    currentState = STATE_CAPTURE;
  }
}

// ---------------------------------------------------------------------
// void event_playback_rec_play(lv_event_t *e)
// ---------------------------------------------------------------------
void event_playback_rec_play(lv_event_t *e)
{
  Print_Debug("event_playback_rec_play");

  if (currentState == STATE_IDLE)
  {
    // Start
    lv_obj_add_flag(ui_indGreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_indRed, LV_OBJ_FLAG_HIDDEN);

    float freq = String(lv_textarea_get_text(ui_txtRecPlayFq)).toFloat();
    SUBGHZ.setFrequency(freq);

    currentState = STATE_PLAYBACK;
  }
  else
  {
    Print_Debug("NOT IDLE");
  }
}

// Config TAB
// ---------------------------------------------------------------------
// void event_set_modulation(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_modulation(lv_event_t *e)
{
  Print_Debug("event_set_modulation");

  uint8_t index = lv_dropdown_get_selected(ui_ddlCC1101PktFormat);

  switch (index)
  {
  case 0: // ASK/OOK
    SUBGHZ.setModulation(2);
    break;
  case 1: // 2-FSK
    SUBGHZ.setModulation(0);
    break;
  case 3: // 4-FSK
    SUBGHZ.setModulation(3);
    break;
  case 4: // GFSK
    SUBGHZ.setModulation(1);
    break;
  case 5: // MSK
    SUBGHZ.setModulation(4);
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void event_set_packet_format(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_packet_format(lv_event_t *e)
{
  Print_Debug("event_set_packet_format");

  uint8_t index = lv_dropdown_get_selected(ui_ddlCC1101PktFormat);

  switch (index)
  {
  case 0: // NORMAL
    SUBGHZ.setPacketFormat(0);
    break;
  case 1: // SYNCHRONOUS
    SUBGHZ.setPacketFormat(1);
    break;
  case 3: // RANDOM TX
    SUBGHZ.setPacketFormat(2);
    break;
  case 4: // ASYNCHRONOUS
    SUBGHZ.setPacketFormat(3);
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void event_set_preset(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_preset(lv_event_t *e)
{
  Print_Debug("event_set_preset");

  uint8_t index = lv_dropdown_get_selected(ui_Config1101Preset);

  switch (index)
  {
  case 0:
    SUBGHZ.setPreset(AM650);
    lv_dropdown_set_selected(ui_ddlCC1101ModType, 0); // Set Modulation
    lv_arc_set_value(ui_arcScanBW, 650);              // Set bandwidth
    lv_label_set_text(ui_lblRXBWArc, "650");
    lv_arc_set_value(ui_arcDeviation, 1); // Set deviation
    lv_label_set_text(ui_lblDeviation, "1");
    break;
  case 1:
    SUBGHZ.setPreset(AM270);
    lv_dropdown_set_selected(ui_ddlCC1101ModType, 0); // Set Modulation
    lv_arc_set_value(ui_arcScanBW, 270);              // Set bandwidth
    lv_label_set_text(ui_lblRXBWArc, "270");
    lv_arc_set_value(ui_arcDeviation, 1); // Set deviation
    lv_label_set_text(ui_lblDeviation, "1");
    break;
  case 2:
    SUBGHZ.setPreset(FM238);
    lv_dropdown_set_selected(ui_ddlCC1101ModType, 1); // Set Modulation
    lv_arc_set_value(ui_arcScanBW, 270);              // Set bandwidth
    lv_label_set_text(ui_lblRXBWArc, "270");
    lv_arc_set_value(ui_arcDeviation, 2); // Set deviation
    lv_label_set_text(ui_lblDeviation, "2");
    break;
  case 3:
    SUBGHZ.setPreset(FM476);
    lv_dropdown_set_selected(ui_ddlCC1101ModType, 1); // Set Modulation
    lv_arc_set_value(ui_arcScanBW, 270);              // Set bandwidth
    lv_label_set_text(ui_lblRXBWArc, "270");
    lv_arc_set_value(ui_arcDeviation, 47); // Set deviation
    lv_label_set_text(ui_lblDeviation, "47");
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Events in Protocol Analyzer Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_set_preset_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_preset_analyzer(lv_event_t *e)
{
  Print_Debug("event_set_preset_analyzer");

  uint8_t index = lv_dropdown_get_selected(ui_ProtanaPreset);

  switch (index)
  {
  case 0:
    SUBGHZ.setPreset(AM650);
    break;
  case 1:
    SUBGHZ.setPreset(AM270);
    break;
  case 2:
    SUBGHZ.setPreset(FM238);
    break;
  case 3:
    SUBGHZ.setPreset(FM476);
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void event_exit_protocol_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_exit_protocol_analyzer(lv_event_t *e)
{
  Print_Debug("event_exit_protocol_analyzer");
  currentState = STATE_IDLE;
  lv_obj_clear_state(ui_swtProtAnaRxEn, LV_STATE_CHECKED);
  lv_label_set_text(ui_lblProtAnaRXEn, "ON/OFF");
  lv_scr_load(ui_scrMain);
}

// ---------------------------------------------------------------------
// void event_start_stop_protocol_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_start_stop_protocol_analyzer(lv_event_t *e)
{
  Print_Debug("event_start_stop_protocol_analyzer");

  if (currentState == STATE_IDLE)
  {
    // Start
    float freq = String(lv_textarea_get_text(ui_txtMainFreq)).toFloat();
    lv_obj_add_state(ui_swtProtAnaRxEn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblProtAnaRXEn, "RX ON");
    SUBGHZ.setFrequency(freq);
    SUBGHZ.enableRCSwitch();
    currentState = STATE_ANALYZER;
  }
  else
  {
    // Stop
    lv_obj_clear_state(ui_swtProtAnaRxEn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblProtAnaRXEn, "RX OFF");
    SUBGHZ.disableRCSwitch();
    currentState = STATE_IDLE;
  }
}

// ---------------------------------------------------------------------
// void event_clear_protocol_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_clear_protocol_analyzer(lv_event_t *e)
{
  Print_Debug("event_clear_protocol_analyzer");

  lv_textarea_set_text(ui_txtProtAnaBinary, "-");          // Binary
  lv_textarea_set_text(ui_txtProtAnaPulsLen, "-");         // Pulse Length
  lv_textarea_set_text(ui_txtProtAnaProtAnaTriState, "-"); // TriState
  lv_textarea_set_text(ui_txtProtAnaProtocol, "-");        // Protocol
  lv_textarea_set_text(ui_txtProtAnaResults, "");
  lv_textarea_set_text(ui_txtProtAnaBitLength, "-");
  lv_textarea_set_text(ui_txtProtAnaReceived, "");
}

// ---------------------------------------------------------------------
// void event_replay_protocol_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_replay_protocol_analyzer(lv_event_t *e)
{
  Print_Debug("event_replay_protocol_analyzer");

  bool inRecordingMode = false;

  // Check if recording
  if (currentState == STATE_ANALYZER)
  {
    inRecordingMode = true;
    // Stop
    lv_obj_clear_state(ui_swtProtAnaRxEn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblProtAnaRXEn, "RX OFF");
    SUBGHZ.disableRCSwitch();
    currentState = STATE_IDLE;
  }

  // Send Last Signal
  float freq = String(lv_textarea_get_text(ui_txtMainFreq)).toFloat();
  SUBGHZ.setFrequency(freq);
  SUBGHZ.enableTransmit();
  SUBGHZ.sendLastSignal();
  SUBGHZ.disableTransmit();

  Print_Debug(String(String("Signal transmitted, value: ") +
                     String(lv_textarea_get_text(ui_txtProtAnaReceived)) +
                     String(" (") + String(lv_textarea_get_text(ui_txtProtAnaBitLength)) + String(" bit)") +
                     String(" - Protocol: ") + String(lv_textarea_get_text(ui_txtProtAnaProtocol)) +
                     String(" - Frequency: ") + String(freq) + String(" mHz"))
                  .c_str());

  // Back to recording
  if (inRecordingMode)
  {
    // Start
    float freq = String(lv_textarea_get_text(ui_txtMainFreq)).toFloat();
    lv_obj_add_state(ui_swtProtAnaRxEn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblProtAnaRXEn, "RX ON");
    SUBGHZ.setFrequency(freq);
    SUBGHZ.enableRCSwitch();
    currentState = STATE_ANALYZER;
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Events in RC Switch Apps Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_rc_switch_send_on(lv_event_t *e)
// ---------------------------------------------------------------------
void event_rc_switch_send_on(lv_event_t *e)
{
  Print_Debug("event_rc_switch_send_on");

  SUBGHZ.setFrequency(String(lv_textarea_get_text(ui_txt10PoleFreq)).toFloat());

  lv_label_set_text(ui_lblRCSWStatus, "TX 'On' Command.");

  String FirstFive = lv_label_get_text(ui_lblBit0);
  FirstFive += lv_label_get_text(ui_lblBit1);
  FirstFive += lv_label_get_text(ui_lblBit2);
  FirstFive += lv_label_get_text(ui_lblBit3);
  FirstFive += lv_label_get_text(ui_lblBit4);

  String SecondFive = lv_label_get_text(ui_lblBit5);
  SecondFive += lv_label_get_text(ui_lblBit6);
  SecondFive += lv_label_get_text(ui_lblBit7);
  SecondFive += lv_label_get_text(ui_lblBit8);
  SecondFive += lv_label_get_text(ui_lblBit9);

  SUBGHZ.enableTransmit();
  SUBGHZ.switchOn(String(FirstFive).c_str(), String(SecondFive).c_str());
  SUBGHZ.disableTransmit();

  // delay(1000);
  String TxResult = "TX ON: ";
  TxResult += FirstFive;
  TxResult += SecondFive;

  Print_Debug(String(String("Send switch ON, value: ") + String(FirstFive) + String(SecondFive) + String(" - Frequency: ") + String(String(lv_textarea_get_text(ui_txt10PoleFreq)).toFloat()) + String(" mHz")).c_str());
  lv_label_set_text(ui_lblRCSWStatus, String(TxResult).c_str());
}

// ---------------------------------------------------------------------
// void event_rc_switch_send_off(lv_event_t *e)
// ---------------------------------------------------------------------
void event_rc_switch_send_off(lv_event_t *e)
{
  Print_Debug("event_rc_switch_send_off");

  SUBGHZ.setFrequency(String(lv_textarea_get_text(ui_txt10PoleFreq)).toFloat());

  lv_label_set_text(ui_lblRCSWStatus, String(SUBGHZ.getFrequency()).c_str()); //"TX 'Off' Command.");

  String FirstFive = lv_label_get_text(ui_lblBit0);
  FirstFive += lv_label_get_text(ui_lblBit1);
  FirstFive += lv_label_get_text(ui_lblBit2);
  FirstFive += lv_label_get_text(ui_lblBit3);
  FirstFive += lv_label_get_text(ui_lblBit4);

  String SecondFive = lv_label_get_text(ui_lblBit5);
  SecondFive += lv_label_get_text(ui_lblBit6);
  SecondFive += lv_label_get_text(ui_lblBit7);
  SecondFive += lv_label_get_text(ui_lblBit8);
  SecondFive += lv_label_get_text(ui_lblBit9);

  SUBGHZ.enableTransmit();
  SUBGHZ.switchOff(String(FirstFive).c_str(), String(SecondFive).c_str());
  SUBGHZ.disableTransmit();

  // delay(1000);
  String TxResult = "TX OFF: ";
  TxResult += FirstFive;
  TxResult += SecondFive;

  Print_Debug(String(String("Send switch OFF, value: ") + String(FirstFive) + String(SecondFive) + String(" - Frequency: ") + String(String(lv_textarea_get_text(ui_txt10PoleFreq)).toFloat()) + String(" mHz")).c_str());
  lv_label_set_text(ui_lblRCSWStatus, String(TxResult).c_str());
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Events in WiFi Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_start_wifi_scan(lv_event_t *e)
// ---------------------------------------------------------------------
void event_start_wifi_scan(lv_event_t *e)
{
  Print_Debug("event_start_wifi_scan");

  lv_label_set_text(ui_lblWifiScanNetsFound, "Scanning..");

  // Reset scan state
  scanFinished = false;
  WiFi.scanDelete();

  // Clear the textarea
  lv_textarea_set_text(ui_txtWifiScanNetsFound, "");

  // Clear the dropdown
  lv_dropdown_clear_options(ui_ddlWifiSSID);

  WiFi.disconnect(true);

  WiFi.onEvent(WiFiEvent);

  // Scan for Wi-Fi networks
  int numNetworks = WiFi.scanNetworks(true);

  currentState = STATE_WIFI_SCAN;
}

// ---------------------------------------------------------------------
// void event_refresh_wifi(lv_event_t *e)
// ---------------------------------------------------------------------
void event_refresh_wifi(lv_event_t *e)
{
  Print_Debug("event_refresh_wifi");

  int16_t result = WiFi.scanComplete();

  if (result > 0)
  {
    // Clear the textarea
    lv_textarea_set_text(ui_txtWifiScanNetsFound, "");

    int index = lv_dropdown_get_selected(ui_ddlWifiSSID);
    String ssid;
    int32_t rssi;
    uint8_t encryptionType;
    uint8_t *bssid;
    int32_t channel;

    WiFi.getNetworkInfo(index, ssid, encryptionType, rssi, bssid, channel);

    String MAC = String(bssid[0], HEX) + ":" + String(bssid[1], HEX) + ":" + String(bssid[2], HEX) + ":" + String(bssid[3], HEX) + ":" + String(bssid[4], HEX) + ":" + String(bssid[5], HEX);
    MAC.toUpperCase();

    Print_Debug(String(String("View Network, SSID: ") + String(ssid) + String(" | MAC: ") + String(MAC) + String(" | RSSI: ") + String(rssi) + String(" dBm | Channel: ") + String(channel) + String(" | Encryption Type: ") + String(GetEncryptionTypeString(encryptionType))).c_str());

    // Update the textarea
    lv_textarea_add_text(ui_txtWifiScanNetsFound, String(String("SSID: ") + String(ssid) + String("\nMAC: ") + String(MAC) + String("\nRSSI: ") + String(rssi) + String(" dBm\n") + String("Channel: ") + String(channel) + String("\nEncryption Type: ") + String(GetEncryptionTypeString(encryptionType)) + String("\n\n")).c_str());
  }
}

void JoinWifi(const char* ssid, const char* password) {
    // Start WiFi
    Serial.printf("Connecting to SSID: %s\n", ssid);
    Print_Debug("Connecting to SSID: %s\n", ssid);
    lv_textarea_set_text(ui_txtWifiScanNetsFound,"");
    delay(5);
    lv_textarea_set_text(ui_txtWifiScanNetsFound,"Connecting.");
    WiFi.begin(ssid, password);

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 100) {
        Serial.print(".");
        lv_textarea_add_text(ui_txtWifiScanNetsFound,".");
        delay(100);
        retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        // Show indicator icons
        lv_obj_clear_flag(ui_imgWifi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_imgWifi2, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(ui_txtWifiScanNetsFound,"CONNECTED");
        Print_Debug("\nConnected! ");
        Print_Debug("\nConnected!");
        Print_Debug("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nFailed to connect to WiFi.");
        Print_Debug("\nFailed to connect to WiFi.");
        lv_textarea_add_text(ui_txtWifiScanNetsFound,"FAILED TO CONNECT");
    }
}


// ---------------------------------------------------------------------
// void event_join_wifi(lv_event_t *e)
// ---------------------------------------------------------------------
void event_join_wifi(lv_event_t *e) {
    Print_Debug("event_join_wifi");

    // Get the selected SSID
    int index = lv_dropdown_get_selected(ui_ddlWifiSSID);
    char selected_ssid[64];
    lv_dropdown_get_selected_str(ui_ddlWifiSSID, selected_ssid, 64);

    // Get the password
    const char* entered_pass = lv_textarea_get_text(ui_txtwifiKey);

    if (strlen(selected_ssid) == 0 || !entered_pass || strlen(entered_pass) == 0) {
        Print_Debug("SSID or password is empty or null!");
        Serial.println("SSID or password is empty or null!");
        return;
    }

    // Call JoinWifi with the selected SSID and password
    JoinWifi(selected_ssid, entered_pass);
}

// ---------------------------------------------------------------------
// void evtKybdReady(lv_event_t *e)
// ---------------------------------------------------------------------
void evtKybdReady(lv_event_t * e)
{
  Print_Debug("KybdReady");
  //Set value in kybdvalue variable
  kybdvalue=lv_textarea_get_text(ui_txtKybdText);
  lv_textarea_set_text(ui_txtwifiKey,string(kybdvalue).c_str());
  lv_scr_load(ui_scrWifiApps); 
  
   
  //lv_textarea_set_text(ui_txtKybdText,"");
  
}

// ---------------------------------------------------------------------
// void evtKybdCancel(lv_event_t *e)
// ---------------------------------------------------------------------
void evtKybdCancel(lv_event_t * e)
{
  Print_Debug("KybdCancel");
  lv_textarea_set_text(ui_txtKybdText,"");
  lv_scr_load(ui_scrWifiApps);
}



// ---------------------------------------------------------------------
// void event_exit_wifi_screen(lv_event_t *e)
// ---------------------------------------------------------------------
void event_exit_wifi_screen(lv_event_t *e)
{
  Print_Debug("event_exit_wifi_screen");

  // delete old config
  WiFi.scanDelete();
  WiFi.removeEvent(WiFiEvent);
  WiFi.disconnect(true);

  lv_scr_load(ui_scrMain);
}


// ---------------------------------------------------------------------
// void event_save_capture_rec_play(lv_event_t * e);
// ---------------------------------------------------------------------
void event_save_capture_rec_play(lv_event_t * e)
{
  //SUBGHZ.saveSamples();
}

// ---------------------------------------------------------------------
// void fcnBleScreen(lv_event_t * e);
// ---------------------------------------------------------------------
void fcnBleScreen(lv_event_t * e)
{
  Print_Debug("event_load_screen_ble");
lv_scr_load(ui_scrBLEApps);


}

// ---------------------------------------------------------------------
// void event_exit_ble_screen(lv_event_t * e);
// ---------------------------------------------------------------------
void event_exit_ble_screen(lv_event_t * e)
{
  Print_Debug("event_exit_ble__screen");
currentState=STATE_IDLE;
lv_scr_load(ui_scrMain);

}

// ---------------------------------------------------------------------
// void fcnBLEToggle(lv_event_t * e);
// ---------------------------------------------------------------------
void fcnBLEToggle(lv_event_t * e)
{
    // Only act on the value-changed event
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    // Get the switch object
    lv_obj_t * sw = lv_event_get_target(e);
    bool now_on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (now_on) {
        // User just turned it ON
        bleSpammer.start();
        lv_label_set_text(ui_lblBLEEnable, "BLE ON");
    } else {
        // User just turned it OFF
        bleSpammer.stop();
        lv_label_set_text(ui_lblBLEEnable, "BLE OFF");
    }
}



// ---------------------------------------------------------------------
// void fcnBLEType(lv_event_t * e);
// ---------------------------------------------------------------------
void fcnBLEType(lv_event_t * e)
{

}

