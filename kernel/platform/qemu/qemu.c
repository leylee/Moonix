#include "mapping.h"

Segment PlatformMappingSegments[] = {
    /* SP0 */
    0x02000000, 0x02050FFF, READABLE | WRITABLE,
    0x02500000, 0x02502FFF, READABLE | WRITABLE,
};