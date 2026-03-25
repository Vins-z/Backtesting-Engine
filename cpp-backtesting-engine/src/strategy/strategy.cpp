#include "strategy/strategy.h"
#include <numeric>
#include <algorithm>
#include <cmath>

namespace backtesting {

double Strategy::calculate_sma(const std::vector<double>& values, int period) {
    if (values.empty() || period <= 0 || values.size() < static_cast<size_t>(period)) {
        return 0.0;
    }
    
    double sum = std::accumulate(values.end() - period, values.end(), 0.0);
    return sum / period;
}

double Strategy::calculate_ema(const std::vector<double>& values, int period) {
    if (values.empty() || period <= 0) {
        return 0.0;
    }
    
    double multiplier = 2.0 / (period + 1);
    double ema = values[0];
    
    for (size_t i = 1; i < values.size(); ++i) {
        ema = (values[i] * multiplier) + (ema * (1 - multiplier));
    }
    
    return ema;
}

double Strategy::calculate_std_dev(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    
    double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    double variance = 0.0;
    
    for (double value : values) {
        variance += (value - mean) * (value - mean);
    }
    
    variance /= values.size();
    return std::sqrt(variance);
}

double Strategy::calculate_rsi(const std::vector<double>& prices, int period) {
    if (prices.size() < static_cast<size_t>(period + 1)) {
        return 50.0; // Neutral RSI
    }
    
    std::vector<double> gains, losses;
    
    for (size_t i = 1; i < prices.size(); ++i) {
        double change = prices[i] - prices[i-1];
        gains.push_back(std::max(change, 0.0));
        losses.push_back(std::max(-change, 0.0));
    }
    
    if (gains.size() < static_cast<size_t>(period)) {
        return 50.0;
    }
    
    // Calculate average gains and losses for the period
    double avg_gain = 0.0;
    double avg_loss = 0.0;
    
    for (size_t i = gains.size() - period; i < gains.size(); ++i) {
        avg_gain += gains[i];
        avg_loss += losses[i];
    }
    
    avg_gain /= period;
    avg_loss /= period;
    
    // Calculate RSI
    if (avg_loss == 0.0) {
        return 100.0;
    }
    
    double rs = avg_gain / avg_loss;
    return 100.0 - (100.0 / (1.0 + rs));
}

std::vector<double> Strategy::extract_close_prices(const std::vector<OHLC>& data) {
    std::vector<double> prices;
    prices.reserve(data.size());
    
    for (const auto& ohlc : data) {
        prices.push_back(ohlc.close);
    }
    
    return prices;
}

std::vector<double> Strategy::extract_high_prices(const std::vector<OHLC>& data) {
    std::vector<double> highs;
    highs.reserve(data.size());
    
    for (const auto& ohlc : data) {
        highs.push_back(ohlc.high);
    }
    
    return highs;
}

std::vector<double> Strategy::extract_low_prices(const std::vector<OHLC>& data) {
    std::vector<double> lows;
    lows.reserve(data.size());
    
    for (const auto& ohlc : data) {
        lows.push_back(ohlc.low);
    }
    
    return lows;
}

std::vector<double> Strategy::extract_volumes(const std::vector<OHLC>& data) {
    std::vector<double> volumes;
    volumes.reserve(data.size());
    
    for (const auto& ohlc : data) {
        volumes.push_back(static_cast<double>(ohlc.volume));
    }
    
    return volumes;
}

double Strategy::calculate_macd(const std::vector<double>& prices, int fast_period, int slow_period, int signal_period, double& signal_line, double& histogram) {
    if (prices.size() < static_cast<size_t>(slow_period + signal_period)) {
        signal_line = 0.0;
        histogram = 0.0;
        return 0.0;
    }
    
    // Calculate fast and slow EMAs
    std::vector<double> fast_ema_values;
    std::vector<double> slow_ema_values;
    
    double fast_multiplier = 2.0 / (fast_period + 1);
    double slow_multiplier = 2.0 / (slow_period + 1);
    
    double fast_ema = prices[0];
    double slow_ema = prices[0];
    
    for (size_t i = 1; i < prices.size(); ++i) {
        fast_ema = (prices[i] * fast_multiplier) + (fast_ema * (1 - fast_multiplier));
        slow_ema = (prices[i] * slow_multiplier) + (slow_ema * (1 - slow_multiplier));
        
        if (i >= static_cast<size_t>(slow_period - 1)) {
            fast_ema_values.push_back(fast_ema);
            slow_ema_values.push_back(slow_ema);
        }
    }
    
    if (fast_ema_values.empty()) {
        signal_line = 0.0;
        histogram = 0.0;
        return 0.0;
    }
    
    // Calculate MACD line
    double macd_line = fast_ema_values.back() - slow_ema_values.back();
    
    // Calculate signal line (EMA of MACD line)
    if (fast_ema_values.size() >= static_cast<size_t>(signal_period)) {
        std::vector<double> macd_line_values;
        for (size_t i = 0; i < fast_ema_values.size(); ++i) {
            macd_line_values.push_back(fast_ema_values[i] - slow_ema_values[i]);
        }
        
        double signal_multiplier = 2.0 / (signal_period + 1);
        double signal_ema = macd_line_values[macd_line_values.size() - signal_period];
        for (size_t i = macd_line_values.size() - signal_period + 1; i < macd_line_values.size(); ++i) {
            signal_ema = (macd_line_values[i] * signal_multiplier) + (signal_ema * (1 - signal_multiplier));
        }
        signal_line = signal_ema;
    } else {
        signal_line = macd_line * 0.9; // Simplified fallback
    }
    
    histogram = macd_line - signal_line;
    return macd_line;
}

double Strategy::calculate_bollinger_bands(const std::vector<double>& prices, int period, double std_dev, double& upper_band, double& lower_band) {
    if (prices.size() < static_cast<size_t>(period)) {
        upper_band = 0.0;
        lower_band = 0.0;
        return 0.0;
    }
    
    // Calculate SMA (middle band)
    double sma = calculate_sma(prices, period);
    
    // Calculate standard deviation
    double variance = 0.0;
    for (size_t i = prices.size() - period; i < prices.size(); ++i) {
        double diff = prices[i] - sma;
        variance += diff * diff;
    }
    double std_deviation = std::sqrt(variance / period);
    
    upper_band = sma + (std_dev * std_deviation);
    lower_band = sma - (std_dev * std_deviation);
    
    return sma; // Return middle band
}

double Strategy::calculate_atr(const std::vector<OHLC>& data, int period) {
    if (data.size() < static_cast<size_t>(period + 1)) {
        return 0.0;
    }
    
    std::vector<double> true_ranges;
    for (size_t i = 1; i < data.size(); ++i) {
        double tr1 = data[i].high - data[i].low;
        double tr2 = std::abs(data[i].high - data[i-1].close);
        double tr3 = std::abs(data[i].low - data[i-1].close);
        true_ranges.push_back(std::max({tr1, tr2, tr3}));
    }
    
    if (true_ranges.size() >= static_cast<size_t>(period)) {
        return calculate_sma(true_ranges, period);
    }
    
    return 0.0;
}

double Strategy::calculate_stochastic(const std::vector<OHLC>& data, int k_period, int d_period, double& d_value) {
    if (data.size() < static_cast<size_t>(k_period + d_period)) {
        d_value = 0.0;
        return 0.0;
    }
    
    // Calculate %K
    std::vector<double> k_values;
    for (size_t i = k_period - 1; i < data.size(); ++i) {
        double highest_high = data[i].high;
        double lowest_low = data[i].low;
        
        for (size_t j = i - k_period + 1; j <= i; ++j) {
            highest_high = std::max(highest_high, data[j].high);
            lowest_low = std::min(lowest_low, data[j].low);
        }
        
        if (highest_high != lowest_low) {
            double k = 100.0 * ((data[i].close - lowest_low) / (highest_high - lowest_low));
            k_values.push_back(k);
        } else {
            k_values.push_back(50.0);
        }
    }
    
    if (k_values.empty()) {
        d_value = 0.0;
        return 0.0;
    }
    
    double k_current = k_values.back();
    
    // Calculate %D (SMA of %K)
    if (k_values.size() >= static_cast<size_t>(d_period)) {
        d_value = calculate_sma(k_values, d_period);
    } else {
        d_value = k_current;
    }
    
    return k_current;
}

double Strategy::calculate_williams_r(const std::vector<OHLC>& data, int period) {
    if (data.size() < static_cast<size_t>(period)) {
        return 0.0;
    }
    
    double highest_high = data[data.size() - period].high;
    double lowest_low = data[data.size() - period].low;
    
    for (size_t i = data.size() - period; i < data.size(); ++i) {
        highest_high = std::max(highest_high, data[i].high);
        lowest_low = std::min(lowest_low, data[i].low);
    }
    
    double current_close = data.back().close;
    
    if (highest_high != lowest_low) {
        return -100.0 * ((highest_high - current_close) / (highest_high - lowest_low));
    }
    
    return 0.0;
}

double Strategy::calculate_cci(const std::vector<OHLC>& data, int period) {
    if (data.size() < static_cast<size_t>(period)) {
        return 0.0;
    }
    
    // Calculate Typical Price
    std::vector<double> typical_prices;
    for (const auto& ohlc : data) {
        typical_prices.push_back((ohlc.high + ohlc.low + ohlc.close) / 3.0);
    }
    
    // Calculate SMA of typical prices
    double sma_tp = calculate_sma(typical_prices, period);
    
    // Calculate Mean Deviation
    double mean_dev = 0.0;
    for (size_t i = typical_prices.size() - period; i < typical_prices.size(); ++i) {
        mean_dev += std::abs(typical_prices[i] - sma_tp);
    }
    mean_dev /= period;
    
    if (mean_dev == 0.0) {
        return 0.0;
    }
    
    double current_tp = typical_prices.back();
    return (current_tp - sma_tp) / (0.015 * mean_dev);
}

double Strategy::calculate_obv(const std::vector<OHLC>& data) {
    if (data.size() < 2) {
        return 0.0;
    }
    
    double obv = 0.0;
    for (size_t i = 1; i < data.size(); ++i) {
        if (data[i].close > data[i-1].close) {
            obv += static_cast<double>(data[i].volume);
        } else if (data[i].close < data[i-1].close) {
            obv -= static_cast<double>(data[i].volume);
        }
        // If close is same, OBV stays the same
    }
    
    return obv;
}

double Strategy::calculate_mfi(const std::vector<OHLC>& data, int period) {
    if (data.size() < static_cast<size_t>(period + 1)) {
        return 50.0; // Neutral MFI
    }
    
    std::vector<double> money_flows;
    std::vector<double> positive_flows;
    std::vector<double> negative_flows;
    
    for (size_t i = 1; i < data.size(); ++i) {
        double typical_price = (data[i].high + data[i].low + data[i].close) / 3.0;
        double prev_typical_price = (data[i-1].high + data[i-1].low + data[i-1].close) / 3.0;
        
        double raw_money_flow = typical_price * static_cast<double>(data[i].volume);
        
        if (typical_price > prev_typical_price) {
            positive_flows.push_back(raw_money_flow);
            negative_flows.push_back(0.0);
        } else if (typical_price < prev_typical_price) {
            positive_flows.push_back(0.0);
            negative_flows.push_back(raw_money_flow);
        } else {
            positive_flows.push_back(0.0);
            negative_flows.push_back(0.0);
        }
    }
    
    if (positive_flows.size() < static_cast<size_t>(period)) {
        return 50.0;
    }
    
    double sum_positive = std::accumulate(positive_flows.end() - period, positive_flows.end(), 0.0);
    double sum_negative = std::accumulate(negative_flows.end() - period, negative_flows.end(), 0.0);
    
    if (sum_negative == 0.0) {
        return 100.0;
    }
    
    double money_flow_ratio = sum_positive / sum_negative;
    return 100.0 - (100.0 / (1.0 + money_flow_ratio));
}

double Strategy::calculate_adx(const std::vector<OHLC>& data, int period) {
    // Simplified ADX calculation
    if (data.size() < static_cast<size_t>(period + 1)) {
        return 0.0;
    }
    
    std::vector<double> tr_values;
    std::vector<double> plus_dm_values;
    std::vector<double> minus_dm_values;
    
    for (size_t i = 1; i < data.size(); ++i) {
        double tr = std::max({data[i].high - data[i].low,
                              std::abs(data[i].high - data[i-1].close),
                              std::abs(data[i].low - data[i-1].close)});
        tr_values.push_back(tr);
        
        double plus_dm = (data[i].high > data[i-1].high) ? (data[i].high - data[i-1].high) : 0.0;
        double minus_dm = (data[i].low < data[i-1].low) ? (data[i-1].low - data[i].low) : 0.0;
        
        if (plus_dm > minus_dm) {
            plus_dm_values.push_back(plus_dm);
            minus_dm_values.push_back(0.0);
        } else if (minus_dm > plus_dm) {
            plus_dm_values.push_back(0.0);
            minus_dm_values.push_back(minus_dm);
        } else {
            plus_dm_values.push_back(0.0);
            minus_dm_values.push_back(0.0);
        }
    }
    
    if (tr_values.size() < static_cast<size_t>(period)) {
        return 0.0;
    }
    
    double atr = calculate_sma(tr_values, period);
    double plus_di = calculate_sma(plus_dm_values, period) / atr * 100.0;
    double minus_di = calculate_sma(minus_dm_values, period) / atr * 100.0;
    
    if (atr == 0.0) {
        return 0.0;
    }
    
    double dx = std::abs(plus_di - minus_di) / (plus_di + minus_di) * 100.0;
    return dx; // Simplified - full ADX would smooth DX over period
}

double Strategy::calculate_parabolic_sar(const std::vector<OHLC>& data, double acceleration, double maximum) {
    if (data.size() < 2) {
        return 0.0;
    }
    // Parameters are currently unused by this simplified implementation.
    (void)acceleration;
    (void)maximum;
    
    // Simplified Parabolic SAR
    bool uptrend = data[data.size() - 1].close > data[data.size() - 2].close;
    double sar = uptrend ? data.back().low : data.back().high;
    double ep = uptrend ? data.back().high : data.back().low;
    
    // Find highest high or lowest low in recent period
    int lookback = std::min(10, static_cast<int>(data.size()));
    for (int i = data.size() - lookback; i < static_cast<int>(data.size()); ++i) {
        if (uptrend) {
            ep = std::max(ep, data[i].high);
        } else {
            ep = std::min(ep, data[i].low);
        }
    }
    
    return sar;
}

} // namespace backtesting 