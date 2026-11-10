#include "logic/camera_json.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "cJSON.h"

int CameraValues::displayIndex(const char* name) const {
    for (size_t i = 0; i < displayCount; ++i) {
        if (strcmp(displays[i], name) == 0) return (int)i;
    }
    return -1;
}

namespace {
const char* kMonitoringPrefix = "/monitoring/";
const char* kCleanFeedSuffix = "/cleanFeed";

const char* const kSubscribed[] = {
    "/lens/focus",         "/lens/focus/description", "/lens/iris",
    "/lens/iris/description", "/video/iso",         "/video/whiteBalance",
    "/video/whiteBalanceTint", "/camera/tallyStatus", "/transports/0/record",
};

bool getNumber(const cJSON* obj, const char* key, double& out) {
    const cJSON* n = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(n)) return false;
    out = n->valuedouble;
    return true;
}

bool getBool(const cJSON* obj, const char* key, bool& out) {
    const cJSON* b = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsBool(b)) return false;
    out = cJSON_IsTrue(b);
    return true;
}

bool getRange(const cJSON* obj, const char* key, double& min, double& max) {
    const cJSON* r = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsObject(r)) return false;
    double lo = 0, hi = 0;
    if (!getNumber(r, "min", lo) || !getNumber(r, "max", hi) || lo > hi) return false;
    min = lo;
    max = hi;
    return true;
}

void copyName(char* dst, size_t cap, const char* src) {
    snprintf(dst, cap, "%s", src);
}

// "/monitoring/<name>/cleanFeed" -> name; false for other paths.
bool cleanFeedDisplay(const char* path, char* name, size_t cap) {
    const size_t pre = strlen(kMonitoringPrefix);
    const size_t suf = strlen(kCleanFeedSuffix);
    const size_t len = strlen(path);
    if (len <= pre + suf || strncmp(path, kMonitoringPrefix, pre) != 0 ||
        strcmp(path + len - suf, kCleanFeedSuffix) != 0) {
        return false;
    }
    const size_t n = len - pre - suf;
    if (n == 0 || n >= cap || memchr(path + pre, '/', n)) return false;
    memcpy(name, path + pre, n);
    name[n] = '\0';
    return true;
}

bool applyDisplays(CameraValues& v, const cJSON* root) {
    const cJSON* arr = cJSON_IsArray(root) ? root : cJSON_GetObjectItemCaseSensitive(root, "displays");
    if (!cJSON_IsArray(arr)) return false;
    CameraValues before = v;
    v.displayCount = 0;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, arr) {
        if (!cJSON_IsString(item) || !item->valuestring[0]) continue;
        if (v.displayCount >= CAM_MAX_DISPLAYS) break;
        if (strlen(item->valuestring) > CAM_DISPLAY_NAME_MAX) continue;
        const size_t i = v.displayCount++;
        copyName(v.displays[i], sizeof(v.displays[i]), item->valuestring);
        // Keep what we knew about displays that are still there.
        const int old = before.displayIndex(item->valuestring);
        v.cleanFeedKnown[i] = old >= 0 && before.cleanFeedKnown[old];
        v.cleanFeed[i] = old >= 0 && before.cleanFeed[old];
    }
    return true;
}

bool applyObject(CameraValues& v, const char* path, const cJSON* root) {
    double d = 0, lo = 0, hi = 0;
    bool b = false;
    if (strcmp(path, "/monitoring/display") == 0) return applyDisplays(v, root);
    if (!cJSON_IsObject(root)) return false;

    if (strcmp(path, "/system/product") == 0) {
        const cJSON* n = cJSON_GetObjectItemCaseSensitive(root, "deviceName");
        if (!cJSON_IsString(n)) n = cJSON_GetObjectItemCaseSensitive(root, "productName");
        if (cJSON_IsString(n)) copyName(v.name, sizeof(v.name), n->valuestring);
        return true;
    }
    if (strcmp(path, "/lens/focus") == 0) {
        if (getNumber(root, "normalised", d)) {
            v.focus = (float)std::min(1.0, std::max(0.0, d));
            v.focusKnown = true;
        }
        return true;
    }
    if (strcmp(path, "/lens/focus/description") == 0) {
        if (getBool(root, "controllable", b)) v.focusControllable = b;
        return true;
    }
    if (strcmp(path, "/lens/iris") == 0) {
        if (getNumber(root, "apertureStop", d)) {
            v.irisStop = (float)d;
            v.irisKnown = true;
        }
        if (getNumber(root, "normalised", d)) v.irisNormalised = (float)d;
        if (getBool(root, "continuousApertureAutoExposure", b)) v.irisAuto = b;
        return true;
    }
    if (strcmp(path, "/lens/iris/description") == 0) {
        if (getRange(root, "apertureStop", lo, hi)) {
            v.irisMin = (float)lo;
            v.irisMax = (float)hi;
        }
        if (getBool(root, "controllable", b)) v.irisControllable = b;
        return true;
    }
    if (strcmp(path, "/video/iso") == 0) {
        if (getNumber(root, "iso", d)) {
            v.iso = (int)d;
            v.isoKnown = true;
        }
        return true;
    }
    if (strcmp(path, "/video/supportedISOs") == 0) {
        const cJSON* arr = cJSON_GetObjectItemCaseSensitive(root, "supportedISOs");
        if (!cJSON_IsArray(arr)) return false;
        size_t n = 0;
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, arr) {
            if (cJSON_IsNumber(item) && item->valueint > 0 && n < CAM_MAX_ISOS) {
                v.isoList[n++] = item->valueint;
            }
        }
        std::sort(v.isoList, v.isoList + n);
        v.isoCount = (size_t)(std::unique(v.isoList, v.isoList + n) - v.isoList);
        return true;
    }
    if (strcmp(path, "/video/whiteBalance") == 0) {
        if (getNumber(root, "whiteBalance", d)) {
            v.wb = (int)d;
            v.wbKnown = true;
        }
        return true;
    }
    if (strcmp(path, "/video/whiteBalance/description") == 0) {
        if (getRange(root, "whiteBalance", lo, hi)) {
            v.wbMin = (int)lo;
            v.wbMax = (int)hi;
        }
        return true;
    }
    if (strcmp(path, "/video/whiteBalanceTint") == 0) {
        if (getNumber(root, "whiteBalanceTint", d)) {
            v.tint = (int)d;
            v.tintKnown = true;
        }
        return true;
    }
    if (strcmp(path, "/video/whiteBalanceTint/description") == 0) {
        if (getRange(root, "whiteBalanceTint", lo, hi)) {
            v.tintMin = (int)lo;
            v.tintMax = (int)hi;
        }
        return true;
    }
    if (strcmp(path, "/camera/tallyStatus") == 0) {
        const cJSON* st = cJSON_GetObjectItemCaseSensitive(root, "status");
        if (cJSON_IsString(st)) {
            const char* s = st->valuestring;
            v.tally = strcmp(s, "Program") == 0   ? CameraValues::Tally::Program
                      : strcmp(s, "Preview") == 0 ? CameraValues::Tally::Preview
                                                  : CameraValues::Tally::None;
            v.tallyKnown = true;
        }
        return true;
    }
    if (strcmp(path, "/transports/0/record") == 0) {
        if (getBool(root, "recording", b)) {
            v.recording = b;
            v.recordKnown = true;
        }
        return true;
    }
    char display[CAM_DISPLAY_NAME_MAX + 1];
    if (cleanFeedDisplay(path, display, sizeof(display))) {
        const int i = v.displayIndex(display);
        if (i < 0) return false;
        if (getBool(root, "enabled", b)) {
            v.cleanFeed[i] = b;
            v.cleanFeedKnown[i] = true;
        }
        return true;
    }
    return false;
}
}  // namespace

bool cameraApplyJson(CameraValues& v, const char* path, const char* body, size_t len) {
    if (!path || !body) return false;
    cJSON* root = cJSON_ParseWithLength(body, len);
    if (!root) return false;
    const bool ok = applyObject(v, path, root);
    cJSON_Delete(root);
    return ok;
}

CameraWsKind cameraApplyWsMessage(CameraValues& v, const char* msg, size_t len) {
    cJSON* root = cJSON_ParseWithLength(msg, len);
    if (!root) return CameraWsKind::Other;
    CameraWsKind kind = CameraWsKind::Other;
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON* action = cJSON_GetObjectItemCaseSensitive(data, "action");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "event") == 0 && cJSON_IsString(action)) {
        if (strcmp(action->valuestring, "websocketOpened") == 0) {
            kind = CameraWsKind::Opened;
        } else if (strcmp(action->valuestring, "propertyValueChanged") == 0) {
            const cJSON* prop = cJSON_GetObjectItemCaseSensitive(data, "property");
            const cJSON* value = cJSON_GetObjectItemCaseSensitive(data, "value");
            if (cJSON_IsString(prop) && value && applyObject(v, prop->valuestring, value)) {
                kind = CameraWsKind::PropertyChanged;
            }
        }
    }
    cJSON_Delete(root);
    return kind;
}

size_t cameraSubscribeMessage(const CameraValues& v, char* out, size_t cap) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "request");
    cJSON* data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "action", "subscribe");
    cJSON* props = cJSON_AddArrayToObject(data, "properties");
    for (const char* p : kSubscribed) cJSON_AddItemToArray(props, cJSON_CreateString(p));
    for (size_t i = 0; i < v.displayCount; ++i) {
        char p[64];
        snprintf(p, sizeof(p), "%s%s%s", kMonitoringPrefix, v.displays[i], kCleanFeedSuffix);
        cJSON_AddItemToArray(props, cJSON_CreateString(p));
    }
    const bool ok = cJSON_PrintPreallocated(root, out, (int)cap, false);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}

size_t cameraEncodePathSegment(const char* in, char* out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (; *in; ++in) {
        const unsigned char c = (unsigned char)*in;
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        const size_t need = plain ? 1 : 3;
        if (n + need + 1 > cap) return 0;
        if (plain) {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 0x0F];
        }
    }
    if (cap == 0) return 0;
    out[n] = '\0';
    return n;
}
