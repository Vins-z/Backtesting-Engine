#include "data/data_handler.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    fs::path temp_dir = fs::temp_directory_path() / "bt_multi_symbol_test";
    fs::create_directories(temp_dir);

    // Create two test CSV files
    fs::path aapl_csv = temp_dir / "AAPL.csv";
    {
        std::ofstream f(aapl_csv);
        f << "Date,Open,High,Low,Close,Volume\n";
        f << "2023-01-03,130.0,131.0,129.0,130.5,1000000\n";
        f << "2023-01-04,130.5,132.0,130.0,131.5,1200000\n";
    }

    fs::path msft_csv = temp_dir / "MSFT.csv";
    {
        std::ofstream f(msft_csv);
        f << "Date,Open,High,Low,Close,Volume\n";
        f << "2023-01-03,240.0,242.0,239.0,241.0,800000\n";
        f << "2023-01-04,241.0,243.5,240.5,242.5,950000\n";
    }

    // 1. Test CSVDataHandler directory loading
    backtesting::CSVDataHandler handler(temp_dir.string());
    bool ok1 = handler.load_symbol_data("AAPL", "2023-01-01", "2023-01-10");
    bool ok2 = handler.load_symbol_data("MSFT", "2023-01-01", "2023-01-10");

    assert(ok1 && "Failed to load AAPL");
    assert(ok2 && "Failed to load MSFT");

    auto symbols = handler.get_symbols();
    assert(symbols.size() == 2 && "Expected exactly 2 symbols");

    auto aapl_bars = handler.get_historical_data("AAPL");
    assert(aapl_bars.size() == 2 && "Expected 2 AAPL bars");
    for (const auto& bar : aapl_bars) {
        assert(bar.symbol == "AAPL" && "Bar symbol must be AAPL");
    }

    auto msft_bars = handler.get_historical_data("MSFT");
    assert(msft_bars.size() == 2 && "Expected 2 MSFT bars");
    for (const auto& bar : msft_bars) {
        assert(bar.symbol == "MSFT" && "Bar symbol must be MSFT");
    }

    // 2. Test stream interleaved retrieval
    int total_bars = 0;
    while (handler.has_next()) {
        auto bar = handler.get_next();
        assert(!bar.symbol.empty() && "Streamed bar must have a symbol assigned");
        assert((bar.symbol == "AAPL" || bar.symbol == "MSFT") && "Unexpected symbol in bar stream");
        total_bars++;
    }
    assert(total_bars == 4 && "Expected 4 total bars from combined stream");

    // Clean up
    fs::remove_all(temp_dir);
    std::cout << "test_multi_symbol_csv passed successfully.\n";
    return 0;
}
