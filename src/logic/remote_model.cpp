#include "logic/remote_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

RemoteKey remoteKeyFor(const Key& k) {
    if (k.code == KeyCode::Enter) return RemoteKey::Enter;
    if (k.code == KeyCode::Backspace) return RemoteKey::Back;
    if (k.code != KeyCode::Char) return RemoteKey::None;
    switch (k.ch) {
        case ';': return RemoteKey::Up;      // printed arrow up
        case '.': return RemoteKey::Down;    // printed arrow down
        case ',': return RemoteKey::Left;    // printed arrow left
        case '/': return RemoteKey::Right;   // printed arrow right
        case ' ': return RemoteKey::AutoFocus;
        case 'h': return RemoteKey::Help;
        case '`': return RemoteKey::Back;  // printed esc
        case 'f': return RemoteKey::JumpFocus;
        case 'i': return RemoteKey::JumpIris;
        case 's': return RemoteKey::JumpShutter;
        case 'o': return RemoteKey::JumpIso;
        case 'w': return RemoteKey::JumpWb;
        case 't': return RemoteKey::JumpTint;
        case 'c': return RemoteKey::JumpFeed;
        default: return RemoteKey::None;
    }
}

bool remoteKeyRepeats(RemoteKey k) {
    return k == RemoteKey::Left || k == RemoteKey::Right || k == RemoteKey::Up ||
           k == RemoteKey::Down;
}

void RemoteModel::clampFeedDisplay(const CameraValues& v) {
    if (v.displayCount == 0) {
        feedDisplay_ = 0;
    } else if (feedDisplay_ >= (int)v.displayCount) {
        feedDisplay_ = (int)v.displayCount - 1;
    }
}

RemoteCommand RemoteModel::onKey(RemoteKey key, bool big, CameraValues& v) {
    RemoteCommand none;
    limitHit_ = false;
    if (help_) {  // any key closes help
        if (key != RemoteKey::None) help_ = false;
        return none;
    }
    switch (key) {
        case RemoteKey::Up:
        case RemoteKey::Down:
        case RemoteKey::ColLeft:
        case RemoteKey::ColRight: {
            const int tile = (int)remoteTileRow(row_);
            int col = tile / REMOTE_GRID_ROWS;
            int r = tile % REMOTE_GRID_ROWS;
            if (key == RemoteKey::Up) r = r > 0 ? r - 1 : r;
            if (key == RemoteKey::Down) r = r < REMOTE_GRID_ROWS - 1 ? r + 1 : r;
            if (key == RemoteKey::ColLeft) col = 0;
            if (key == RemoteKey::ColRight) col = 1;
            row_ = (RemoteRow)(col * REMOTE_GRID_ROWS + r);
            return none;
        }
        case RemoteKey::Left: return change(-1, big, v);
        case RemoteKey::Right: return change(+1, big, v);
        case RemoteKey::Enter: return rowAction(v);
        case RemoteKey::AutoFocus: {
            RemoteCommand c;
            c.type = RemoteCmd::AutoFocus;
            return c;
        }
        case RemoteKey::Help: help_ = true; return none;
        case RemoteKey::JumpFocus:
        case RemoteKey::JumpIris:
        case RemoteKey::JumpShutter:
        case RemoteKey::JumpIso:
        case RemoteKey::JumpWb:
        case RemoteKey::JumpFeed:
        case RemoteKey::JumpTint:
            row_ = (RemoteRow)((int)key - (int)RemoteKey::JumpFocus);
            return none;
        default: return none;
    }
}

RemoteCommand RemoteModel::change(int dir, bool big, CameraValues& v) {
    RemoteCommand c = changeValue(dir, big, v);
    // A step that changes nothing at a known value is a range limit.
    limitHit_ = c.type == RemoteCmd::None && row_ != RemoteRow::Feed && rowKnown(v);
    return c;
}

bool RemoteModel::rowKnown(const CameraValues& v) const {
    switch (row_) {
        case RemoteRow::Focus: return v.focusKnown && v.focusControllable;
        case RemoteRow::Iris: return v.irisKnown && v.irisControllable && !v.irisAuto;
        case RemoteRow::Iso: return v.isoKnown && v.isoCount > 0;
        case RemoteRow::Shutter: return v.shutterKnown && !v.shutterAuto && v.shutterListCount() > 0;
        case RemoteRow::Wb: return v.wbKnown;
        case RemoteRow::Tint: return v.tintKnown;
        default: return false;
    }
}

RemoteCommand RemoteModel::changeValue(int dir, bool big, CameraValues& v) {
    RemoteCommand c;
    switch (row_) {
        case RemoteRow::Focus: {
            if (!v.focusKnown || !v.focusControllable) return c;
            const float step = big ? FOCUS_BIG_STEP : FOCUS_STEP;
            const float next = std::min(1.0f, std::max(0.0f, v.focus + dir * step));
            if (next == v.focus) return c;
            v.focus = next;
            c.type = RemoteCmd::SetFocus;
            c.f = next;
            return c;
        }
        case RemoteRow::Iris: {
            if (!v.irisKnown || !v.irisControllable || v.irisAuto || v.irisStop <= 0) return c;
            // At the lens's own limits there is nothing to step to.
            if (dir < 0 && v.irisMin > 0 && v.irisStop <= v.irisMin + 0.05f) return c;
            if (dir > 0 && v.irisMax > 0 && v.irisStop >= v.irisMax - 0.05f) return c;
            // → closes the iris (bigger f-number), like the + button in the app.
            c.type = RemoteCmd::IrisStep;
            c.i = dir * (big ? IRIS_BIG_STEP : 1);
            return c;
        }
        case RemoteRow::Iso: {
            if (!v.isoKnown || v.isoCount == 0) return c;
            // Nearest list entry to the current ISO, then step from there.
            size_t idx = 0;
            for (size_t k = 1; k < v.isoCount; ++k) {
                if (std::abs(v.isoList[k] - v.iso) < std::abs(v.isoList[idx] - v.iso)) idx = k;
            }
            const int step = big ? ISO_BIG_STEP : 1;
            int next = (int)idx + dir * step;
            next = std::max(0, std::min((int)v.isoCount - 1, next));
            if (v.isoList[next] == v.iso) return c;
            v.iso = v.isoList[next];
            c.type = RemoteCmd::SetIso;
            c.i = v.iso;
            return c;
        }
        case RemoteRow::Shutter: {
            const size_t n = v.shutterListCount();
            if (!v.shutterKnown || v.shutterAuto || n == 0) return c;
            const int* list = v.shutterList();
            const int cur = v.shutterValue();
            size_t idx = 0;
            for (size_t k = 1; k < n; ++k) {
                if (std::abs(list[k] - cur) < std::abs(list[idx] - cur)) idx = k;
            }
            // -> = the next bigger number (1/125 -> 1/200, 180 -> 270 degrees).
            int next = (int)idx + dir * (big ? SHUTTER_BIG_STEP : 1);
            next = std::max(0, std::min((int)n - 1, next));
            if (list[next] == cur) return c;
            if (v.shutterAngleMode) {
                v.shutterAngle = list[next];
            } else {
                v.shutterSpeed = list[next];
            }
            c.type = RemoteCmd::SetShutter;
            c.i = list[next];
            c.b = v.shutterAngleMode;
            return c;
        }
        case RemoteRow::Wb: {
            if (!v.wbKnown) return c;
            const int step = big ? WB_BIG_STEP : WB_STEP;
            // Snap to the step grid so 5537 + 50 gives 5550, not 5587.
            int next = dir > 0 ? (v.wb / step + 1) * step : ((v.wb + step - 1) / step - 1) * step;
            next = std::max(v.wbMin, std::min(v.wbMax, next));
            if (next == v.wb) return c;
            v.wb = next;
            c.type = RemoteCmd::SetWb;
            c.i = next;
            return c;
        }
        case RemoteRow::Tint: {
            if (!v.tintKnown) return c;
            const int next = std::max(v.tintMin, std::min(v.tintMax, v.tint + dir * (big ? TINT_BIG_STEP : 1)));
            if (next == v.tint) return c;
            v.tint = next;
            c.type = RemoteCmd::SetTint;
            c.i = next;
            return c;
        }
        case RemoteRow::Feed: {
            if (v.displayCount == 0) return c;
            const int n = (int)v.displayCount;
            feedDisplay_ = ((feedDisplay_ + dir) % n + n) % n;
            return c;
        }
        default: return c;
    }
}

RemoteCommand RemoteModel::rowAction(CameraValues& v) {
    RemoteCommand c;
    switch (row_) {
        case RemoteRow::Focus: c.type = RemoteCmd::AutoFocus; return c;
        case RemoteRow::Wb: c.type = RemoteCmd::AutoWb; return c;
        case RemoteRow::Feed: {
            clampFeedDisplay(v);
            if (v.displayCount == 0) return c;
            const int i = feedDisplay_;
            v.cleanFeed[i] = !v.cleanFeed[i];
            v.cleanFeedKnown[i] = true;
            c.type = RemoteCmd::SetCleanFeed;
            c.display = i;
            c.b = v.cleanFeed[i];
            return c;
        }
        default: return c;
    }
}

void formatFocus(const CameraValues& v, char* out, size_t cap) {
    if (!v.focusKnown) {
        snprintf(out, cap, "--");
    } else {
        snprintf(out, cap, "%d%%", (int)std::lround(v.focus * 100.0f));
    }
}

void formatIris(const CameraValues& v, char* out, size_t cap) {
    if (!v.irisKnown || v.irisStop <= 0.0f) {
        snprintf(out, cap, "--");
        return;
    }
    float stop = v.irisStop;
    // At the end stops show the lens's own min/max (the camera reports
    // quarter stops that can read f/3.7 for an f/3.5 lens).
    if (v.irisMin > 0 && std::fabs(stop - v.irisMin) < 0.1f) stop = v.irisMin;
    if (v.irisMax > 0 && std::fabs(stop - v.irisMax) < 0.1f) stop = v.irisMax;
    if (stop < 10.0f) {
        snprintf(out, cap, "f/%.1f", stop);
    } else {
        snprintf(out, cap, "f/%d", (int)std::lround(stop));
    }
    if (v.irisAuto) {
        const size_t n = strlen(out);
        snprintf(out + n, cap > n ? cap - n : 0, " A");
    }
}

void formatIso(const CameraValues& v, char* out, size_t cap) {
    if (!v.isoKnown) {
        snprintf(out, cap, "--");
    } else {
        snprintf(out, cap, "%d", v.iso);
    }
}

void formatWb(const CameraValues& v, char* out, size_t cap) {
    if (!v.wbKnown) {
        snprintf(out, cap, "--");
    } else {
        snprintf(out, cap, "%dK", v.wb);
    }
}

void formatTint(const CameraValues& v, char* out, size_t cap) {
    if (!v.tintKnown) {
        snprintf(out, cap, "--");
    } else {
        snprintf(out, cap, "%+d", v.tint);
    }
}

void formatShutter(const CameraValues& v, char* out, size_t cap) {
    if (!v.shutterKnown) {
        snprintf(out, cap, "--");
    } else if (!v.shutterAngleMode) {
        snprintf(out, cap, "1/%d", v.shutterSpeed);
    } else if (v.shutterAngle % 100 == 0) {
        snprintf(out, cap, "%d", v.shutterAngle / 100);
    } else if (v.shutterAngle % 10 == 0) {
        snprintf(out, cap, "%d.%d", v.shutterAngle / 100, (v.shutterAngle / 10) % 10);
    } else {
        snprintf(out, cap, "%d.%02d", v.shutterAngle / 100, v.shutterAngle % 100);
    }
}

void shortDisplayName(const char* name, char* out, size_t cap) {
    if (strstr(name, "SDI")) {
        snprintf(out, cap, "SDI");
    } else if (strstr(name, "HDMI")) {
        snprintf(out, cap, "HDMI");
    } else if (strstr(name, "USB")) {
        snprintf(out, cap, "USB");
    } else {
        snprintf(out, cap, "%.5s", name);
    }
}
