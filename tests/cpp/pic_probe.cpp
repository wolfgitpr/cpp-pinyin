// The whole file exists so that a SHARED object links against this library.
//
// WHY (2026-10-20 audit, G10): a static install tree that is not position-independent links fine into an
// executable -- which is exactly what the static leg of CI builds -- and fails only when a consumer pulls
// the archive into their own shared object ("relocation R_X86_64_PC32 against symbol ... can not be used
// when making a shared object"). The defect therefore belongs to the consumer, and they cannot fix it.
// `tests/CMakeLists.txt` builds this file into `PicProbe` (only when BUILD_SHARED_LIBS=OFF) and links the
// library into it: the build itself is the assertion, so there is nothing to check at runtime.
//
// It calls into the library on purpose. A target that linked the archive without referencing a symbol
// would let the linker drop every object and would pass no matter how the archive was compiled.

#include <cpp-pinyin2/ext/c_api.h>

extern "C" const char *pic_probe_version(void) { return cppp2_version(); }
