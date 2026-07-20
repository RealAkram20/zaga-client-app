#pragma once

#include <windows.h>

namespace zaga {

// The provider DLL's own module handle, captured in DllMain. Needed to load the
// tile bitmap from the DLL's resources rather than from the host process (LogonUI),
// which does not carry it.
HINSTANCE dllInstance();

}
