// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#pragma once
#include <cstdint>
#include <string>
namespace astra {
// Self-play data: one line per quiet position "FEN;result;score" (result 1/0.5/0 and search score in cp,
// both from White's view; older files without the score column are still accepted by the tuner/trainer).
int runDatagen(int games, int depth, std::uint64_t seed, const std::string& outFile);
// Texel tuning of the classical evaluation. Resumes from `paramFile` if it exists and rewrites it
// after every improvement ("NAME value" per line).
int runTune(const std::string& dataFile, int maxIterations, const std::string& paramFile);
bool loadParamFile(const std::string& path);
}
