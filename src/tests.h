#pragma once
namespace astra {
int runSelfTest();            // returns 0 on success
int runBench(int depth);      // prints node count + nps (deterministic node signature)
}
