#pragma once
#include <cstdint>
#include <cstdio>
// [VR] InteractionTimingLog=1 (default 0): one summary a second of how long the mod's
// interaction search and the game's own interaction update took, and how many physics
// hits the searches returned. This file only adds numbers and formats a line.
namespace InteractionTiming {
struct Second {
    uint64_t updates{},carrying{},beginTotal{},beginMax{},gameTotal{},gameMax{},endMax{},gapMax{},queries{},hitsTotal{};
    unsigned hitsMax{};
};
inline void AddUpdate(Second& s,uint64_t begin,uint64_t game,uint64_t end,uint64_t gap,bool carrying) {
    ++s.updates;if(carrying)++s.carrying;
    s.beginTotal+=begin;if(begin>s.beginMax)s.beginMax=begin;
    s.gameTotal+=game;if(game>s.gameMax)s.gameMax=game;
    if(end>s.endMax)s.endMax=end;
    if(gap>s.gapMax)s.gapMax=gap;
}
inline void AddQuery(Second& s,int found) {
    ++s.queries;
    if(found<=0)return;
    s.hitsTotal+=unsigned(found);
    if(unsigned(found)>s.hitsMax)s.hitsMax=unsigned(found);
}
inline int Format(const Second& s,double microsPerTick,char* out,size_t size) {
    const double n=s.updates?double(s.updates):1.;
    return snprintf(out,size,"updates=%llu carrying=%llu beginAvgUs=%.0f beginMaxUs=%.0f gameAvgUs=%.0f gameMaxUs=%.0f endMaxUs=%.0f gapMaxMs=%.1f queries=%llu hitsTotal=%llu hitsMax=%u",
        (unsigned long long)s.updates,(unsigned long long)s.carrying,s.beginTotal*microsPerTick/n,s.beginMax*microsPerTick,
        s.gameTotal*microsPerTick/n,s.gameMax*microsPerTick,s.endMax*microsPerTick,s.gapMax*microsPerTick/1000.,
        (unsigned long long)s.queries,(unsigned long long)s.hitsTotal,s.hitsMax);
}
}
