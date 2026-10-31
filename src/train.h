// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
#pragma once
#include <string>
namespace astra {
// Trains a PS768 -> H -> 1 network (see nnue.h) on "FEN;result[;score]" data and exports an ASTN file.
// Pure C++, no external dependencies. `lambda` blends search score (1.0) and game result (0.0) as the target.
int runTrain(const std::string& dataFile, const std::string& outFile, int hidden, int epochs, double lambda,
             int threads, double lr);
}
