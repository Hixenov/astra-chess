// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
// Serialises all engine output so lines from the search thread and the UCI thread never interleave.
#pragma once
#include <mutex>
namespace astra {
inline std::mutex& outMutex() {
    static std::mutex m;
    return m;
}
}  // namespace astra
