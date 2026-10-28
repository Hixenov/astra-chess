// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#pragma once
namespace astra {
int runSelfTest();            // returns 0 on success
int runBench(int depth);      // prints node count + nps (deterministic node signature)
}
