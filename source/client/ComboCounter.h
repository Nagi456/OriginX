#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace MyClientCombo {
// Caller serializes access on the game event thread. No actor pointers or allocations.
class Counter {
    struct AttackEntry { uint64_t id, at; };
    std::array<AttackEntry,8> pending_{};
    size_t size_ = 0;
    uint64_t lastId_ = 0, lastAttack_ = 0, hurtAt_ = 0, motionAt_ = 0;
    uint32_t combo_ = 0;
    bool hurt_ = false, motion_ = false;
    void Remove(size_t index) {
        for (size_t i=index+1; i<size_; ++i) pending_[i-1] = pending_[i];
        --size_;
    }
    void Prune(uint64_t now) {
        for (size_t i=0; i<size_;)
            if (now >= pending_[i].at && now-pending_[i].at >= 1000) Remove(i);
            else ++i;
    }
    void PairDamage() {
        if (hurt_ && motion_ && (hurtAt_ > motionAt_ ? hurtAt_-motionAt_ : motionAt_-hurtAt_) <= 150) Reset();
    }
public:
    void Reset() { size_=0; combo_=0; lastId_=lastAttack_=hurtAt_=motionAt_=0; hurt_=motion_=false; }
    void Attack(uint64_t id, uint64_t now) {
        Prune(now);
        if (size_ == pending_.size()) Remove(0);
        pending_[size_++] = {id,now};
    }
    void Hurt(uint64_t id, uint64_t now, bool local) {
        Prune(now);
        if (local) { hurtAt_=now; hurt_=true; PairDamage(); return; }
        for (size_t i=size_; i>0; --i) {
            const auto entry=pending_[i-1];
            if (entry.id != id) continue;
            Remove(i-1);
            if (combo_ && id==lastId_ && entry.at>=lastAttack_ && entry.at-lastAttack_<=500) {
                if (combo_ != std::numeric_limits<uint32_t>::max()) ++combo_;
            } else combo_=1;
            lastId_=id; lastAttack_=entry.at;
            return;
        }
    }
    void Motion(uint64_t now) { motionAt_=now; motion_=true; PairDamage(); }
    uint32_t Value(uint64_t now) {
        Prune(now);
        if (combo_ && now>=lastAttack_ && now-lastAttack_>500) {
            bool waiting=false;
            for (size_t i=0;i<size_;++i) if (pending_[i].id==lastId_) waiting=true;
            if (!waiting) combo_=0;
        }
        return combo_;
    }
    size_t Pending() const { return size_; }
};
}
