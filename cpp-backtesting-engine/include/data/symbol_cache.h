#pragma once

#include "stock_symbols.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>

namespace backtesting {

// Thread-safe symbol cache for storing validated symbols
class SymbolCache {
private:
    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, MarketMetadata> symbol_cache_;

public:
    SymbolCache();
    
    // Add a symbol to the cache
    void add_symbol(const std::string& symbol, const MarketMetadata& metadata);
    
    // Check if a symbol exists in cache
    bool has_symbol(const std::string& symbol) const;
    
    // Get symbol metadata
    MarketMetadata get_symbol(const std::string& symbol) const;
    
    // Search symbols by query (symbol or name)
    std::vector<std::string> search_symbols(const std::string& query) const;
    
    // Get all cached symbols
    std::vector<MarketMetadata> get_all_symbols() const;
    
    // Get cache size
    size_t size() const;
    
    // Clear cache
    void clear();
};

} // namespace backtesting
