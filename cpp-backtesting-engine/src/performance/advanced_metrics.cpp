#include "performance/advanced_metrics.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>

namespace backtesting {

AdvancedPerformanceAnalyzer::AdvancedPerformanceAnalyzer() {
    // Initialize advanced analyzer
}

AdvancedMetrics AdvancedPerformanceAnalyzer::calculate_advanced_metrics(
    const std::vector<std::pair<Timestamp, Price>>& equity_curve,
    const std::vector<Fill>& trades,
    Price initial_capital,
    Price risk_free_rate
) const {
    AdvancedMetrics metrics;
    
    if (equity_curve.empty()) {
        return metrics;
    }
    
    auto returns = calculate_returns(equity_curve);
    
    // Risk-adjusted returns
    metrics.calmar_ratio = calculate_calmar_ratio(equity_curve, returns);
    metrics.sortino_ratio = calculate_sortino_ratio(returns, risk_free_rate);
    metrics.information_ratio = calculate_information_ratio(returns, risk_free_rate);
    metrics.treynor_ratio = calculate_treynor_ratio(returns, risk_free_rate);
    metrics.jensen_alpha = calculate_jensen_alpha(returns, risk_free_rate);
    
    // Drawdown analysis
    auto drawdown_metrics = calculate_drawdown_metrics(equity_curve);
    metrics.avg_drawdown = drawdown_metrics.first;
    metrics.avg_drawdown_duration = drawdown_metrics.second;
    metrics.recovery_factor = calculate_recovery_factor(equity_curve);
    metrics.ulcer_index = calculate_ulcer_index(equity_curve);
    
    // Trade analysis
    calculate_trade_metrics(trades, metrics);
    
    // Volatility analysis
    metrics.downside_deviation = calculate_downside_deviation(returns);
    metrics.upside_deviation = calculate_upside_deviation(returns);
    metrics.var_95 = calculate_var(returns, 0.95);
    metrics.cvar_95 = calculate_cvar(returns, 0.95);
    
    // Time-based analysis
    auto monthly_returns = calculate_monthly_returns(equity_curve);
    std::copy(monthly_returns.begin(), monthly_returns.end(), metrics.monthly_returns);
    
    // Advanced ratios
    metrics.gain_to_pain_ratio = calculate_gain_to_pain_ratio(trades);
    metrics.profit_factor_ratio = calculate_profit_factor_ratio(trades);
    metrics.risk_reward_ratio = calculate_risk_reward_ratio(trades);
    metrics.payoff_ratio = calculate_payoff_ratio(trades);
    
    // Machine learning metrics
    metrics.strategy_consistency = calculate_strategy_consistency(trades);
    metrics.market_correlation = 0.0; // Will be set when market data is available
    metrics.regime_adaptation = calculate_regime_adaptation(trades);
    
    return metrics;
}

std::vector<double> AdvancedPerformanceAnalyzer::calculate_returns(
    const std::vector<std::pair<Timestamp, Price>>& equity_curve
) const {
    std::vector<double> returns;
    returns.reserve(equity_curve.size() - 1);
    
    for (size_t i = 1; i < equity_curve.size(); ++i) {
        double return_val = (equity_curve[i].second - equity_curve[i-1].second) / equity_curve[i-1].second;
        returns.push_back(return_val);
    }
    
    return returns;
}

double AdvancedPerformanceAnalyzer::calculate_calmar_ratio(
    const std::vector<std::pair<Timestamp, Price>>& equity_curve,
    const std::vector<double>& returns
) const {
    if (returns.empty()) return 0.0;
    
    double avg_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double max_dd = calculate_max_drawdown(equity_curve);
    
    if (max_dd == 0.0) return 0.0;
    
    return (avg_return * 252) / max_dd; // Annualized
}

double AdvancedPerformanceAnalyzer::calculate_sortino_ratio(
    const std::vector<double>& returns,
    double risk_free_rate
) const {
    if (returns.empty()) return 0.0;
    
    double avg_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double downside_dev = calculate_downside_deviation(returns);
    
    if (downside_dev == 0.0) return 0.0;
    
    return (avg_return - risk_free_rate/252) / downside_dev;
}

double AdvancedPerformanceAnalyzer::calculate_information_ratio(
    const std::vector<double>& returns,
    double risk_free_rate
) const {
    if (returns.empty()) return 0.0;
    
    double avg_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double excess_return = avg_return - risk_free_rate/252;
    double volatility = calculate_volatility(returns);
    
    if (volatility == 0.0) return 0.0;
    
    return excess_return / volatility;
}

double AdvancedPerformanceAnalyzer::calculate_treynor_ratio(
    const std::vector<double>& returns,
    double risk_free_rate
) const {
    if (returns.empty()) return 0.0;
    
    double avg_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double beta = 1.0; // Assuming market beta of 1.0 for simplicity
    
    if (beta == 0.0) return 0.0;
    
    return (avg_return - risk_free_rate/252) / beta;
}

double AdvancedPerformanceAnalyzer::calculate_jensen_alpha(
    const std::vector<double>& returns,
    double risk_free_rate
) const {
    if (returns.empty()) return 0.0;
    
    double avg_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double beta = 1.0; // Assuming market beta of 1.0 for simplicity
    double market_return = 0.10; // Assuming 10% market return
    
    return avg_return - (risk_free_rate/252 + beta * (market_return/252 - risk_free_rate/252));
}

std::pair<double, double> AdvancedPerformanceAnalyzer::calculate_drawdown_metrics(
    const std::vector<std::pair<Timestamp, Price>>& equity_curve
) const {
    if (equity_curve.empty()) return {0.0, 0.0};
    
    std::vector<double> drawdowns;
    std::vector<int> durations;
    
    Price peak = equity_curve[0].second;
    int duration = 0;
    
    for (const auto& point : equity_curve) {
        if (point.second > peak) {
            peak = point.second;
            if (duration > 0) {
                durations.push_back(duration);
                duration = 0;
            }
        } else {
            double drawdown = (peak - point.second) / peak;
            drawdowns.push_back(drawdown);
            duration++;
        }
    }
    
    if (duration > 0) {
        durations.push_back(duration);
    }
    
    double avg_drawdown = drawdowns.empty() ? 0.0 : 
        std::accumulate(drawdowns.begin(), drawdowns.end(), 0.0) / drawdowns.size();
    
    double avg_duration = durations.empty() ? 0.0 :
        std::accumulate(durations.begin(), durations.end(), 0.0) / durations.size();
    
    return {avg_drawdown, avg_duration};
}

double AdvancedPerformanceAnalyzer::calculate_recovery_factor(
    const std::vector<std::pair<Timestamp, Price>>& equity_curve
) const {
    if (equity_curve.empty()) return 0.0;
    
    Price initial = equity_curve[0].second;
    Price final = equity_curve.back().second;
    double total_return = (final - initial) / initial;
    double max_dd = calculate_max_drawdown(equity_curve);
    
    if (max_dd == 0.0) return 0.0;
    
    return total_return / max_dd;
}

double AdvancedPerformanceAnalyzer::calculate_ulcer_index(
    const std::vector<std::pair<Timestamp, Price>>& equity_curve
) const {
    if (equity_curve.empty()) return 0.0;
    
    std::vector<double> squared_drawdowns;
    Price peak = equity_curve[0].second;
    
    for (const auto& point : equity_curve) {
        if (point.second > peak) {
            peak = point.second;
        } else {
            double drawdown = (peak - point.second) / peak;
            squared_drawdowns.push_back(drawdown * drawdown);
        }
    }
    
    if (squared_drawdowns.empty()) return 0.0;
    
    double avg_squared_dd = std::accumulate(squared_drawdowns.begin(), squared_drawdowns.end(), 0.0) / squared_drawdowns.size();
    return std::sqrt(avg_squared_dd);
}

void AdvancedPerformanceAnalyzer::calculate_trade_metrics(
    const std::vector<Fill>& trades,
    AdvancedMetrics& metrics
) const {
    if (trades.empty()) return;
    
    std::vector<double> profits, losses;
    
    for (const auto& trade : trades) {
        double pnl = trade.pnl;
        if (pnl > 0) {
            profits.push_back(pnl);
        } else {
            losses.push_back(std::abs(pnl));
        }
    }
    
    // Average win/loss
    metrics.avg_win = profits.empty() ? 0.0 : 
        std::accumulate(profits.begin(), profits.end(), 0.0) / profits.size();
    metrics.avg_loss = losses.empty() ? 0.0 :
        std::accumulate(losses.begin(), losses.end(), 0.0) / losses.size();
    
    // Largest win/loss
    metrics.largest_win = profits.empty() ? 0.0 : *std::max_element(profits.begin(), profits.end());
    metrics.largest_loss = losses.empty() ? 0.0 : *std::max_element(losses.begin(), losses.end());
    
    // Win/loss ratio
    metrics.win_loss_ratio = metrics.avg_loss == 0.0 ? 0.0 : metrics.avg_win / metrics.avg_loss;
    
    // Expectancy
    double win_rate = static_cast<double>(profits.size()) / trades.size();
    metrics.expectancy = (win_rate * metrics.avg_win) - ((1 - win_rate) * metrics.avg_loss);
    
    // Kelly criterion
    if (metrics.avg_loss > 0.0) {
        metrics.kelly_criterion = (win_rate * metrics.avg_win - (1 - win_rate) * metrics.avg_loss) / metrics.avg_win;
    } else {
        metrics.kelly_criterion = 0.0;
    }
}

double AdvancedPerformanceAnalyzer::calculate_downside_deviation(
    const std::vector<double>& returns
) const {
    if (returns.empty()) return 0.0;
    
    double avg_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    std::vector<double> downside_returns;
    
    for (double ret : returns) {
        if (ret < avg_return) {
            downside_returns.push_back((ret - avg_return) * (ret - avg_return));
        }
    }
    
    if (downside_returns.empty()) return 0.0;
    
    double avg_downside = std::accumulate(downside_returns.begin(), downside_returns.end(), 0.0) / downside_returns.size();
    return std::sqrt(avg_downside);
}

double AdvancedPerformanceAnalyzer::calculate_upside_deviation(
    const std::vector<double>& returns
) const {
    if (returns.empty()) return 0.0;
    
    double avg_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    std::vector<double> upside_returns;
    
    for (double ret : returns) {
        if (ret > avg_return) {
            upside_returns.push_back((ret - avg_return) * (ret - avg_return));
        }
    }
    
    if (upside_returns.empty()) return 0.0;
    
    double avg_upside = std::accumulate(upside_returns.begin(), upside_returns.end(), 0.0) / upside_returns.size();
    return std::sqrt(avg_upside);
}

double AdvancedPerformanceAnalyzer::calculate_var(
    const std::vector<double>& returns,
    double confidence
) const {
    if (returns.empty()) return 0.0;
    
    std::vector<double> sorted_returns = returns;
    std::sort(sorted_returns.begin(), sorted_returns.end());
    
    int index = static_cast<int>((1 - confidence) * sorted_returns.size());
    return sorted_returns[index];
}

double AdvancedPerformanceAnalyzer::calculate_cvar(
    const std::vector<double>& returns,
    double confidence
) const {
    if (returns.empty()) return 0.0;
    
    double var = calculate_var(returns, confidence);
    std::vector<double> tail_returns;
    
    for (double ret : returns) {
        if (ret <= var) {
            tail_returns.push_back(ret);
        }
    }
    
    if (tail_returns.empty()) return var;
    
    return std::accumulate(tail_returns.begin(), tail_returns.end(), 0.0) / tail_returns.size();
}

std::vector<double> AdvancedPerformanceAnalyzer::calculate_monthly_returns(
    const std::vector<std::pair<Timestamp, Price>>& equity_curve
) const {
    std::vector<double> monthly_returns(12, 0.0);
    // Monthly return computation is not implemented yet in this build.
    // Return the pre-sized vector (initialized to zeros).
    return monthly_returns;
}

double AdvancedPerformanceAnalyzer::calculate_gain_to_pain_ratio(
    const std::vector<Fill>& trades
) const {
    if (trades.empty()) return 0.0;
    
    double total_gain = 0.0, total_pain = 0.0;
    
    for (const auto& trade : trades) {
        if (trade.pnl > 0) {
            total_gain += trade.pnl;
        } else {
            total_pain += std::abs(trade.pnl);
        }
    }
    
    return total_pain == 0.0 ? 0.0 : total_gain / total_pain;
}

double AdvancedPerformanceAnalyzer::calculate_profit_factor_ratio(
    const std::vector<Fill>& trades
) const {
    return calculate_gain_to_pain_ratio(trades);
}

double AdvancedPerformanceAnalyzer::calculate_risk_reward_ratio(
    const std::vector<Fill>& trades
) const {
    if (trades.empty()) return 0.0;
    
    double max_profit = 0.0, max_loss = 0.0;
    
    for (const auto& trade : trades) {
        if (trade.pnl > max_profit) max_profit = trade.pnl;
        if (trade.pnl < -max_loss) max_loss = std::abs(trade.pnl);
    }
    
    return max_loss == 0.0 ? 0.0 : max_profit / max_loss;
}

double AdvancedPerformanceAnalyzer::calculate_payoff_ratio(
    const std::vector<Fill>& trades
) const {
    if (trades.empty()) return 0.0;
    
    double total_profit = 0.0, total_loss = 0.0;
    int profit_count = 0, loss_count = 0;
    
    for (const auto& trade : trades) {
        if (trade.pnl > 0) {
            total_profit += trade.pnl;
            profit_count++;
        } else {
            total_loss += std::abs(trade.pnl);
            loss_count++;
        }
    }
    
    double avg_profit = profit_count > 0 ? total_profit / profit_count : 0.0;
    double avg_loss = loss_count > 0 ? total_loss / loss_count : 0.0;
    
    return avg_loss == 0.0 ? 0.0 : avg_profit / avg_loss;
}

double AdvancedPerformanceAnalyzer::calculate_strategy_consistency(
    const std::vector<Fill>& trades
) const {
    if (trades.size() < 2) return 0.0;
    
    std::vector<double> consecutive_wins;
    int current_streak = 0;
    
    for (const auto& trade : trades) {
        if (trade.pnl > 0) {
            current_streak++;
        } else {
            if (current_streak > 0) {
                consecutive_wins.push_back(current_streak);
                current_streak = 0;
            }
        }
    }
    
    if (current_streak > 0) {
        consecutive_wins.push_back(current_streak);
    }
    
    if (consecutive_wins.empty()) return 0.0;
    
    double avg_streak = std::accumulate(consecutive_wins.begin(), consecutive_wins.end(), 0.0) / consecutive_wins.size();
    return avg_streak / trades.size(); // Normalized consistency
}

double AdvancedPerformanceAnalyzer::calculate_regime_adaptation(
    const std::vector<Fill>& trades
) const {
    if (trades.size() < 10) return 0.0;
    
    // Calculate performance in different time periods
    int mid_point = trades.size() / 2;
    std::vector<Fill> first_half(trades.begin(), trades.begin() + mid_point);
    std::vector<Fill> second_half(trades.begin() + mid_point, trades.end());
    
    double first_performance = 0.0, second_performance = 0.0;
    
    for (const auto& trade : first_half) {
        first_performance += trade.pnl;
    }
    
    for (const auto& trade : second_half) {
        second_performance += trade.pnl;
    }
    
    // Normalize by number of trades
    first_performance /= first_half.size();
    second_performance /= second_half.size();
    
    // Adaptation score based on performance consistency
    double performance_diff = std::abs(first_performance - second_performance);
    double avg_performance = (first_performance + second_performance) / 2.0;
    
    return avg_performance == 0.0 ? 0.0 : 1.0 - (performance_diff / std::abs(avg_performance));
}

double AdvancedPerformanceAnalyzer::calculate_volatility(
    const std::vector<double>& returns
) const {
    if (returns.empty()) return 0.0;
    
    double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double variance = 0.0;
    
    for (double ret : returns) {
        variance += (ret - mean) * (ret - mean);
    }
    
    variance /= returns.size();
    return std::sqrt(variance);
}

double AdvancedPerformanceAnalyzer::calculate_max_drawdown(
    const std::vector<std::pair<Timestamp, Price>>& equity_curve
) const {
    if (equity_curve.empty()) return 0.0;
    
    double max_dd = 0.0;
    Price peak = equity_curve[0].second;
    
    for (const auto& point : equity_curve) {
        if (point.second > peak) {
            peak = point.second;
        } else {
            double drawdown = (peak - point.second) / peak;
            if (drawdown > max_dd) {
                max_dd = drawdown;
            }
        }
    }
    
    return max_dd;
}

nlohmann::json AdvancedMetrics::to_json() const {
    nlohmann::json j;
    
    // Risk-adjusted returns
    j["calmar_ratio"] = calmar_ratio;
    j["sortino_ratio"] = sortino_ratio;
    j["information_ratio"] = information_ratio;
    j["treynor_ratio"] = treynor_ratio;
    j["jensen_alpha"] = jensen_alpha;
    
    // Drawdown analysis
    j["avg_drawdown"] = avg_drawdown;
    j["avg_drawdown_duration"] = avg_drawdown_duration;
    j["recovery_factor"] = recovery_factor;
    j["ulcer_index"] = ulcer_index;
    
    // Trade analysis
    j["avg_win"] = avg_win;
    j["avg_loss"] = avg_loss;
    j["largest_win"] = largest_win;
    j["largest_loss"] = largest_loss;
    j["win_loss_ratio"] = win_loss_ratio;
    j["expectancy"] = expectancy;
    j["kelly_criterion"] = kelly_criterion;
    
    // Volatility analysis
    j["downside_deviation"] = downside_deviation;
    j["upside_deviation"] = upside_deviation;
    j["var_95"] = var_95;
    j["cvar_95"] = cvar_95;
    
    // Advanced ratios
    j["gain_to_pain_ratio"] = gain_to_pain_ratio;
    j["profit_factor_ratio"] = profit_factor_ratio;
    j["risk_reward_ratio"] = risk_reward_ratio;
    j["payoff_ratio"] = payoff_ratio;
    
    // Machine learning metrics
    j["strategy_consistency"] = strategy_consistency;
    j["market_correlation"] = market_correlation;
    j["regime_adaptation"] = regime_adaptation;
    
    return j;
}

} // namespace backtesting
