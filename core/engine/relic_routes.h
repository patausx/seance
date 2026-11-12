#pragma once
#include "unit.h"
#include "relic_types.h"
namespace seance {
// One exclusive modulation slot: legacy ids remain 0..9. Identity is project local.
struct SourceRef {
    enum class Type : uint8_t { None=0, Legacy=1, Relic=2 };
    Type type = Type::None;
    ModSource legacy = ModSource::None;
    relic::Port port = relic::Port::Pull;
    uint32_t instance = 0;
    static SourceRef from_legacy(ModSource s) { SourceRef r; r.type=s==ModSource::None?Type::None:Type::Legacy;r.legacy=s;return r; }
    static SourceRef from_relic(uint32_t id,relic::Port p) { SourceRef r;r.type=Type::Relic;r.instance=id;r.port=p;return r; }
};
struct RelicSourceInfo {
    SourceRef source;
    int8_t chain=-1,slot=-1;
    q15 value=0;
    uint64_t epoch=0;
    uint32_t revision=0;
    bool contributing=false;
};
struct ModRouteInfo {
    SourceRef source;
    int8_t chain=-1,slot=-1,param=-1;
    UnitKind kind=UnitKind::Empty;
    q15 depth=0,value=0;
    bool enabled=true;
    uint64_t epoch=0;
};
} // namespace seance
