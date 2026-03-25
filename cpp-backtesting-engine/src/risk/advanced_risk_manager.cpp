#include "risk/advanced_risk_manager.h"
#include "portfolio/portfolio_manager.h"
#include "common/types.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace backtesting {

ExtendedRiskAnalytics::ExtendedRiskAnalytics() : current_regime_("normal") {
    trading_start_time_ = "09:30";
    trading_end_time_ = "16:00";
}

bool ExtendedRiskAnalytics::check_portfolio_risk(const PortfolioManager& portfolio) const {
    // Check correlation risk
    auto positions = portfolio.get_all_positions();
    std::vector<std::string> symbols;
    for (const auto& [symbol, pos] : positions) {
        if (pos.is_open()) {
            symbols.push_back(symbol);
        }
    }
    
    if (symbols.size() > 1) {
        for (size_t i = 0; i < symbols.size(); ++i) {
            for (size_t j = i + 1; j < symbols.size(); ++j) {
                // Simplified correlation check
                // In real implementation, would calculate actual correlation
                if (!check_correlation_risk(symbols[i], portfolio, symbols)) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

double ExtendedRiskAnalytics::calculate_var_historical(
    const PortfolioManager& portfolio,
    double confidence_level,
    int lookback_period
) const {
    auto equity_curve = portfolio.get_equity_curve();
    if (equity_curve.size() < 2) {
        return 0.0;
    }
    
    // Calculate daily returns
    std::vector<double> returns;
    for (size_t i = 1; i < equity_curve.size() && i < static_cast<size_t>(lookback_period + 1); ++i) {
        double prev_value = equity_curve[i-1].second;
        double curr_value = equity_curve[i].second;
        if (prev_value > 0) {
            returns.push_back((curr_value - prev_value) / prev_value);
        }
    }
    
    if (returns.empty()) {
        return 0.0;
    }
    
    // Sort returns
    std::sort(returns.begin(), returns.end());
    
    // Find VaR at confidence level
    int var_index = static_cast<int>((1.0 - confidence_level) * returns.size());
    if (var_index < 0) var_index = 0;
    if (var_index >= static_cast<int>(returns.size())) var_index = returns.size() - 1;
    
    double var_return = returns[var_index];
    double portfolio_value = portfolio.get_total_value();
    
    return std::abs(var_return * portfolio_value);
}

double ExtendedRiskAnalytics::calculate_var_parametric(
    const PortfolioManager& portfolio,
    double confidence_level
) const {
    auto equity_curve = portfolio.get_equity_curve();
    if (equity_curve.size() < 2) {
        return 0.0;
    }
    
    // Calculate mean and standard deviation of returns
    std::vector<double> returns;
    for (size_t i = 1; i < equity_curve.size(); ++i) {
        double prev_value = equity_curve[i-1].second;
        double curr_value = equity_curve[i].second;
        if (prev_value > 0) {
            returns.push_back((curr_value - prev_value) / prev_value);
        }
    }
    
    if (returns.empty()) {
        return 0.0;
    }
    
    double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    
    double variance = 0.0;
    for (double ret : returns) {
        variance += (ret - mean) * (ret - mean);
    }
    variance /= returns.size();
    double std_dev = std::sqrt(variance);
    
    // Z-score for confidence level (95% = 1.645, 99% = 2.326)
    double z_score = confidence_level == 0.95 ? 1.645 : 
                     confidence_level == 0.99 ? 2.326 : 1.0;
    
    double var_return = mean - (z_score * std_dev);
    double portfolio_value = portfolio.get_total_value();
    
    return std::abs(var_return * portfolio_value);
}

double ExtendedRiskAnalytics::calculate_cvar(
    const PortfolioManager& portfolio,
    double confidence_level,
    int lookback_period
) const {
    auto equity_curve = portfolio.get_equity_curve();
    if (equity_curve.size() < 2) {
        return 0.0;
    }
    
    // Calculate daily returns
    std::vector<double> returns;
    for (size_t i = 1; i < equity_curve.size() && i < static_cast<size_t>(lookback_period + 1); ++i) {
        double prev_value = equity_curve[i-1].second;
        double curr_value = equity_curve[i].second;
        if (prev_value > 0) {
            returns.push_back((curr_value - prev_value) / prev_value);
        }
    }
    
    if (returns.empty()) {
        return 0.0;
    }
    
    // Sort returns
    std::sort(returns.begin(), returns.end());
    
    // Find VaR threshold
    int var_index = static_cast<int>((1.0 - confidence_level) * returns.size());
    if (var_index < 0) var_index = 0;
    if (var_index >= static_cast<int>(returns.size())) var_index = returns.size() - 1;
    
    // Calculate average of returns below VaR threshold (Expected Shortfall)
    double sum_below_var = 0.0;
    int count_below_var = 0;
    for (int i = 0; i <= var_index; ++i) {
        sum_below_var += returns[i];
        count_below_var++;
    }
    
    if (count_below_var == 0) {
        return 0.0;
    }
    
    double avg_return_below_var = sum_below_var / count_below_var;
    double portfolio_value = portfolio.get_total_value();
    
    return std::abs(avg_return_below_var * portfolio_value);
}

double ExtendedRiskAnalytics::calculate_sortino_ratio(
    const PortfolioManager& portfolio,
    double risk_free_rate
) const {
    auto equity_curve = portfolio.get_equity_curve();
    if (equity_curve.size() < 2) {
        return 0.0;
    }
    
    // Calculate daily returns
    std::vector<double> returns;
    for (size_t i = 1; i < equity_curve.size(); ++i) {
        double prev_value = equity_curve[i-1].second;
        double curr_value = equity_curve[i].second;
        if (prev_value > 0) {
            returns.push_back((curr_value - prev_value) / prev_value);
        }
    }
    
    if (returns.empty()) {
        return 0.0;
    }
    
    // Calculate mean return
    double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double annualized_return = mean_return * 252; // Assuming 252 trading days
    
    // Calculate downside deviation (only negative returns)
    double downside_variance = 0.0;
    int downside_count = 0;
    for (double ret : returns) {
        if (ret < 0) {
            downside_variance += ret * ret;
            downside_count++;
        }
    }
    
    if (downside_count == 0) {
        return 0.0;
    }
    
    downside_variance /= downside_count;
    double downside_deviation = std::sqrt(downside_variance) * std::sqrt(252); // Annualized
    
    if (downside_deviation == 0.0) {
        return 0.0;
    }
    
    return (annualized_return - risk_free_rate) / downside_deviation;
}

double ExtendedRiskAnalytics::calculate_calmar_ratio(
    const PortfolioManager& portfolio
) const {
    auto stats = portfolio.calculate_portfolio_stats();
    double max_drawdown = std::abs(stats.max_drawdown);
    
    if (max_drawdown == 0.0) {
        return 0.0;
    }
    
    return stats.annualized_return / max_drawdown;
}

ExtendedRiskAnalytics::DrawdownAnalysis ExtendedRiskAnalytics::analyze_drawdowns(
    const PortfolioManager& portfolio
) const {
    DrawdownAnalysis analysis;
    analysis.max_drawdown = 0.0;
    analysis.current_drawdown = 0.0;
    analysis.drawdown_duration_days = 0;
    analysis.recovery_duration_days = 0;
    
    auto equity_curve = portfolio.get_equity_curve();
    if (equity_curve.size() < 2) {
        return analysis;
    }
    
    double peak = equity_curve[0].second;
    Timestamp peak_time = equity_curve[0].first;
    Timestamp drawdown_start_time = equity_curve[0].first;
    bool in_drawdown = false;
    int current_drawdown_days = 0;
    
    for (size_t i = 1; i < equity_curve.size(); ++i) {
        double current_value = equity_curve[i].second;
        Timestamp current_time = equity_curve[i].first;
        
        if (current_value > peak) {
            peak = current_value;
            peak_time = current_time;
            if (in_drawdown) {
                // Drawdown ended
                analysis.recovery_duration_days = current_drawdown_days;
                analysis.drawdown_end = current_time;
                in_drawdown = false;
                current_drawdown_days = 0;
            }
        } else {
            double drawdown = (peak - current_value) / peak;
            if (drawdown > analysis.max_drawdown) {
                analysis.max_drawdown = drawdown;
                analysis.drawdown_start = drawdown_start_time;
            }
            
            if (!in_drawdown && drawdown > 0.01) { // 1% threshold
                in_drawdown = true;
                drawdown_start_time = current_time;
                current_drawdown_days = 0;
            }
            
            if (in_drawdown) {
                current_drawdown_days++;
                analysis.drawdown_duration_days = std::max(analysis.drawdown_duration_days, current_drawdown_days);
            }
        }
    }
    
    // Current drawdown
    double current_value = portfolio.get_total_value();
    if (current_value < peak) {
        analysis.current_drawdown = (peak - current_value) / peak;
    }
    
    return analysis;
}

Quantity ExtendedRiskAnalytics::calculate_kelly_position_size(
    const std::string& symbol,
    Price current_price,
    const PortfolioManager& portfolio,
    double win_rate,
    double avg_win,
    double avg_loss
) const {
    (void)symbol; // Reserved for future use (symbol-specific Kelly adjustments)
    if (avg_loss == 0.0 || win_rate <= 0.0 || win_rate >= 1.0) {
        return 0;
    }
    
    // Kelly Criterion: f = (p * b - q) / b
    // where p = win rate, q = loss rate, b = avg_win / avg_loss
    double b = avg_win / std::abs(avg_loss);
    double q = 1.0 - win_rate;
    double kelly_fraction = (win_rate * b - q) / b;
    
    // Use fractional Kelly (multiply by kelly_fraction parameter)
    kelly_fraction *= params_.kelly_fraction;
    
    // Ensure positive and within bounds
    if (kelly_fraction <= 0.0 || kelly_fraction > 1.0) {
        kelly_fraction = 0.0;
    }
    
    double portfolio_value = portfolio.get_total_value();
    double position_value = portfolio_value * kelly_fraction;
    
    return static_cast<Quantity>(position_value / current_price);
}

Quantity ExtendedRiskAnalytics::calculate_optimal_f_position_size(
    const std::string& symbol,
    Price current_price,
    const PortfolioManager& portfolio,
    double win_rate,
    double avg_win,
    double avg_loss
) const {
    (void)symbol; // Reserved for future use (symbol-specific f adjustments)
    // Optimal f is similar to Kelly but uses geometric mean
    if (avg_loss == 0.0 || win_rate <= 0.0) {
        return 0;
    }
    
    // Simplified optimal f calculation
    double f = win_rate - ((1.0 - win_rate) / (avg_win / std::abs(avg_loss)));
    
    // Use fractional f
    f *= params_.kelly_fraction;
    
    if (f <= 0.0 || f > 1.0) {
        f = 0.0;
    }
    
    double portfolio_value = portfolio.get_total_value();
    double position_value = portfolio_value * f;
    
    return static_cast<Quantity>(position_value / current_price);
}

Quantity ExtendedRiskAnalytics::calculate_volatility_adjusted_size(
    const std::string& symbol,
    Price current_price,
    const PortfolioManager& portfolio,
    int lookback_period
) const {
    (void)symbol; // Reserved for future use (symbol-specific volatility)
    (void)lookback_period; // Reserved for future use (dynamic lookback)
    // Simplified volatility calculation
    // In real implementation, would use ATR or historical volatility
    double volatility = 0.02; // Assume 2% daily volatility
    
    // Adjust position size inversely to volatility
    double volatility_multiplier = params_.volatility_multiplier / (volatility * 10.0);
    volatility_multiplier = std::min(volatility_multiplier, 1.0);
    volatility_multiplier = std::max(volatility_multiplier, 0.1);
    
    double portfolio_value = portfolio.get_total_value();
    double base_allocation = 0.1; // 10% base allocation
    double position_value = portfolio_value * base_allocation * volatility_multiplier;
    
    return static_cast<Quantity>(position_value / current_price);
}

bool ExtendedRiskAnalytics::check_correlation_risk(
    const std::string& symbol,
    const PortfolioManager& portfolio,
    const std::vector<std::string>& existing_positions
) const {
    // Simplified correlation check
    // In real implementation, would calculate actual correlation matrix
    // For now, return true (allow trade)
    (void)symbol;
    (void)portfolio;
    (void)existing_positions;
    return true;
}

void ExtendedRiskAnalytics::adjust_risk_parameters(
    const std::vector<Fill>& recent_trades,
    const std::vector<std::pair<Timestamp, Price>>& equity_curve
) {
    // Adjust risk parameters based on recent performance
    if (recent_trades.empty() || equity_curve.size() < 10) {
        return;
    }
    
    // Calculate recent performance from equity curve
    // Simplified: use equity curve trend to adjust risk
    if (equity_curve.size() >= 2) {
        double recent_return = (equity_curve.back().second - equity_curve[equity_curve.size() - 2].second) / equity_curve[equity_curve.size() - 2].second;
        
        // Adjust Kelly fraction based on recent performance
        if (recent_return < -0.02) { // Down more than 2%
            params_.kelly_fraction = 0.1; // Reduce risk if losing
        } else if (recent_return > 0.02) { // Up more than 2%
            params_.kelly_fraction = 0.3; // Increase risk if winning
        } else {
            params_.kelly_fraction = 0.25; // Default
        }
    }
}

void ExtendedRiskAnalytics::track_mae(const std::string& symbol, Price entry_price, Price current_price) {
    double mae = std::abs(current_price - entry_price) / entry_price;
    auto it = mae_tracking_.find(symbol);
    if (it == mae_tracking_.end() || mae > it->second) {
        mae_tracking_[symbol] = mae;
    }
}

bool ExtendedRiskAnalytics::check_mae_limits(const std::string& symbol) const {
    auto it = mae_tracking_.find(symbol);
    if (it == mae_tracking_.end()) {
        return true;
    }
    return it->second <= params_.mae_threshold;
}

bool ExtendedRiskAnalytics::check_time_based_risk(const Timestamp& current_time) const {
    return is_trading_hours(current_time);
}

void ExtendedRiskAnalytics::set_trading_hours(const std::string& start_time, const std::string& end_time) {
    trading_start_time_ = start_time;
    trading_end_time_ = end_time;
}

void ExtendedRiskAnalytics::set_news_events(const std::vector<std::string>& high_impact_events) {
    high_impact_events_ = high_impact_events;
}

bool ExtendedRiskAnalytics::check_news_risk(const Timestamp& current_time) const {
    return !is_news_time(current_time);
}

void ExtendedRiskAnalytics::set_market_regime(const std::string& regime) {
    current_regime_ = regime;
}

void ExtendedRiskAnalytics::adjust_risk_for_regime(const std::string& regime) {
    if (regime == "high_volatility") {
        params_.regime_risk_multiplier = 0.5; // Reduce risk by 50%
    } else if (regime == "low_volatility") {
        params_.regime_risk_multiplier = 1.2; // Increase risk by 20%
    } else {
        params_.regime_risk_multiplier = 1.0; // Normal
    }
}

double ExtendedRiskAnalytics::predict_risk_score(
    const std::string& symbol,
    Price current_price,
    const PortfolioManager& portfolio
) const {
    // Simplified risk score (0-1, where 1 is highest risk)
    // In real implementation, would use ML model
    (void)symbol;
    (void)current_price;
    (void)portfolio;
    return 0.5; // Default medium risk
}

std::vector<double> ExtendedRiskAnalytics::stress_test_portfolio(
    const PortfolioManager& portfolio,
    const std::vector<double>& stress_scenarios
) const {
    std::vector<double> results;
    double base_value = portfolio.get_total_value();
    
    for (double scenario : stress_scenarios) {
        // Apply stress scenario (e.g., -10% market crash)
        double stressed_value = base_value * (1.0 + scenario);
        results.push_back(stressed_value);
    }
    
    return results;
}

nlohmann::json ExtendedRiskAnalytics::generate_risk_report(const PortfolioManager& portfolio) const {
    nlohmann::json report;
    
    // VaR metrics
    report["var_historical_95"] = calculate_var_historical(portfolio, 0.95);
    report["var_historical_99"] = calculate_var_historical(portfolio, 0.99);
    report["var_parametric_95"] = calculate_var_parametric(portfolio, 0.95);
    
    // CVaR
    report["cvar_95"] = calculate_cvar(portfolio, 0.95);
    report["cvar_99"] = calculate_cvar(portfolio, 0.99);
    
    // Risk-adjusted returns
    report["sortino_ratio"] = calculate_sortino_ratio(portfolio);
    report["calmar_ratio"] = calculate_calmar_ratio(portfolio);
    
    // Drawdown analysis
    auto drawdown_analysis = analyze_drawdowns(portfolio);
    report["max_drawdown"] = drawdown_analysis.max_drawdown;
    report["current_drawdown"] = drawdown_analysis.current_drawdown;
    report["drawdown_duration_days"] = drawdown_analysis.drawdown_duration_days;
    report["recovery_duration_days"] = drawdown_analysis.recovery_duration_days;
    
    // Portfolio metrics
    auto stats = portfolio.calculate_portfolio_stats();
    report["sharpe_ratio"] = stats.sharpe_ratio;
    report["total_return"] = stats.total_return;
    report["volatility"] = stats.volatility;
    
    return report;
}

// Helper methods
double ExtendedRiskAnalytics::calculate_correlation(
    const std::vector<double>& returns1,
    const std::vector<double>& returns2
) const {
    if (returns1.size() != returns2.size() || returns1.size() < 2) {
        return 0.0;
    }
    
    double mean1 = std::accumulate(returns1.begin(), returns1.end(), 0.0) / returns1.size();
    double mean2 = std::accumulate(returns2.begin(), returns2.end(), 0.0) / returns2.size();
    
    double covariance = 0.0;
    double var1 = 0.0;
    double var2 = 0.0;
    
    for (size_t i = 0; i < returns1.size(); ++i) {
        double diff1 = returns1[i] - mean1;
        double diff2 = returns2[i] - mean2;
        covariance += diff1 * diff2;
        var1 += diff1 * diff1;
        var2 += diff2 * diff2;
    }
    
    covariance /= (returns1.size() - 1);
    var1 /= (returns1.size() - 1);
    var2 /= (returns2.size() - 1);
    
    double stddev1 = std::sqrt(var1);
    double stddev2 = std::sqrt(var2);
    
    if (stddev1 == 0.0 || stddev2 == 0.0) {
        return 0.0;
    }
    
    return covariance / (stddev1 * stddev2);
}

double ExtendedRiskAnalytics::calculate_volatility(
    const std::vector<Price>& prices,
    int lookback_period
) const {
    if (prices.size() < 2) {
        return 0.0;
    }
    
    std::vector<double> returns;
    int start = std::max(0, static_cast<int>(prices.size()) - lookback_period);
    
    for (size_t i = start + 1; i < prices.size(); ++i) {
        if (prices[i-1] > 0) {
            returns.push_back((prices[i] - prices[i-1]) / prices[i-1]);
        }
    }
    
    if (returns.empty()) {
        return 0.0;
    }
    
    double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double variance = 0.0;
    for (double ret : returns) {
        variance += (ret - mean) * (ret - mean);
    }
    variance /= returns.size();
    
    return std::sqrt(variance);
}

bool ExtendedRiskAnalytics::is_trading_hours(const Timestamp& time) const {
    // Simplified - would parse trading_start_time_ and trading_end_time_
    // For now, assume 9:30 AM to 4:00 PM ET
    (void)time;
    return true;
}

bool ExtendedRiskAnalytics::is_news_time(const Timestamp& time) const {
    // Simplified - would check against high_impact_events_
    (void)time;
    (void)high_impact_events_;
    return false;
}

std::string ExtendedRiskAnalytics::detect_market_regime(const std::vector<Price>& prices) const {
    if (prices.size() < 20) {
        return "normal";
    }
    
    double volatility = calculate_volatility(prices, 20);
    
    if (volatility > 0.03) {
        return "high_volatility";
    } else if (volatility < 0.01) {
        return "low_volatility";
    }
    
    return "normal";
}

} // namespace backtesting

