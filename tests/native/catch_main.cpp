// The default main for the native tests. Catch2 is built without its own main
// (CATCH_AMALGAMATED_CUSTOM_MAIN), so a test that needs its own command-line
// options, like test_xex_verify.cpp, can define main instead of linking this.

#include "catch_amalgamated.hpp"

int main(int argc, char* argv[]) { return Catch::Session().run(argc, argv); }
