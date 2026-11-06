#include "ps2_runtime.h"

#include <new>

// C ABI boundary reserved for the future JNI/GameActivity bridge.
// Creating the object does not initialize a desktop window, start audio,
// read a disc, or execute guest instructions.
extern "C" __attribute__((visibility("default")))
PS2Runtime *gowCreatePs2Runtime() noexcept
{
    try
    {
        return new PS2Runtime();
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" __attribute__((visibility("default")))
void gowDestroyPs2Runtime(PS2Runtime *runtime) noexcept
{
    delete runtime;
}
