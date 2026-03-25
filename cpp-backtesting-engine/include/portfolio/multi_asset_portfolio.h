#pragma once

#include "common/types.h"
#include "portfolio/portfolio_manager.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <Eigen/Dense>
#include <nlohmann/json.hpp>

namespace backtesting {

// Lightweight view over a multi-asset portfolio built on top of PortfolioManager
class MultiAssetPortfolio {
public:
    // Target weights per symbol (0–1, should sum to 1.0 for fully invested portfolio)
    using Weights = std::unordered_map<std::string, double>;

    explicit MultiAssetPortfolio(PortfolioManager& manager);

    // Set / get target weights
    void set_target_weights(const Weights& weights);
    Weights get_target_weights() const { return target_weights_; }

    // Compute correlation matrix for the provided symbols using their equity/price series
    Eigen::MatrixXd compute_correlation_matrix(const std::vector<std::string>& symbols,
                                               const std::unordered_map<std::string, std::vector<OHLC>>& history) const;

    // Basic risk metrics at portfolio level
    struct RiskMetrics {
        double portfolio_variance = 0.0;
        double portfolio_volatility = 0.0;
        double diversification_ratio = 0.0;
    };

    RiskMetrics compute_risk_metrics(const std::vector<std::string>& symbols,
                                     const std::unordered_map<std::string, std::vector<OHLC>>& history) const;

    // Simple mean-variance style optimization – returns optimal weights under constraint sum(w)=1, w>=0
    Weights optimize_weights_min_variance(const std::vector<std::string>& symbols,
                                          const std::unordered_map<std::string, std::vector<OHLC>>& history) const;

    // Serialize analytics for API responses
    nlohmann::json to_json(const std::vector<std::string>& symbols,
                           const std::unordered_map<std::string, std::vector<OHLC>>& history) const;

private:
    [[maybe_unused]] PortfolioManager& portfolio_manager_;
    Weights target_weights_;

    static Eigen::VectorXd build_return_series(const std::vector<OHLC>& ohlc);
};

} // namespace backtesting

