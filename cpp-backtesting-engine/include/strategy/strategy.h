#pragma once

#include "common/types.h"
#ifdef HAS_TALIB
#include <ta_libc.h>
#endif
#include <string>
#include <vector>
#include <memory>

namespace backtesting {

// Forward declarations
class PortfolioManager;

// Base strategy interface
class Strategy {
public:
    virtual ~Strategy() = default;
    
    // Main strategy method - generate trading signals
    virtual Signal generate_signal(
        const std::string& symbol,
        const OHLC& current_data,
        const PortfolioManager& portfolio
    ) = 0;
    
    // Initialize strategy with historical data
    virtual void initialize(const std::vector<OHLC>& historical_data) = 0;
    
    // Update strategy with new market data
    virtual void update(const OHLC& new_data) = 0;
    
    // Get strategy name
    virtual std::string get_name() const = 0;
    
    // Get strategy parameters as key-value pairs
    virtual std::unordered_map<std::string, double> get_parameters() const = 0;
    
    // Reset strategy state
    virtual void reset() = 0;
    
    // Check if strategy is ready to generate signals
    virtual bool is_ready() const = 0;
    
protected:
    // Helper method for calculating Simple Moving Average
    static double calculate_sma(const std::vector<double>& values, int period);
    
    // Helper method for calculating Exponential Moving Average
    static double calculate_ema(const std::vector<double>& values, int period);
    
    // Helper method for calculating standard deviation
    static double calculate_std_dev(const std::vector<double>& values);
    
    // Helper method for calculating RSI
    static double calculate_rsi(const std::vector<double>& prices, int period);
    
    // Helper method to extract close prices from OHLC data
    static std::vector<double> extract_close_prices(const std::vector<OHLC>& data);
    
    // Helper method to extract high prices from OHLC data
    static std::vector<double> extract_high_prices(const std::vector<OHLC>& data);
    
    // Helper method to extract low prices from OHLC data
    static std::vector<double> extract_low_prices(const std::vector<OHLC>& data);
    
    // Helper method to extract volumes from OHLC data
    static std::vector<double> extract_volumes(const std::vector<OHLC>& data);
    
    // Comprehensive indicator calculations
    
    // MACD: Returns MACD line value, signal line and histogram stored in out parameters
    static double calculate_macd(const std::vector<double>& prices, int fast_period, int slow_period, int signal_period, double& signal_line, double& histogram);
    
    // Bollinger Bands: Returns middle band (SMA), upper and lower stored in out parameters
    static double calculate_bollinger_bands(const std::vector<double>& prices, int period, double std_dev, double& upper_band, double& lower_band);
    
    // Average True Range
    static double calculate_atr(const std::vector<OHLC>& data, int period);
    
    // Stochastic Oscillator: Returns %K, %D stored in out parameter
    static double calculate_stochastic(const std::vector<OHLC>& data, int k_period, int d_period, double& d_value);
    
    // Williams %R
    static double calculate_williams_r(const std::vector<OHLC>& data, int period);
    
    // Commodity Channel Index
    static double calculate_cci(const std::vector<OHLC>& data, int period);
    
    // On-Balance Volume
    static double calculate_obv(const std::vector<OHLC>& data);
    
    // Money Flow Index
    static double calculate_mfi(const std::vector<OHLC>& data, int period);
    
    // Average Directional Index (simplified)
    static double calculate_adx(const std::vector<OHLC>& data, int period);
    
    // Parabolic SAR (simplified)
    static double calculate_parabolic_sar(const std::vector<OHLC>& data, double acceleration, double maximum);
};

} // namespace backtesting 