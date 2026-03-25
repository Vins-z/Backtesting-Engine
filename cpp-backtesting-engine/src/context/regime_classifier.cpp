#include "context/regime_classifier.h"
#include <cmath>
#include <algorithm>

namespace backtesting {

RegimeClassifier::RegimeClassifier(int sma_period, int atr_period, int atr_ma_period)
    : sma_period_(sma_period), atr_period_(atr_period), atr_ma_period_(atr_ma_period) {}

double RegimeClassifier::compute_sma(const std::vector<double>& values, size_t start_idx, int period) {
    if (start_idx + static_cast<size_t>(period) > values.size() || period <= 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < period; ++i) {
        sum += values[start_idx + i];
    }
    return sum / period;
}

double RegimeClassifier::compute_atr(const std::vector<OHLC>& data, size_t end_idx, int period) {
    if (data.size() < static_cast<size_t>(period + 1) || end_idx < static_cast<size_t>(period)) return 0.0;
    size_t start = end_idx >= static_cast<size_t>(period) ? end_idx - period : 0;
    std::vector<double> true_ranges;
    for (size_t i = start + 1; i <= end_idx && i < data.size(); ++i) {
        double tr1 = data[i].high - data[i].low;
        double tr2 = std::abs(data[i].high - data[i - 1].close);
        double tr3 = std::abs(data[i].low - data[i - 1].close);
        true_ranges.push_back(std::max({tr1, tr2, tr3}));
    }
    if (true_ranges.size() < static_cast<size_t>(period)) return 0.0;
    double sum = 0.0;
    for (size_t i = true_ranges.size() - static_cast<size_t>(period); i < true_ranges.size(); ++i) {
        sum += true_ranges[i];
    }
    return sum / period;
}

RegimeState RegimeClassifier::classify(const std::vector<OHLC>& history, size_t current_index) const {
    RegimeState out;
    out.regime = "Neutral";
    out.trend_strength = 0.0;
    out.volatility_pct = 0.0;
    out.confidence = 0.0;

    const size_t min_bars = static_cast<size_t>(std::max(sma_period_, atr_ma_period_) + atr_period_ + 5);
    if (history.size() < min_bars || current_index < min_bars) return out;

    double current_price = history[current_index].close;
    if (current_price <= 0.0) return out;

    // Trend: price vs SMA
    std::vector<double> closes;
    for (size_t i = 0; i <= current_index && i < history.size(); ++i) {
        closes.push_back(history[i].close);
    }
    size_t sma_start = closes.size() >= static_cast<size_t>(sma_period_)
        ? closes.size() - static_cast<size_t>(sma_period_) : 0;
    double sma = compute_sma(closes, sma_start, sma_period_);
    bool bullish = current_price > sma;
    double trend_pct = (sma > 0.0) ? ((current_price - sma) / sma * 100.0) : 0.0;
    out.trend_strength = std::abs(trend_pct);

    // Volatility: ATR(14) vs ATR_MA(50)
    double atr = compute_atr(history, current_index, atr_period_);
    out.volatility_pct = (current_price > 0.0 && atr > 0.0) ? (atr / current_price * 100.0) : 0.0;

    std::vector<double> atr_series;
    for (size_t i = static_cast<size_t>(atr_period_); i <= current_index; ++i) {
        double a = compute_atr(history, i, atr_period_);
        if (a > 0.0) atr_series.push_back(a);
    }
    double atr_ma = 0.0;
    if (atr_series.size() >= static_cast<size_t>(atr_ma_period_)) {
        size_t atr_ma_start = atr_series.size() - static_cast<size_t>(atr_ma_period_);
        for (size_t i = atr_ma_start; i < atr_series.size(); ++i) atr_ma += atr_series[i];
        atr_ma /= atr_ma_period_;
    }
    bool high_vol = (atr_ma > 0.0 && atr > atr_ma);

    // Rule-based 2x2: trend × vol
    if (bullish && !high_vol) {
        out.regime = "Bull_Quiet";
        out.confidence = 0.85;
    } else if (bullish && high_vol) {
        out.regime = "Bull_Volatile";
        out.confidence = 0.80;
    } else if (!bullish && !high_vol) {
        out.regime = "Bear_Quiet";
        out.confidence = 0.85;
    } else {
        out.regime = "Bear_Volatile";
        out.confidence = 0.80;
    }

    return out;
}

}  // namespace backtesting
