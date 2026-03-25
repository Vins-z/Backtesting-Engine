#include "data/technical_indicators.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <spdlog/spdlog.h>

namespace backtesting {

// Helper function to convert IndicatorResult to JSON
nlohmann::json indicator_result_to_json(const IndicatorResult& result) {
    nlohmann::json json_result;
    json_result["current_value"] = result.current_value;
    json_result["history"] = result.history;
    json_result["signal"] = result.signal;
    json_result["interpretation"] = result.interpretation;
    return json_result;
}

nlohmann::json TechnicalIndicatorsCalculator::calculate_all_indicators(
    const std::vector<OHLC>& data,
    const std::string& symbol
) {
    nlohmann::json result;
    result["symbol"] = symbol;
    result["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    
    if (data.empty()) {
        result["error"] = "No data available";
        return result;
    }
    
    try {
        // Trend Indicators
        nlohmann::json trend_indicators;
        trend_indicators["sma_20"] = indicator_result_to_json(calculate_sma(data, 20));
        trend_indicators["sma_50"] = indicator_result_to_json(calculate_sma(data, 50));
        trend_indicators["sma_200"] = indicator_result_to_json(calculate_sma(data, 200));
        trend_indicators["ema_12"] = indicator_result_to_json(calculate_ema(data, 12));
        trend_indicators["ema_26"] = indicator_result_to_json(calculate_ema(data, 26));
        result["trend_indicators"] = trend_indicators;
        
        // Momentum Indicators
        nlohmann::json momentum_indicators;
        momentum_indicators["rsi_14"] = indicator_result_to_json(calculate_rsi(data, 14));
        momentum_indicators["macd"] = indicator_result_to_json(calculate_macd(data));
        result["momentum_indicators"] = momentum_indicators;
        
        // Volatility Indicators
        nlohmann::json volatility_indicators;
        volatility_indicators["bollinger_bands"] = indicator_result_to_json(calculate_bollinger_bands(data, 20, 2.0));
        volatility_indicators["atr_14"] = indicator_result_to_json(calculate_atr(data, 14));
        result["volatility_indicators"] = volatility_indicators;
        
        // Volume Indicators
        nlohmann::json volume_indicators;
        volume_indicators["volume_analysis"] = indicator_result_to_json(calculate_volume_indicators(data));
        result["volume_indicators"] = volume_indicators;
        
        // Current price info
        if (!data.empty()) {
            const auto& latest = data.back();
            result["current_price"] = {
                {"open", latest.open},
                {"high", latest.high},
                {"low", latest.low},
                {"close", latest.close},
                {"volume", latest.volume}
            };
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Error calculating technical indicators for {}: {}", symbol, e.what());
        result["error"] = "Failed to calculate indicators: " + std::string(e.what());
    }
    
    return result;
}

IndicatorResult TechnicalIndicatorsCalculator::calculate_sma(const std::vector<OHLC>& data, int period) {
    IndicatorResult result;
    
    if (data.size() < static_cast<size_t>(period)) {
        result.current_value = 0.0;
        result.signal = "insufficient_data";
        result.interpretation = "Insufficient data for SMA(" + std::to_string(period) + ")";
        return result;
    }
    
    auto closes = extract_closes(data);
    
    // Calculate current SMA
    double sum = std::accumulate(closes.end() - period, closes.end(), 0.0);
    result.current_value = sum / period;
    
    // Calculate historical SMA values (last 30 days)
    result.history = get_last_n_values(closes, 30);
    
    // Simple signal interpretation
    if (data.size() >= 2) {
        double current_price = data.back().close;
        if (current_price > result.current_value) {
            result.signal = "bullish";
            result.interpretation = "Price above SMA(" + std::to_string(period) + ") - Bullish trend";
        } else {
            result.signal = "bearish";
            result.interpretation = "Price below SMA(" + std::to_string(period) + ") - Bearish trend";
        }
    } else {
        result.signal = "neutral";
        result.interpretation = "SMA(" + std::to_string(period) + ") = " + std::to_string(result.current_value);
    }
    
    return result;
}

IndicatorResult TechnicalIndicatorsCalculator::calculate_ema(const std::vector<OHLC>& data, int period) {
    IndicatorResult result;
    
    if (data.empty()) {
        result.current_value = 0.0;
        result.signal = "insufficient_data";
        result.interpretation = "No data available";
        return result;
    }
    
    auto closes = extract_closes(data);
    
    // Calculate EMA
    double multiplier = 2.0 / (period + 1);
    double ema = closes[0];
    
    for (size_t i = 1; i < closes.size(); ++i) {
        ema = (closes[i] * multiplier) + (ema * (1 - multiplier));
    }
    
    result.current_value = ema;
    result.history = get_last_n_values(closes, 30);
    
    if (data.size() >= 2) {
        double current_price = data.back().close;
        if (current_price > result.current_value) {
            result.signal = "bullish";
            result.interpretation = "Price above EMA(" + std::to_string(period) + ") - Bullish momentum";
        } else {
            result.signal = "bearish";
            result.interpretation = "Price below EMA(" + std::to_string(period) + ") - Bearish momentum";
        }
    } else {
        result.signal = "neutral";
        result.interpretation = "EMA(" + std::to_string(period) + ") = " + std::to_string(result.current_value);
    }
    
    return result;
}

IndicatorResult TechnicalIndicatorsCalculator::calculate_rsi(const std::vector<OHLC>& data, int period) {
    IndicatorResult result;
    
    if (data.size() < static_cast<size_t>(period + 1)) {
        result.current_value = 0.0;
        result.signal = "insufficient_data";
        result.interpretation = "Insufficient data for RSI(" + std::to_string(period) + ")";
        return result;
    }
    
    auto closes = extract_closes(data);
    
    // Calculate price changes
    std::vector<double> gains, losses;
    for (size_t i = 1; i < closes.size(); ++i) {
        double change = closes[i] - closes[i-1];
        if (change > 0) {
            gains.push_back(change);
            losses.push_back(0);
        } else {
            gains.push_back(0);
            losses.push_back(-change);
        }
    }
    
    // Calculate average gains and losses
    double avg_gain = 0, avg_loss = 0;
    if (gains.size() >= static_cast<size_t>(period)) {
        avg_gain = std::accumulate(gains.end() - period, gains.end(), 0.0) / period;
        avg_loss = std::accumulate(losses.end() - period, losses.end(), 0.0) / period;
    }
    
    // Calculate RSI
    if (avg_loss == 0) {
        result.current_value = 100.0;
    } else {
        double rs = avg_gain / avg_loss;
        result.current_value = 100 - (100 / (1 + rs));
    }
    
    result.history = get_last_n_values(closes, 30);
    result.signal = interpret_rsi_signal(result.current_value);
    result.interpretation = "RSI(" + std::to_string(period) + ") = " + std::to_string(result.current_value) + " - " + result.signal;
    
    return result;
}

IndicatorResult TechnicalIndicatorsCalculator::calculate_macd(const std::vector<OHLC>& data) {
    IndicatorResult result;
    
    if (data.size() < 26) {
        result.current_value = 0.0;
        result.signal = "insufficient_data";
        result.interpretation = "Insufficient data for MACD";
        return result;
    }
    
    auto closes = extract_closes(data);
    
    // Calculate EMA 12 and EMA 26
    double ema12 = calculate_ema(data, 12).current_value;
    double ema26 = calculate_ema(data, 26).current_value;
    
    // MACD line
    double macd_line = ema12 - ema26;
    
    // Signal line (EMA 9 of MACD line) - simplified
    double signal_line = macd_line * 0.9; // Simplified signal line
    double histogram = macd_line - signal_line;
    
    result.current_value = macd_line;
    result.history = get_last_n_values(closes, 30);
    result.signal = interpret_macd_signal(macd_line, signal_line, histogram);
    result.interpretation = "MACD = " + std::to_string(macd_line) + ", Signal = " + std::to_string(signal_line) + " - " + result.signal;
    
    return result;
}

IndicatorResult TechnicalIndicatorsCalculator::calculate_bollinger_bands(const std::vector<OHLC>& data, int period, double std_dev) {
    IndicatorResult result;
    
    if (data.size() < static_cast<size_t>(period)) {
        result.current_value = 0.0;
        result.signal = "insufficient_data";
        result.interpretation = "Insufficient data for Bollinger Bands";
        return result;
    }
    
    auto closes = extract_closes(data);
    
    // Calculate SMA
    double sum = std::accumulate(closes.end() - period, closes.end(), 0.0);
    double sma = sum / period;
    
    // Calculate standard deviation
    double variance = 0.0;
    for (int i = closes.size() - period; i < static_cast<int>(closes.size()); ++i) {
        double diff = closes[i] - sma;
        variance += diff * diff;
    }
    double std_deviation = std::sqrt(variance / period);
    
    // Bollinger Bands
    double upper_band = sma + (std_dev * std_deviation);
    double lower_band = sma - (std_dev * std_deviation);
    
    result.current_value = sma; // Middle band
    result.history = get_last_n_values(closes, 30);
    
    double current_price = data.back().close;
    result.signal = interpret_bollinger_signal(current_price, upper_band, lower_band, sma);
    result.interpretation = "BB Upper: " + std::to_string(upper_band) + ", Middle: " + std::to_string(sma) + ", Lower: " + std::to_string(lower_band) + " - " + result.signal;
    
    return result;
}

IndicatorResult TechnicalIndicatorsCalculator::calculate_atr(const std::vector<OHLC>& data, int period) {
    IndicatorResult result;
    
    if (data.size() < static_cast<size_t>(period + 1)) {
        result.current_value = 0.0;
        result.signal = "insufficient_data";
        result.interpretation = "Insufficient data for ATR";
        return result;
    }
    
    auto highs = extract_highs(data);
    auto lows = extract_lows(data);
    auto closes = extract_closes(data);
    
    // Calculate True Range values
    std::vector<double> true_ranges;
    for (size_t i = 1; i < data.size(); ++i) {
        double tr1 = highs[i] - lows[i];
        double tr2 = std::abs(highs[i] - closes[i-1]);
        double tr3 = std::abs(lows[i] - closes[i-1]);
        true_ranges.push_back(std::max({tr1, tr2, tr3}));
    }
    
    // Calculate ATR as SMA of True Ranges
    if (true_ranges.size() >= static_cast<size_t>(period)) {
        double sum = std::accumulate(true_ranges.end() - period, true_ranges.end(), 0.0);
        result.current_value = sum / period;
    } else {
        result.current_value = 0.0;
    }
    
    result.history = get_last_n_values(closes, 30);
    result.signal = "neutral";
    result.interpretation = "ATR(" + std::to_string(period) + ") = " + std::to_string(result.current_value) + " - Volatility measure";
    
    return result;
}

IndicatorResult TechnicalIndicatorsCalculator::calculate_volume_indicators(const std::vector<OHLC>& data) {
    IndicatorResult result;
    
    if (data.empty()) {
        result.current_value = 0.0;
        result.signal = "insufficient_data";
        result.interpretation = "No volume data available";
        return result;
    }
    
    auto volumes = extract_volumes(data);
    
    // Calculate average volume
    double avg_volume = std::accumulate(volumes.begin(), volumes.end(), 0.0) / volumes.size();
    result.current_value = avg_volume;
    result.history = get_last_n_values(volumes, 30);
    
    double current_volume = data.back().volume;
    if (current_volume > avg_volume * 1.5) {
        result.signal = "high_volume";
        result.interpretation = "High volume detected - " + std::to_string(current_volume) + " vs avg " + std::to_string(avg_volume);
    } else if (current_volume < avg_volume * 0.5) {
        result.signal = "low_volume";
        result.interpretation = "Low volume detected - " + std::to_string(current_volume) + " vs avg " + std::to_string(avg_volume);
    } else {
        result.signal = "normal_volume";
        result.interpretation = "Normal volume - " + std::to_string(current_volume) + " vs avg " + std::to_string(avg_volume);
    }
    
    return result;
}

std::string TechnicalIndicatorsCalculator::interpret_rsi_signal(double rsi) {
    if (rsi >= 70) return "overbought";
    if (rsi <= 30) return "oversold";
    if (rsi > 50) return "bullish";
    return "bearish";
}

std::string TechnicalIndicatorsCalculator::interpret_macd_signal(double macd, double signal, double histogram) {
    if (macd > signal && histogram > 0) return "bullish_crossover";
    if (macd < signal && histogram < 0) return "bearish_crossover";
    if (macd > signal) return "bullish";
    return "bearish";
}

std::string TechnicalIndicatorsCalculator::interpret_bollinger_signal(double price, double upper, double lower, double middle) {
    if (price > upper) return "overbought";
    if (price < lower) return "oversold";
    if (price > middle) return "bullish";
    return "bearish";
}

std::string TechnicalIndicatorsCalculator::interpret_sma_signal(double price, double sma_short, double sma_long) {
    if (sma_short > sma_long && price > sma_short) return "bullish_crossover";
    if (sma_short < sma_long && price < sma_short) return "bearish_crossover";
    if (price > sma_short) return "bullish";
    return "bearish";
}

std::vector<double> TechnicalIndicatorsCalculator::extract_closes(const std::vector<OHLC>& data) {
    std::vector<double> closes;
    closes.reserve(data.size());
    for (const auto& ohlc : data) {
        closes.push_back(ohlc.close);
    }
    return closes;
}

std::vector<double> TechnicalIndicatorsCalculator::extract_highs(const std::vector<OHLC>& data) {
    std::vector<double> highs;
    highs.reserve(data.size());
    for (const auto& ohlc : data) {
        highs.push_back(ohlc.high);
    }
    return highs;
}

std::vector<double> TechnicalIndicatorsCalculator::extract_lows(const std::vector<OHLC>& data) {
    std::vector<double> lows;
    lows.reserve(data.size());
    for (const auto& ohlc : data) {
        lows.push_back(ohlc.low);
    }
    return lows;
}

std::vector<double> TechnicalIndicatorsCalculator::extract_volumes(const std::vector<OHLC>& data) {
    std::vector<double> volumes;
    volumes.reserve(data.size());
    for (const auto& ohlc : data) {
        volumes.push_back(static_cast<double>(ohlc.volume));
    }
    return volumes;
}

std::vector<double> TechnicalIndicatorsCalculator::get_last_n_values(const std::vector<double>& values, int n) {
    if (values.size() <= static_cast<size_t>(n)) {
        return values;
    }
    return std::vector<double>(values.end() - n, values.end());
}

} // namespace backtesting
