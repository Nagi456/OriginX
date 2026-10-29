// Required by LeviLamina's native mod loader. Use the game's allocator for
// allocations that can cross the SDK boundary (strings, listeners, callbacks).
#define LL_MEMORY_OPERATORS
#include "ll/api/memory/MemoryOperators.h"
