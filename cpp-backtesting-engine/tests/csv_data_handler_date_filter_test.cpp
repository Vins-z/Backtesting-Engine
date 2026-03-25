#include "data/data_handler.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "bt_csv_filter_test";
    fs::create_directories(tmp_dir);
    const fs::path csv_path = tmp_dir / "TEST.csv";

    std::ofstream out(csv_path.string());
    out << "Date,Open,High,Low,Close,Adj Close,Volume\n";
    out << "2024-01-01,100,101,99,100,100,1000\n";
    out << "2024-01-02,101,102,100,101,101,1000\n";
    out << "2024-01-03,102,103,101,102,102,1000\n";
    out.close();

    backtesting::CSVDataHandler handler(tmp_dir.string());
    if (!handler.load_symbol_data("TEST", "2024-01-02", "2024-01-02")) {
        std::cerr << "Failed to load CSV data\n";
        return 1;
    }

    const auto data = handler.get_historical_data("TEST");
    fs::remove_all(tmp_dir);

    if (data.size() != 1) {
        std::cerr << "Expected exactly one bar after date filtering\n";
        return 1;
    }
    return 0;
}
