#pragma once
#include "Core.hpp"
#include <span>

namespace fwl {
constexpr int checkpointSafetyTicks = 12;
constexpr int checkpointValidationTicks = 60;
constexpr size_t checkpointCap = 128;

// A snapshot at tick N represents the boundary BEFORE input/physics step N.
struct AnalysisTimeline {
    int tick = 0;
    size_t inputCursor = 0, poseCursor = 0;
    std::array<bool, 2> jumpHeld{};
    uint64_t randomSeed = 0, replaySeed = 0;
    bool verified = false, usable = true;
};
inline int checkpointSpacing(int endTick, int requested = 180) {
    return std::max(std::clamp(requested, 180, 2400),
        static_cast<int>((static_cast<int64_t>(std::max(0, endTick)) + checkpointCap - 1) / checkpointCap));
}
template<class Points>
std::optional<size_t> selectAnalysisCheckpoint(Points const& points, int targetTick,
        int window = maxWindow, int safety = checkpointSafetyTicks, bool includeUnusable = false) {
    int64_t latest = static_cast<int64_t>(targetTick) - std::max(0, window) - std::max(0, safety);
    std::optional<size_t> found;
    for (size_t i = 0; i < points.size(); ++i) {
        auto const& point = points[i];
        if (point.tick >= 0 && point.tick <= latest && (point.usable || includeUnusable) &&
            (!found || point.tick > points[*found].tick)) found = i;
    }
    return found;
}
// Build analyzer-owned checkpoints near the latest safe restore boundary for
// actual inputs, rather than blindly spacing them across empty parts of a level.
// If there are more desired points than the snapshot cap, sample by timeline
// position; dense click clusters can share one nearby restore point.
inline std::vector<int> inputAwareCheckpointPlan(Run const& run, size_t cap = checkpointCap,
        int window = maxWindow, int safety = checkpointSafetyTicks) {
    std::vector<int> desired;
    desired.reserve(run.inputs.size());
    for(auto const& in:run.inputs) {
        int tick=in.tick-std::max(0,window)-std::max(0,safety);
        if(tick>0 && tick<run.endTick)desired.push_back(tick);
    }
    std::sort(desired.begin(),desired.end());
    desired.erase(std::unique(desired.begin(),desired.end()),desired.end());
    if(cap==0 || desired.empty())return {};
    if(desired.size()<=cap)return desired;
    if(cap==1)return {desired.front()};

    std::vector<int> plan;plan.reserve(cap);
    const int64_t first=desired.front(),last=desired.back(),range=last-first;
    for(size_t slot=0;slot<cap;++slot) {
        int64_t target=first+(range*static_cast<int64_t>(slot))/static_cast<int64_t>(cap-1);
        auto it=std::lower_bound(desired.begin(),desired.end(),static_cast<int>(target));
        int chosen;
        if(it==desired.begin())chosen=*it;
        else if(it==desired.end())chosen=desired.back();
        else {
            int hi=*it,lo=*(it-1);
            chosen=(target-lo<=hi-target)?lo:hi;
        }
        if(plan.empty() || plan.back()!=chosen)plan.push_back(chosen);
    }
    // Timeline sampling can map multiple empty-time slots to one desired point.
    // That's intentional: no input needs a checkpoint in those empty regions.
    return plan;
}

inline AnalysisTimeline timelineAt(Run const& run, int tick) {
    AnalysisTimeline point; point.tick = tick;
    while (point.inputCursor < run.inputs.size() && run.inputs[point.inputCursor].tick < tick) {
        auto const& in = run.inputs[point.inputCursor++];
        point.jumpHeld.at(in.player) = in.down;
    }
    point.poseCursor = std::lower_bound(run.poses.begin(), run.poses.end(), tick,
        [](Pose const& p, int t) { return p.tick < t; }) - run.poses.begin();
    return point;
}

// Stable merge of one shifted event with the immutable reference. No trial-sized
// allocation/sort. Ties use original index, matching shifted()'s stable_sort.
struct CandidatePlayback {
    size_t cursor = 0;
    std::optional<size_t> target;
    int offset = 0;
    bool deliveredTarget = false;
    void reset(size_t from, std::optional<size_t> index = {}, int shift = 0) {
        cursor = from; target = index; offset = shift; deliveredTarget = false;
    }
    std::optional<size_t> next(std::span<Input const> inputs) {
        if (target && cursor == *target) ++cursor;
        if (target && !deliveredTarget) {
            int shiftedTick = inputs[*target].tick + offset;
            if (cursor >= inputs.size() || shiftedTick < inputs[cursor].tick ||
                (shiftedTick == inputs[cursor].tick && *target < cursor)) return target;
        }
        if (cursor < inputs.size()) return cursor;
        return {};
    }
    int tick(std::span<Input const> inputs, size_t index) const {
        return inputs[index].tick + (target && index == *target ? offset : 0);
    }
    void consume(size_t index) {
        if (target && index == *target) deliveredTarget = true;
        else ++cursor;
    }
};
}
