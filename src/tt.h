// SPDX-License-Identifier: GPL-3.0-or-later
// Astra Chess - Copyright (C) Astra Chess contributors. See LICENSE.
// Astra Chess - lock-free transposition table shared by all search threads.
//
// Each slot is two 64-bit words stored with relaxed atomics: `data` holds the payload and `check`
// holds key ^ data. A torn read (two threads writing the same slot) makes key ^ data mismatch, so
// the entry is simply treated as a miss. This is the standard lockless scheme used by Stockfish.
//
// 0.9: slots are grouped into 2-way buckets with an ageing field (6 bits, bumped once per search via
// newSearch()). On a miss the entry with the lowest "depth minus age penalty" in the bucket is replaced,
// so stale entries from earlier searches no longer block fresh, deeper ones.
#pragma once
#include <algorithm>
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
        std::size_t entries = std::max<std::size_t>(2, mb * 1024 * 1024 / sizeof(Slot));
        std::size_t p = 2;
        while (p * 2 <= entries) p *= 2;
        slots_.reset(new Slot[p]);
        size_ = p;
        mask_ = p - 2;  // bucket base index (even)
        gen_ = 0;
    }
    void clear() {
        for (std::size_t i = 0; i < size_; ++i) {
            slots_[i].check.store(0, std::memory_order_relaxed);
            slots_[i].data.store(0, std::memory_order_relaxed);
        }
        gen_ = 0;
    }
    // Call once at the start of every search (before helper threads are started).
    void newSearch() { gen_ = std::uint8_t((gen_ + 1) & 63); }

    bool probe(Key key, TTEntry& out) const {
        const Slot* b = &slots_[key & mask_];
        for (int i = 0; i < 2; ++i) {
            const std::uint64_t d = b[i].data.load(std::memory_order_relaxed);
            const std::uint64_t c = b[i].check.load(std::memory_order_relaxed);
            if ((c ^ d) != key) continue;
            out = unpack(key, d);
            if (out.bound != BOUND_NONE) return true;
        }
        return false;
    }
    // `always` forces replacement of the matching/victim slot (used by quiescence); otherwise depth-preferred.
    void store(const TTEntry& e, bool always = false) {
        Slot* b = &slots_[e.key & mask_];
        Slot* victim = nullptr;
        int worst = 1 << 30;
        for (int i = 0; i < 2; ++i) {
            const std::uint64_t od = b[i].data.load(std::memory_order_relaxed);
            const std::uint64_t oc = b[i].check.load(std::memory_order_relaxed);
            if ((oc ^ od) == e.key && unpack(e.key, od).bound != BOUND_NONE) {
                const TTEntry old = unpack(e.key, od);
                TTEntry n = e;
                if (!always && ageOf(od) == gen_) {
                    if (!(n.depth >= old.depth - 3 || n.bound == BOUND_EXACT)) return;
                }
                if (n.move == MOVE_NONE) n.move = old.move;
                write(b[i], n);
                return;
            }
            const TTEntry old = unpack(0, od);
            int worth;
            if (old.bound == BOUND_NONE) worth = -100000;  // empty
            else worth = int(old.depth) - 4 * ((gen_ - ageOf(od)) & 63);
            if (worth < worst) { worst = worth; victim = &b[i]; }
        }
        write(*victim, e);
    }
    int hashfull() const {
        const std::size_t n = std::min<std::size_t>(1000, size_);
        if (!n) return 0;
        int used = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint64_t d = slots_[i].data.load(std::memory_order_relaxed);
            used += unpack(0, d).bound != BOUND_NONE && ageOf(d) == gen_;
        }
        return int(used * 1000 / n);
    }

private:
    struct Slot {
        std::atomic<std::uint64_t> check{0};
        std::atomic<std::uint64_t> data{0};
    };
    void write(Slot& s, const TTEntry& n) {
        const std::uint64_t nd = pack(n) | (std::uint64_t(gen_) << 58);
        s.data.store(nd, std::memory_order_relaxed);
        s.check.store(n.key ^ nd, std::memory_order_relaxed);
    }
    static std::uint8_t ageOf(std::uint64_t d) { return std::uint8_t((d >> 58) & 63); }
    // bits 56-57: bound, bits 58-63: age
    static std::uint64_t pack(const TTEntry& e) {
        return std::uint64_t(std::uint16_t(e.score)) | (std::uint64_t(std::uint16_t(e.eval)) << 16) |
               (std::uint64_t(e.move) << 32) | (std::uint64_t(std::uint8_t(e.depth)) << 48) |
               (std::uint64_t(e.bound & 3) << 56);
    }
    static TTEntry unpack(Key key, std::uint64_t d) {
        TTEntry e;
        e.key = key;
        e.score = std::int16_t(d & 0xFFFF);
        e.eval = std::int16_t((d >> 16) & 0xFFFF);
        e.move = Move((d >> 32) & 0xFFFF);
        e.depth = std::int8_t((d >> 48) & 0xFF);
        e.bound = std::uint8_t((d >> 56) & 3);
        return e;
    }
    std::unique_ptr<Slot[]> slots_;
    std::size_t size_ = 0, mask_ = 0;
    std::uint8_t gen_ = 0;
};

}  // namespace astra
