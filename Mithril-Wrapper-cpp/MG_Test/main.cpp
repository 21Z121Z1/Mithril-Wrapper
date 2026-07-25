// Mithril-Wrapper - MG_Test/main.cpp
// gtest main replacement. Currently delegates to gtest_main's default entry
// point by initialising Google Test and running all registered tests.
//
// Kept as an explicit .cpp (instead of linking GTest::gtest_main only) so
// future global test fixtures — e.g. a one-shot glslang::InitializeProcess()
// call, a stub VkDevice installed for the whole test process, or a custom
// event listener — can be wired in without touching every test file.
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    // Reserve this spot for process-wide setup hooks (e.g. glslang init,
    // backend stub installation, custom listeners). None are needed yet —
    // gtest_main's default behaviour is sufficient.
    return RUN_ALL_TESTS();
}
