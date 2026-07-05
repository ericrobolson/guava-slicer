/// @file main.cpp
/// @brief Guava Slicer backend entry point.
#include "commands.h"
#include "ipc.h"

int main() {
    commands::register_all();
    ipc::run_loop();
    return 0;
}
