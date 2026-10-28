// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#pragma once
#include <cstdint>
#include <string>
namespace astra {
// Self-play data: one line per quiet position "FEN;result" (result from White's view: 1, 0.5, 0).
int runDatagen(int games, int depth, std::uint64_t seed, const std::string& outFile);
// Texel tuning of the classical evaluation. Resumes from `paramFile` if it exists and rewrites it
// after every improvement ("NAME value" per line).
int runTune(const std::string& dataFile, int maxIterations, const std::string& paramFile);
bool loadParamFile(const std::string& path);
}
