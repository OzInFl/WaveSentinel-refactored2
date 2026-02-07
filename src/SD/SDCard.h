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

// Dedicated SPI bus for SD card (HSPI/SPI3) so it doesn't conflict with CC1101 on default SPI
SPIClass sdSPI(HSPI);

// SD is mounted/unmounted for each operation to save memory and
// support hot-swap — card can be removed and re-inserted at any time.

// ---------------------------------------------------------------------
// bool sd_card_is_present()
// Mounts the SD card. Handles hot-swap by ending any stale session
// first and retrying once if the initial mount fails.
// ---------------------------------------------------------------------
bool sd_card_is_present()
{
    // End any previous SD session to handle hot-swap cleanly.
    // Without this, SD.begin() can fail if the card was removed and
    // re-inserted while a previous session was still "open".
    SD.end();

    sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

    if (!SD.begin(SD_CS, sdSPI))
    {
        // Retry once — hot-swapped cards sometimes need a second attempt
        // after the SPI bus is re-initialized.
        vTaskDelay(pdMS_TO_TICKS(100));
        if (!SD.begin(SD_CS, sdSPI))
        {
            Serial.println("Card Mount Failed");
            sdCardPresent = false;
            return sdCardPresent;
        }
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
// Extract just the filename from a path (handles ESP32 file.name() returning full paths)
static const char* sd_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return (slash && slash[1]) ? slash + 1 : path;
}

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
                lv_dropdown_add_option(obj, sd_basename(file.name()), LV_DROPDOWN_POS_LAST);
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

    size_t extLen = strlen(extension);

    while (file)
    {
        if (!file.isDirectory())
        {
            const char *name = sd_basename(file.name());
            size_t nameLen = strlen(name);
            // Check if filename ends with the extension
            if (nameLen >= extLen && strcmp(name + nameLen - extLen, extension) == 0)
            {
                lv_dropdown_add_option(obj, name, LV_DROPDOWN_POS_LAST);
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
    memset(tempSample, 0, sizeof(tempSample));
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
