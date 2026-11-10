#include "logic/ui_nav.h"

namespace {
bool isJump(RemoteKey k) {
    return k == RemoteKey::JumpFocus || k == RemoteKey::JumpIris || k == RemoteKey::JumpIso ||
           k == RemoteKey::JumpWb || k == RemoteKey::JumpTint || k == RemoteKey::JumpFeed;
}
}  // namespace

void UiNav::startIntro() {
    view_ = UiView::Intro;
    introCard_ = 0;
    introAdvanceAt_ = 0;
}

NavKeys UiNav::onKey(RemoteKey k, uint32_t nowMs, bool connected) {
    NavKeys out;
    if (k == RemoteKey::None) return out;
    lastKeyMs_ = nowMs;

    if (view_ == UiView::Help) {
        if (k == RemoteKey::Help) {
            helpPage_ ^= 1;
        } else if (k == RemoteKey::Enter && helpPage_ == 1) {  // "Enter: replay intro"
            startIntro();
        } else {
            view_ = UiView::Overview;
        }
        return out;  // the key itself is not executed
    }
    if (k == RemoteKey::Help) {
        view_ = UiView::Help;
        helpPage_ = 0;
        return out;
    }
    if (view_ == UiView::Intro) {
        if (introCard_ == 0 && k == RemoteKey::AutoFocus && connected) {
            // Teach by doing: really run autofocus, advance when it is done.
            out.add(RemoteKey::JumpFocus);
            out.add(RemoteKey::AutoFocus);
            return out;
        }
        if (introCard_ == 0) {
            introCard_ = 1;
        } else {
            view_ = UiView::Overview;
            introFinished_ = true;
            out.add(RemoteKey::JumpFocus);
        }
        return out;
    }
    if (!connected) return out;

    const bool detail = view_ == UiView::Detail;
    switch (k) {
        case RemoteKey::Up:
        case RemoteKey::Down:
            if (detail) {
                out.add(k == RemoteKey::Up ? RemoteKey::Right : RemoteKey::Left);
                out.big = true;
            } else {
                out.add(k);
            }
            return out;
        case RemoteKey::Left:
        case RemoteKey::Right:
            if (detail) {
                out.add(k);
            } else {
                out.add(k == RemoteKey::Left ? RemoteKey::ColLeft : RemoteKey::ColRight);
            }
            return out;
        case RemoteKey::Enter:
            if (detail) {
                out.add(k);
            } else {
                view_ = UiView::Detail;
            }
            return out;
        case RemoteKey::AutoFocus:
            view_ = UiView::Detail;
            out.add(RemoteKey::JumpFocus);
            out.add(RemoteKey::AutoFocus);
            return out;
        case RemoteKey::Back:
            view_ = UiView::Overview;
            return out;
        default:
            if (isJump(k)) {
                view_ = UiView::Detail;
                out.add(k);
            }
            return out;
    }
}

void UiNav::tick(uint32_t nowMs, bool busy) {
    if (view_ == UiView::Detail && !busy && nowMs - lastKeyMs_ >= DETAIL_IDLE_CLOSE_MS) {
        view_ = UiView::Overview;
    }
    if (busy && view_ == UiView::Detail) lastKeyMs_ = nowMs;  // count idle from the end of work
    if (view_ == UiView::Intro && introAdvanceAt_ && (int32_t)(nowMs - introAdvanceAt_) >= 0) {
        introAdvanceAt_ = 0;
        introCard_ = 1;
    }
}

void UiNav::onAutofocusDone(uint32_t nowMs) {
    if (view_ == UiView::Intro && introCard_ == 0) introAdvanceAt_ = nowMs + INTRO_AF_ADVANCE_MS;
}

bool HintTracker::onUnmappedKey(uint32_t nowMs) {
    const bool lost = hadUnmapped_ && nowMs - lastUnmappedMs_ < 5000;
    hadUnmapped_ = true;
    lastUnmappedMs_ = nowMs;
    if (lost) {
        lostThisSession_ = true;
        hadUnmapped_ = false;
    }
    return lost;
}
