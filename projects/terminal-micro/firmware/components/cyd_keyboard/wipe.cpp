#include <cstddef>

namespace cm {
void wipe(void* pointer, size_t size) {
    auto* byte = static_cast<volatile unsigned char*>(pointer);
    while (size--) *byte++ = 0;
}
}
