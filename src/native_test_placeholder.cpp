#ifdef UNIT_TEST
// Weak main so PlatformIO can link the native test harness before Unity's test entry point is linked in.
extern "C" int __attribute__((weak)) main() { return 0; }
#endif
