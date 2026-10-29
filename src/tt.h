// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
// Astra Chess - lock-free transposition table shared by all search threads.
//
// Each slot is two 64-bit words stored with relaxed atomics: `data` holds the payload and `check`
// holds key ^ data. A torn read (two threads writing the same slot) makes key ^ data mismatch, so
// the entry is simply treated as a miss. This is the standard lockless scheme used by Stockfish.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

#include "types.h"

namespace astra {

enum TTBound : std::uint8_t { BOUND_NONE, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT };

struct TTEntry {
    Key key = 0;
    std::int16_t score = 0;
    std::int16_t eval = 0;
    Move move = MOVE_NONE;
    std::int8_t depth = 0;
    std::uint8_t bound = BOUND_NONE;
};

class TranspositionTable {
public:
    void resize(std::size_t mb) {
        std::size_t entries = std::max<std::size_t>(1, mb * 1024 * 1024 / sizeof(Slot));
        std::size_t p = 1;
        while (p * 2 <= entries) p *= 2;
        slots_.reset(new Slot[p]);
        size_ = p;
        mask_ = p - 1;
    }
    void clear() {
        for (std::size_t i = 0; i < size_; ++i) {
            slots_[i].check.store(0, std::memory_order_relaxed);
            slots_[i].data.store(0, std::memory_order_relaxed);
        }
    }
    bool probe(Key key, TTEntry& out) const {
        const Slot& s = slots_[key & mask_];
        const std::uint64_t d = s.data.load(std::memory_order_relaxed);
        const std::uint64_t c = s.check.load(std::memory_order_relaxed);
        if ((c ^ d) != key) return false;
        out = unpack(key, d);
        return out.bound != BOUND_NONE;
    }
    // `always` forces replacement (used by quiescence); otherwise a depth-preferred policy applies.
    void store(const TTEntry& e, bool always = false) {
        Slot& s = slots_[e.key & mask_];
        const std::uint64_t od = s.data.load(std::memory_order_relaxed);
        const std::uint64_t oc = s.check.load(std::memory_order_relaxed);
        const bool same = (oc ^ od) == e.key;
        TTEntry n = e;
        if (!always && same) {
            const TTEntry old = unpack(e.key, od);
            if (!(n.depth >= old.depth - 3 || n.bound == BOUND_EXACT)) return;
            if (n.move == MOVE_NONE) n.move = old.move;
        }
        const std::uint64_t nd = pack(n);
        s.data.store(nd, std::memory_order_relaxed);
        s.check.store(e.key ^ nd, std::memory_order_relaxed);
    }
    int hashfull() const {
        const std::size_t n = std::min<std::size_t>(1000, size_);
        if (!n) return 0;
        int used = 0;
        for (std::size_t i = 0; i < n; ++i)
            used += unpack(0, slots_[i].data.load(std::memory_order_relaxed)).bound != BOUND_NONE;
        return int(used * 1000 / n);
    }

private:
    struct Slot {
        std::atomic<std::uint64_t> check{0};
        std::atomic<std::uint64_t> data{0};
    };
    static std::uint64_t pack(const TTEntry& e) {
        return std::uint64_t(std::uint16_t(e.score)) | (std::uint64_t(std::uint16_t(e.eval)) << 16) |
               (std::uint64_t(e.move) << 32) | (std::uint64_t(std::uint8_t(e.depth)) << 48) |
               (std::uint64_t(e.bound) << 56);
    }
    static TTEntry unpack(Key key, std::uint64_t d) {
        TTEntry e;
        e.key = key;
        e.score = std::int16_t(d & 0xFFFF);
        e.eval = std::int16_t((d >> 16) & 0xFFFF);
        e.move = Move((d >> 32) & 0xFFFF);
        e.depth = std::int8_t((d >> 48) & 0xFF);
        e.bound = std::uint8_t((d >> 56) & 0xFF);
        return e;
    }
    std::unique_ptr<Slot[]> slots_;
    std::size_t size_ = 0, mask_ = 0;
};

}  // namespace astra
