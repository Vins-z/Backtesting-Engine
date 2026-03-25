#pragma once

#include <vector>
#include <string>

namespace backtesting {

// Top 50 S&P 500 stocks by market cap (as of 2024)
const std::vector<std::string> SP500_TOP_50 = {
    "AAPL", "MSFT", "GOOGL", "AMZN", "NVDA", "META", "TSLA", "BRK-B", "AVGO", "JPM",
    "UNH", "XOM", "JNJ", "V", "PG", "MA", "HD", "CVX", "ABBV", "PFE",
    "BAC", "KO", "PEP", "TMO", "COST", "WMT", "MRK", "ABT", "ACN", "NFLX",
    "DHR", "VZ", "ADBE", "CRM", "TXN", "NKE", "QCOM", "RTX", "NEE", "PM",
    "T", "HON", "SPGI", "LOW", "UNP", "IBM", "INTU", "GS", "CAT", "AMGN"
};

// Top 50 Nifty 50 stocks (Indian market - NSE)
const std::vector<std::string> NIFTY_50 = {
    "RELIANCE.NS", "TCS.NS", "HDFCBANK.NS", "ICICIBANK.NS", "INFY.NS", "HINDUNILVR.NS", "ITC.NS",
    "SBIN.NS", "BHARTIARTL.NS", "KOTAKBANK.NS", "LT.NS", "ASIANPAINT.NS", "AXISBANK.NS",
    "BAJFINANCE.NS", "MARUTI.NS", "SUNPHARMA.NS", "HCLTECH.NS", "WIPRO.NS", "TECHM.NS",
    "ULTRACEMCO.NS", "POWERGRID.NS", "ONGC.NS", "TITAN.NS", "ADANIENT.NS", "NTPC.NS",
    "NESTLEIND.NS", "TATAMOTORS.NS", "INDUSINDBK.NS", "COALINDIA.NS", "TATASTEEL.NS",
    "JSWSTEEL.NS", "GRASIM.NS", "DRREDDY.NS", "CIPLA.NS", "APOLLOHOSP.NS", "DIVISLAB.NS",
    "EICHERMOT.NS", "BAJAJFINSV.NS", "BRITANNIA.NS", "HEROMOTOCO.NS", "UPL.NS",
    "SHREECEM.NS", "BPCL.NS", "IOC.NS", "HINDALCO.NS", "TATACONSUM.NS", "ADANIPORTS.NS"
};

// Top 50 BSE Sensex stocks (Indian market - BSE)
const std::vector<std::string> BSE_SENSEX_50 = {
    "RELIANCE.BO", "TCS.BO", "HDFCBANK.BO", "ICICIBANK.BO", "INFY.BO", "HINDUNILVR.BO", "ITC.BO",
    "SBIN.BO", "BHARTIARTL.BO", "KOTAKBANK.BO", "LT.BO", "ASIANPAINT.BO", "AXISBANK.BO",
    "BAJFINANCE.BO", "MARUTI.BO", "SUNPHARMA.BO", "HCLTECH.BO", "WIPRO.BO", "TECHM.BO",
    "ULTRACEMCO.BO", "POWERGRID.BO", "ONGC.BO", "TITAN.BO", "ADANIENT.BO", "NTPC.BO",
    "NESTLEIND.BO", "TATAMOTORS.BO", "INDUSINDBK.BO", "COALINDIA.BO", "TATASTEEL.BO",
    "JSWSTEEL.BO", "GRASIM.BO", "DRREDDY.BO", "CIPLA.BO", "APOLLOHOSP.BO", "DIVISLAB.BO",
    "EICHERMOT.BO", "BAJAJFINSV.BO", "BRITANNIA.BO", "HEROMOTOCO.BO", "UPL.BO",
    "SHREECEM.BO", "BPCL.BO", "IOC.BO", "HINDALCO.BO", "TATACONSUM.BO", "ADANIPORTS.BO",
    "M&M.BO", "TATAPOWER.BO", "VEDL.BO", "JINDALSTEL.BO"
};

// Market metadata structure
struct MarketMetadata {
    std::string symbol;
    std::string name;
    std::string exchange;
    std::string sector;
};

// Get market metadata for a symbol
MarketMetadata get_market_metadata(const std::string& symbol);

// Get all symbols for a market
std::vector<std::string> get_market_symbols(const std::string& market);

// Get all known symbols (for cache initialization)
std::vector<std::string> get_all_known_symbols();

} // namespace backtesting
