#include "elmd_test.hpp"

int main(int argc, const char* argv[]) {
    return boost::ut::cfg<>.run({.argc = argc, .argv = argv});
}
