#include "FlipperSubFile.h"
#include "lvgl.h"

const std::map<CC1101Preset, std::string> FlipperSubFile::presetMapping = {
    {AM270, "FuriHalSubGhzPresetOok270Async"},
    {AM650, "FuriHalSubGhzPresetOok650Async"},
    {FM238, "FuriHalSubGhzPreset2FSKDev238Async"},
    {FM476, "FuriHalSubGhzPreset2FSKDev476Async"},
    {CUSTOM, "FuriHalSubGhzPresetCustom"}
};

void FlipperSubFile::generateRaw(
    File& file,
    const CC1101Preset& presetName,
    const std::vector<byte>& customPresetData,
    std::stringstream& samples,
    float frequency
) {
    writeHeader(file, frequency);
    writePresetInfo(file, presetName, customPresetData);
    writeRawProtocolData(file, samples);
}

void FlipperSubFile::writeHeader(File& file, float frequency) {
    file.println("Filetype: Flipper SubGhz RAW File");
    file.println("Version: 1");
    file.print("Frequency: ");
    file.print(frequency * 1e6, 0);
    file.println();
}

void FlipperSubFile::writePresetInfo(File& file, const CC1101Preset& presetName, const std::vector<byte>& customPresetData) {
    file.print("Preset: ");
    file.println(getPresetName(presetName).c_str());
    if (presetName == CC1101Preset::CUSTOM) {
        file.println("Custom_preset_module: CC1101");
        file.print("Custom_preset_data: ");
        for (size_t i = 0; i < customPresetData.size(); ++i) {
            char hexStr[3];
            sprintf(hexStr, "%02X", customPresetData[i]);
            file.print(hexStr);
            if (i < customPresetData.size() - 1) {
                file.print(" ");
            }
        }
        file.println();
    }
}

void FlipperSubFile::writeRawProtocolData(File& file, std::stringstream& samples) {
    file.println("Protocol: RAW");
    file.print("RAW_Data: ");

    std::string sample;
    std::vector<std::string> buffer;
    buffer.reserve(512);
    int tokenCount = 0;

    while (std::getline(samples, sample, ' ')) {
        if (!sample.empty()) {
            buffer.push_back(sample);
            tokenCount++;
        }

        if (tokenCount >= 512) {
            // Batch write
            std::string line;
            for (size_t i = 0; i < buffer.size(); ++i) {
                if (i) line += ' ';
                line += buffer[i];
            }
            file.println(line.c_str());
            lv_timer_handler(); // keep LVGL alive
            buffer.clear();
            tokenCount = 0;
            file.print("RAW_Data: ");
        }
    }

    // Write any remaining tokens
    if (!buffer.empty()) {
        std::string line;
        for (size_t i = 0; i < buffer.size(); ++i) {
            if (i) line += ' ';
            line += buffer[i];
        }
        file.println(line.c_str());
    }

    file.println();
}

std::string FlipperSubFile::getPresetName(const CC1101Preset& preset) {
    auto it = presetMapping.find(preset);
    if (it != presetMapping.end()) {
        return it->second;
    } else {
        return "FuriHalSubGhzPresetCustom";
    }
}
