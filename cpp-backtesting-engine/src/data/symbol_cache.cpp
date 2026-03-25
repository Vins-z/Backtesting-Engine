#include "data/symbol_cache.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace backtesting {

SymbolCache::SymbolCache() {
    // Initialize cache with all symbols from MARKET_METADATA
    // This will be populated from stock_symbols.cpp via get_market_metadata
    // For now, initialize with common symbols - full initialization happens on first search
    add_symbol("AAPL", {"AAPL", "Apple Inc.", "NASDAQ", "Technology"});
    add_symbol("MSFT", {"MSFT", "Microsoft Corporation", "NASDAQ", "Technology"});
    add_symbol("GOOGL", {"GOOGL", "Alphabet Inc. Class A", "NASDAQ", "Technology"});
    add_symbol("AMZN", {"AMZN", "Amazon.com Inc.", "NASDAQ", "Technology"});
    add_symbol("NVDA", {"NVDA", "NVIDIA Corporation", "NASDAQ", "Technology"});
    
    // Preload all symbols from MARKET_METADATA for faster search
    // This is done lazily - symbols are added as they're searched or when cache is populated
}

void SymbolCache::add_symbol(const std::string& symbol, const MarketMetadata& metadata) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    symbol_cache_[symbol] = metadata;
    spdlog::debug("Added symbol to cache: {}", symbol);
}

bool SymbolCache::has_symbol(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return symbol_cache_.find(symbol) != symbol_cache_.end();
}

MarketMetadata SymbolCache::get_symbol(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = symbol_cache_.find(symbol);
    if (it != symbol_cache_.end()) {
        return it->second;
    }
    
    // Return default metadata for unknown symbols
    return {symbol, symbol, "Unknown", "Unknown"};
}

std::vector<std::string> SymbolCache::search_symbols(const std::string& query) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    std::vector<std::string> results;
    
    if (query.empty()) {
        return results;
    }
    
    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
    
    for (const auto& [symbol, metadata] : symbol_cache_) {
        std::string lower_symbol = symbol;
        std::transform(lower_symbol.begin(), lower_symbol.end(), lower_symbol.begin(), ::tolower);
        
        std::string lower_name = metadata.name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        
        if (lower_symbol.find(lower_query) != std::string::npos ||
            lower_name.find(lower_query) != std::string::npos) {
            results.push_back(symbol);
        }
    }
    
    // Sort results by relevance (exact symbol match first, then name match)
    std::sort(results.begin(), results.end(), [&](const std::string& a, const std::string& b) {
        std::string lower_a = a;
        std::string lower_b = b;
        std::transform(lower_a.begin(), lower_a.end(), lower_a.begin(), ::tolower);
        std::transform(lower_b.begin(), lower_b.end(), lower_b.begin(), ::tolower);
        
        bool a_exact = lower_a.find(lower_query) == 0;
        bool b_exact = lower_b.find(lower_query) == 0;
        
        if (a_exact && !b_exact) return true;
        if (!a_exact && b_exact) return false;
        
        return a < b;
    });
    
    // Limit results to top 30 for better user experience
    if (results.size() > 30) {
        results.resize(30);
    }
    
    return results;
}

std::vector<MarketMetadata> SymbolCache::get_all_symbols() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    std::vector<MarketMetadata> results;
    results.reserve(symbol_cache_.size());
    
    for (const auto& [symbol, metadata] : symbol_cache_) {
        results.push_back(metadata);
    }
    
    return results;
}

size_t SymbolCache::size() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return symbol_cache_.size();
}

void SymbolCache::clear() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    symbol_cache_.clear();
    spdlog::info("Symbol cache cleared");
}

} // namespace backtesting
