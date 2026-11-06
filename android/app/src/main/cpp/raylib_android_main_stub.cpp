// Raylib 5.5's Android platform object contains a NativeActivity entry point
// that forwards to a conventional main(). FORGE uses AndroidX GameActivity
// instead; the Raylib entry point is renamed in the private runtime patch and
// is never called. This inert symbol only satisfies its dormant reference.
extern "C" int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}
