#include "cli/cli.h"
#include <iostream>
#include <exception>

int main(int argc, char* argv[]) {
    try {
        return backtesting::cli::CLIApplication::run_with_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        return 1;
    }
} 