// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Immutable railway grammar. No per-instance CPU recipe or paint invocation.
#ifndef OPENRCT2_WORLD_RAILWAY_RULES
#define OPENRCT2_WORLD_RAILWAY_RULES
uint worldRailwayBase() { return (uTracks.words[1u]&255u)>=2u?uTracks.words[14u]:0u; }
uint worldRailwayWord(uint word) { return uTracks.words[worldRailwayBase()+word]; }
bool worldRailwayStyle(uint style)
{
    return worldRailwayBase()!=0u && worldRailwayWord(0u)==0x5241494cu && style==worldRailwayWord(7u);
}
bool worldRailwayLookup(uint type,uint sequence,uint direction,out uint row)
{
    row=0u;
    if(type>=worldRailwayWord(2u) || direction>=4u) return false;
    uint descriptor=worldRailwayWord(3u)+type*2u;
    if(sequence>=worldRailwayWord(descriptor+1u)) return false;
    row=worldRailwayWord(4u)+(worldRailwayWord(descriptor)+sequence*4u+direction)*5u;
    return true;
}
WorldTrackPart worldRailwayPart(uint index)
{
    uint at=worldRailwayWord(5u)+index*12u;
    WorldTrackPart p;
    p.image=worldRailwayWord(at);
    p.offset=ivec3(worldRailwayWord(at+1u),worldRailwayWord(at+2u),worldRailwayWord(at+3u));
    p.bounds=ivec3(worldRailwayWord(at+4u),worldRailwayWord(at+5u),worldRailwayWord(at+6u));
    p.size=ivec3(worldRailwayWord(at+7u),worldRailwayWord(at+8u),worldRailwayWord(at+9u));
    p.colourRole=worldRailwayWord(at+10u);p.parent=int(worldRailwayWord(at+11u));
    return p;
}
#endif
