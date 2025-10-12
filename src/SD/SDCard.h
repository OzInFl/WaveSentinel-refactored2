#ifndef SDCard_h
#define SDCard_h

#include <lvgl.h>
#include <ui.h>

#include "Misc/Config.h"
#include "Arduino.h"

#include <SD.h>
#include <SPI.h>

// Create arrays to hold directory and file names
#define MAX_CONTENT 50

bool sdCardPresent = false;

// Begin and Stop SD each time for less memory usage

// ---------------------------------------------------------------------
// bool sd_card_is_present()
// ---------------------------------------------------------------------
bool sd_card_is_present()
{
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI);
    //SPI.setFrequency(1000000);

    if (!SD.begin(SD_CS))
    {
        Serial.println("Card Mount Failed");
        sdCardPresent = false;
        return sdCardPresent;
    }

    uint8_t cardType = SD.cardType();

    if (cardType == CARD_NONE)
    {
        Serial.println("No SD card attached");
        sdCardPresent = false;
        return sdCardPresent;
    }

    sdCardPresent = true;
    return sdCardPresent;
}

// ---------------------------------------------------------------------
// void now_close_sd_card()
// ---------------------------------------------------------------------
void now_close_sd_card()
{
    SD.end();
}

// ---------------------------------------------------------------------
// void refresh_sd_card_folder(lv_obj_t * obj, const char *dirname)
// ---------------------------------------------------------------------
void refresh_sd_card_folder(lv_obj_t *obj, const char *dirname)
{
    Serial.printf("refresh_sd_card_folder: %s\n", dirname);

    File root = SD.open(dirname);

    if (!root)
    {
        Serial.printf("Failed to open directory: %s\n", dirname);
        return;
    }
    if (!root.isDirectory())
    {
        Serial.println("Not a directory");
        return;
    }

    lv_dropdown_clear_options(obj);

    File file = root.openNextFile();
    int i = 0;

    lv_dropdown_add_option(obj, "/", LV_DROPDOWN_POS_LAST);

    while (file)
    {
        if (file.isDirectory())
        {
                lv_dropdown_add_option(obj, file.name(), LV_DROPDOWN_POS_LAST);
                i++;
                if (i >= MAX_CONTENT)
                {
                    break;
                }
        }

        file = root.openNextFile();
    }

    root.close();
    file.close();
}

// ---------------------------------------------------------------------
// void refresh_sd_card_file(lv_obj_t *obj, const char *dirname, const char *extension, bool clear)
// ---------------------------------------------------------------------
void refresh_sd_card_file(lv_obj_t *obj, const char *dirname, const char *extension, bool clear)
{
    Serial.printf("refresh_sd_card_file: %s\n", dirname);

    File root = SD.open(dirname);

    if (!root)
    {
        Serial.printf("Failed to open directory: %s\n", dirname);
        return;
    }
    if (!root.isDirectory())
    {
        Serial.println("Not a directory");
        return;
    }

    if (clear)
    {
        lv_dropdown_clear_options(obj);
    }

    File file = root.openNextFile();
    int i = 0;

    while (file)
    {
        if (!file.isDirectory())
        {
            if (strcmp(String(file.name()).substring(String(file.name()).length() - String(extension).length(), String(file.name()).length()).c_str(), extension) == 0)
            {
                lv_dropdown_add_option(obj, file.name(), LV_DROPDOWN_POS_LAST);
                i++;
                if (i >= MAX_CONTENT)
                {
                    break;
                }
            }
        }

        file = root.openNextFile();
    }

    root.close();
    file.close();
}



#define MAX_LENGHT_RAW_ARRAY 4096

float tempFreq;
int tempSample[MAX_LENGHT_RAW_ARRAY];
int tempSampleCount;

// ---------------------------------------------------------------------
// bool read_sd_card_flipper_file(String filename)
// ---------------------------------------------------------------------
bool read_sd_card_flipper_file(String filename)
{
  Print_Debug("Read Flipper File");

    File file = SD.open(filename, FILE_READ);
    if (!file)
    {
        Serial.println("Failed to open file: " + String(filename));
        return false;
    }

    // Reset Current
    memset(tempSample, 0, sizeof(MAX_LENGHT_RAW_ARRAY));       
    tempSampleCount = 0;

    char *buf = (char *) malloc(MAX_LENGHT_RAW_ARRAY);
    String line = "";

    while (file.available())
    {
        line = file.readStringUntil('\n');
        line.toCharArray(buf, MAX_LENGHT_RAW_ARRAY);
        const char sep[2] = ":";
        const char values_sep[2] = " ";

        char *key = strtok(buf, sep);
        char *value;

        if (key != NULL)
        {
            value = strtok(NULL, sep);

            if (!strcmp(key, "Frequency"))
            {
                tempFreq = atoi(value) / 1000000.0f;
            }

            if (!strcmp(key, "RAW_Data"))
            {
                char *pulse = strtok(value, values_sep);
                int i;
                while (pulse != NULL && tempSampleCount < MAX_LENGHT_RAW_ARRAY)
                {
                    tempSample[tempSampleCount] = atoi(pulse);
                    pulse = strtok(NULL, values_sep);
                    tempSampleCount++;
                }
            }
        }
    }

    file.close();

    free(buf);

  return true;
}

#endif
