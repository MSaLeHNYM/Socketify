// Test entry point: ignore SIGPIPE so raw-socket/TLS teardown races in the
// test clients cannot kill the process.

#include <gtest/gtest.h>

#include <csignal>

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
