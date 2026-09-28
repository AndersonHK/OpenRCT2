// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_TEXT_OPERATION
#define OPENRCT2_WORLD_TEXT_OPERATION
#ifdef __cplusplus
#define TEXT_OPERATION_FN constexpr
#else
#define TEXT_OPERATION_FN
#endif
// Existing hinted-text codes encode coverage semantics, not palette row IDs.
// Return operation kind 1=blend with ink, 2=literal index (including index zero).
TEXT_OPERATION_FN int worldTextOperationKind(int encoded)
{
    if(encoded==0x0102) return 1;
    if(encoded==0x0103 || encoded<256) return 2;
    return (encoded&255)==0?1:2;
}
TEXT_OPERATION_FN int worldTextOperationValue(int encoded)
{
    if(encoded==0x0102 || encoded==0x0103) return 0;
    return encoded<256?encoded:encoded>>8;
}
#undef TEXT_OPERATION_FN
#endif
