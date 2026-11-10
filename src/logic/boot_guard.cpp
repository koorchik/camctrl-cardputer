#include "logic/boot_guard.h"

bool bootGuardOnBoot(BootGuardState& st, ResetKind reset) {
    if (st.magic != BOOT_GUARD_MAGIC || reset == ResetKind::PowerOn) {
        st.magic = BOOT_GUARD_MAGIC;
        st.crashCount = 0;
        st.safeRequested = 0;
        return false;
    }
    if (reset == ResetKind::Crash) ++st.crashCount;
    if (st.safeRequested) {
        st.safeRequested = 0;
        return true;
    }
    return st.crashCount >= BOOT_GUARD_CRASH_LIMIT;
}

void bootGuardMarkStable(BootGuardState& st) {
    st.magic = BOOT_GUARD_MAGIC;
    st.crashCount = 0;
}

void bootGuardRequestSafe(BootGuardState& st) {
    st.magic = BOOT_GUARD_MAGIC;
    st.safeRequested = 1;
}
