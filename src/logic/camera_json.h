#pragma once

#include <cstddef>

#include "logic/camera_values.h"

// Parses camera REST bodies and WebSocket events into CameraValues. The REST
// path of a GET and the WebSocket "property" are the same strings
// ("/lens/focus", "/monitoring/HDMI/cleanFeed", ...), so one function handles
// both. Keys missing from a body mean "unchanged". Pure logic (cJSON).

// Applies the JSON `body` read from (or reported for) `path`. Returns true if
// the path is one the remote uses and the body parsed.
bool cameraApplyJson(CameraValues& v, const char* path, const char* body, size_t len);

enum class CameraWsKind { Other, Opened, PropertyChanged };

// Handles one WebSocket text message. For a propertyValueChanged event it
// applies the value and returns PropertyChanged; "websocketOpened" returns
// Opened (time to subscribe).
CameraWsKind cameraApplyWsMessage(CameraValues& v, const char* msg, size_t len);

// The subscribe request for the properties the remote shows. Returns the
// length written (0 if it does not fit).
size_t cameraSubscribeMessage(const CameraValues& v, char* out, size_t cap);

// Percent-encodes a monitoring display name for use in a URL path segment.
size_t cameraEncodePathSegment(const char* in, char* out, size_t cap);
