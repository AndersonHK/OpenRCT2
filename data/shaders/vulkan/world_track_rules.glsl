// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_TRACK_RULES
#define OPENRCT2_WORLD_TRACK_RULES
// Caller defines WORLD_TRACK_WORD(index) for binding14, addressing the start of
// the immutable native-track table. Ride facts may follow it in the same buffer.
// Authored image clock, evaluated from the immutable snapshot tick on GPU.
uint worldTrackImageAtTick(uint image,uint tick)
{
    if((image&0xfe000000u)!=0x80000000u) return image;
    uint shift=(image>>19u)&7u,frameBits=(image>>22u)&7u;
    return (image&0x7ffffu)+((tick>>shift)&((1u<<frameBits)-1u));
}

struct WorldTrackPart
{
    uint image;
    ivec3 offset;
    ivec3 bounds;
    ivec3 size;
    uint colourRole; // Low3 bits: palette role; bit3: source-authored foreground contact.
    int parent;
};

// state: chain/inverted/brakeClosed/cable/CSG/greenLight/platforms in bits0..6. Direction includes
// camera rotation. Unsupported styles/types/sequences produce no components.
bool worldTrackLookup(uint style, uint type, uint sequence, uint direction, uint state, out uvec2 span)
{
    span=uvec2(0u);
    if (WORLD_TRACK_WORD(0u)!=0x5452434bu || WORLD_TRACK_WORD(1u)!=1u) return false;
    uint styles=WORLD_TRACK_WORD(2u), types=WORLD_TRACK_WORD(3u);
    if(style>=styles || type>=types || direction>=4u || state>=128u) return false;
    uint descriptor=WORLD_TRACK_WORD(4u)+(style*types+type)*3u;
    uint first=WORLD_TRACK_WORD(descriptor);
    uint sequences=WORLD_TRACK_WORD(descriptor+1u), mask=WORLD_TRACK_WORD(descriptor+2u);
    if(sequence>=sequences || mask>=128u) return false;
    uint variant=0u, shift=0u;
    for(uint bit=0u;bit<7u;bit++)
        if((mask&(1u<<bit))!=0u) { variant|=((state>>bit)&1u)<<shift; shift++; }
    uint row=WORLD_TRACK_WORD(5u)+(first+(variant*sequences+sequence)*4u+direction)*2u;
    span=uvec2(WORLD_TRACK_WORD(row),WORLD_TRACK_WORD(row+1u));
    return true;
}

WorldTrackPart worldTrackPart(uint index)
{
    uint offset=WORLD_TRACK_WORD(6u)+index*12u;
    WorldTrackPart part;
    part.image=WORLD_TRACK_WORD(offset);
    part.offset=ivec3(int(WORLD_TRACK_WORD(offset+1u)),int(WORLD_TRACK_WORD(offset+2u)),int(WORLD_TRACK_WORD(offset+3u)));
    part.bounds=ivec3(int(WORLD_TRACK_WORD(offset+4u)),int(WORLD_TRACK_WORD(offset+5u)),int(WORLD_TRACK_WORD(offset+6u)));
    part.size=ivec3(int(WORLD_TRACK_WORD(offset+7u)),int(WORLD_TRACK_WORD(offset+8u)),int(WORLD_TRACK_WORD(offset+9u)));
    part.colourRole=WORLD_TRACK_WORD(offset+10u);
    part.parent=int(WORLD_TRACK_WORD(offset+11u));
    return part;
}
#endif
