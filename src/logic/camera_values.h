#pragma once

#include <cstddef>
#include <cstdint>

// What the remote knows about the camera. Filled from REST GETs and
// WebSocket events (logic/camera_json), changed optimistically by the remote
// model. Pure data.

constexpr size_t CAM_NAME_MAX = 40;
constexpr size_t CAM_MAX_ISOS = 40;
constexpr size_t CAM_MAX_DISPLAYS = 4;
constexpr size_t CAM_DISPLAY_NAME_MAX = 20;

struct CameraValues {
    bool connected = false;
    char name[CAM_NAME_MAX + 1] = "";

    bool focusKnown = false;
    float focus = 0.0f;  // normalised 0 (near) .. 1 (far)
    bool focusControllable = true;

    bool irisKnown = false;
    float irisStop = 0.0f;  // f-number, 0 = no lens contacts
    float irisMin = 0.0f;   // widest f-number from /lens/iris/description
    float irisMax = 0.0f;
    float irisNormalised = 0.0f;
    bool irisControllable = true;
    bool irisAuto = false;  // continuousApertureAutoExposure

    bool isoKnown = false;
    int iso = 0;
    int isoList[CAM_MAX_ISOS] = {};
    size_t isoCount = 0;

    bool wbKnown = false;
    int wb = 0;
    int wbMin = 2500;
    int wbMax = 10000;

    bool tintKnown = false;
    int tint = 0;
    int tintMin = -50;
    int tintMax = 50;

    enum class Tally : uint8_t { None, Preview, Program };
    bool tallyKnown = false;
    Tally tally = Tally::None;
    bool recordKnown = false;
    bool recording = false;

    // /camera/power: where the camera's power comes from and its battery.
    enum class PowerSource : uint8_t { Unknown, Battery, Ac, Usb, Other };
    bool powerKnown = false;
    PowerSource powerSource = PowerSource::Unknown;
    bool batteryPresent = false;
    bool batteryCharging = false;
    int batteryPercent = -1;  // -1: not reported

    size_t displayCount = 0;
    char displays[CAM_MAX_DISPLAYS][CAM_DISPLAY_NAME_MAX + 1] = {};
    bool cleanFeedKnown[CAM_MAX_DISPLAYS] = {};
    bool cleanFeed[CAM_MAX_DISPLAYS] = {};

    // Index of `name` in displays, or -1.
    int displayIndex(const char* name) const;
};
