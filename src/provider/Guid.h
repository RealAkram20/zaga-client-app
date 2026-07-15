#pragma once

#include <guiddef.h>

// Zaga Device Lock credential provider and filter share this class id. Generated
// once for this component; it must stay stable so registration keys keep matching.
// Exactly one translation unit includes <initguid.h> before this header to define
// the symbol; every other unit gets an extern declaration.
DEFINE_GUID(CLSID_ZagaLockProvider,
    0xb7a9e3c2, 0x4d1f, 0x4a88, 0x9c, 0x2e, 0x6f, 0x3b, 0x1d, 0x0a, 0x5e, 0x77);
