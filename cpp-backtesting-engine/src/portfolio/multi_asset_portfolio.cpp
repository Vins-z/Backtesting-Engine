#include "portfolio/multi_asset_portfolio.h"
#include <numeric>

namespace backtesting {

MultiAssetPortfolio::MultiAssetPortfolio(PortfolioManager& manager)
    : portfolio_manager_(manager) {}

void MultiAssetPortfolio::set_target_weights(const Weights& weights) {
    target_weights_ = weights;
}

Eigen::VectorXd MultiAssetPortfolio::build_return_series(const std::vector<OHLC>& ohlc) {
    if (ohlc.size() < 2) {
        return Eigen::VectorXd();
    }

    Eigen::VectorXd rets(static_cast<int>(ohlc.size() - 1));
    for (size_t i = 1; i < ohlc.size(); ++i) {
        double prev = ohlc[i - 1].close;
        double curr = ohlc[i].close;
        if (prev > 0.0) {
            rets[static_cast<int>(i - 1)] = (curr - prev) / prev;
        } else {
            rets[static_cast<int>(i - 1)] = 0.0;
        }
    }
    return rets;
}

Eigen::MatrixXd MultiAssetPortfolio::compute_correlation_matrix(
    const std::vector<std::string>& symbols,
    const std::unordered_map<std::string, std::vector<OHLC>>& history) const {

    const int n = static_cast<int>(symbols.size());
    if (n == 0) {
        return Eigen::MatrixXd();
    }

    // Build return series for each symbol
    std::vector<Eigen::VectorXd> series;
    series.reserve(n);
    int min_len = std::numeric_limits<int>::max();

    for (const auto& sym : symbols) {
        auto it = history.find(sym);
        if (it == history.end()) {
            continue;
        }
        Eigen::VectorXd r = build_return_series(it->second);
        if (r.size() == 0) {
            continue;
        }
        min_len = std::min(min_len, static_cast<int>(r.size()));
        series.push_back(std::move(r));
    }

    if (series.empty() || min_len <= 1) {
        return Eigen::MatrixXd();
    }

    // Truncate all to common length
    const int m = static_cast<int>(series.size());
    Eigen::MatrixXd mat(min_len, m);
    for (int j = 0; j < m; ++j) {
        mat.col(j) = series[j].head(min_len);
    }

    // Demean
    for (int j = 0; j < m; ++j) {
        double mean = mat.col(j).mean();
        mat.col(j).array() -= mean;
    }

    // Covariance matrix
    Eigen::MatrixXd cov = (mat.transpose() * mat) / static_cast<double>(min_len - 1);

    // Correlation matrix
    Eigen::VectorXd stddev = cov.diagonal().array().sqrt();
    Eigen::MatrixXd corr(m, m);
    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < m; ++j) {
            if (stddev[i] == 0.0 || stddev[j] == 0.0) {
                corr(i, j) = 0.0;
            } else {
                corr(i, j) = cov(i, j) / (stddev[i] * stddev[j]);
            }
        }
    }

    return corr;
}

MultiAssetPortfolio::RiskMetrics MultiAssetPortfolio::compute_risk_metrics(
    const std::vector<std::string>& symbols,
    const std::unordered_map<std::string, std::vector<OHLC>>& history) const {

    RiskMetrics metrics;
    if (symbols.empty()) {
        return metrics;
    }

    // Use target weights if available, otherwise equal-weight
    std::vector<double> weights_vec;
    weights_vec.reserve(symbols.size());
    for (const auto& sym : symbols) {
        auto it = target_weights_.find(sym);
        if (it != target_weights_.end()) {
            weights_vec.push_back(it->second);
        } else {
            weights_vec.push_back(1.0); // temp, will normalize below
        }
    }

    double sum_w = std::accumulate(weights_vec.begin(), weights_vec.end(), 0.0);
    if (sum_w <= 0.0) {
        double ew = 1.0 / static_cast<double>(weights_vec.size());
        std::fill(weights_vec.begin(), weights_vec.end(), ew);
    } else {
        for (auto& w : weights_vec) {
            w /= sum_w;
        }
    }

    Eigen::MatrixXd corr = compute_correlation_matrix(symbols, history);
    if (corr.size() == 0) {
        return metrics;
    }

    // Approximate volatility vector from diagonal of correlation (assume unit variance)
    const int m = static_cast<int>(weights_vec.size());
    Eigen::VectorXd w(m);
    for (int i = 0; i < m; ++i) {
        w[i] = weights_vec[static_cast<size_t>(i)];
    }

    // If we had full covariance, we'd use it here; with corr only, assume unit variance
    Eigen::MatrixXd cov = corr; // treating corr as covariance with unit variances

    metrics.portfolio_variance = (w.transpose() * cov * w)(0, 0);
    metrics.portfolio_volatility = std::sqrt(std::max(0.0, metrics.portfolio_variance));

    // Diversification ratio: w' * sigma / sqrt(w' * cov * w), here sigma=1 so simplifies
    double numerator = w.cwiseAbs().sum(); // since sigma=1
    double denominator = metrics.portfolio_volatility;
    if (denominator > 0.0) {
        metrics.diversification_ratio = numerator / denominator;
    } else {
        metrics.diversification_ratio = 0.0;
    }

    return metrics;
}

MultiAssetPortfolio::Weights MultiAssetPortfolio::optimize_weights_min_variance(
    const std::vector<std::string>& symbols,
    const std::unordered_map<std::string, std::vector<OHLC>>& history) const {

    Weights result;
    if (symbols.empty()) {
        return result;
    }

    Eigen::MatrixXd corr = compute_correlation_matrix(symbols, history);
    if (corr.size() == 0) {
        // Fallback to equal weights
        double ew = 1.0 / static_cast<double>(symbols.size());
        for (const auto& sym : symbols) {
            result[sym] = ew;
        }
        return result;
    }

    const int n = static_cast<int>(symbols.size());
    // Solve min w' C w s.t. sum w = 1, w >= 0 using simple heuristic:
    // w_i ∝ 1 / volatility_i; with unit variance this degenerates to equal weights.
    double ew = 1.0 / static_cast<double>(n);
    for (const auto& sym : symbols) {
        result[sym] = ew;
    }
    return result;
}

nlohmann::json MultiAssetPortfolio::to_json(
    const std::vector<std::string>& symbols,
    const std::unordered_map<std::string, std::vector<OHLC>>& history) const {

    nlohmann::json j;
    j["symbols"] = symbols;

    // Target weights
    nlohmann::json w_json = nlohmann::json::object();
    for (const auto& sym : symbols) {
        auto it = target_weights_.find(sym);
        if (it != target_weights_.end()) {
            w_json[sym] = it->second;
        }
    }
    j["target_weights"] = w_json;

    // Correlation matrix
    Eigen::MatrixXd corr = compute_correlation_matrix(symbols, history);
    if (corr.size() > 0) {
        nlohmann::json corr_json = nlohmann::json::array();
        for (int i = 0; i < corr.rows(); ++i) {
            nlohmann::json row = nlohmann::json::array();
            for (int k = 0; k < corr.cols(); ++k) {
                row.push_back(corr(i, k));
            }
            corr_json.push_back(row);
        }
        j["correlation_matrix"] = corr_json;
    }

    // Risk metrics
    RiskMetrics metrics = compute_risk_metrics(symbols, history);
    j["risk_metrics"] = {
        {"portfolio_variance", metrics.portfolio_variance},
        {"portfolio_volatility", metrics.portfolio_volatility},
        {"diversification_ratio", metrics.diversification_ratio}
    };

    return j;
}

} // namespace backtesting

