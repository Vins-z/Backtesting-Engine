#pragma once

#include "common/types.h"
#include <string>
#include <vector>

namespace backtesting {

struct RegimeState {
    std::string regime;   // "Bull_Quiet", "Bull_Volatile", "Bear_Quiet", "Bear_Volatile", "Neutral"
    double trend_strength;
    double volatility_pct;
    double confidence;
};

class RegimeClassifier {
public:
    RegimeClassifier(int sma_period = 50, int atr_period = 14, int atr_ma_period = 50);

    RegimeState classify(const std::vector<OHLC>& history, size_t current_index) const;

private:
    int sma_period_;
    int atr_period_;
    int atr_ma_period_;

    static double compute_sma(const std::vector<double>& values, size_t start_idx, int period);
    static double compute_atr(const std::vector<OHLC>& data, size_t end_idx, int period);
};

}  // namespace backtesting
