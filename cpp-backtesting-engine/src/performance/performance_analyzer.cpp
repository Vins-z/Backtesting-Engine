#include "performance/performance_analyzer.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <limits>

namespace backtesting {

namespace {
struct Lot {
    Quantity quantity = 0.0;
    Price cost_per_share = 0.0; // includes buy commission allocated per share
};

static std::vector<Price> compute_fifo_trade_pnls(const std::vector<Fill>& trades) {
    // Compute realized P&L from fills using FIFO lots.
    // This handles partial sells and multiple entries/exits per symbol.
    std::unordered_map<std::string, std::deque<Lot>> lots_by_symbol;
    std::vector<Price> pnls;

    for (const auto& f : trades) {
        if (f.quantity <= 0 || f.price <= 0.0) continue;

        if (f.side == OrderSide::BUY) {
            Price cost_per_share = f.price;
            cost_per_share += (f.commission / f.quantity);
            lots_by_symbol[f.symbol].push_back(Lot{f.quantity, cost_per_share});
            continue;
        }

        if (f.side == OrderSide::SELL) {
            Quantity remaining = f.quantity;
            Price proceeds_per_share = f.price - (f.commission / f.quantity);

            auto& lots = lots_by_symbol[f.symbol];
            while (remaining > 0 && !lots.empty()) {
                Lot& lot = lots.front();
                Quantity matched = std::min(remaining, lot.quantity);
                pnls.push_back((proceeds_per_share - lot.cost_per_share) * matched);
                lot.quantity -= matched;
                remaining -= matched;
                if (lot.quantity <= 0) lots.pop_front();
            }
        }
    }

    return pnls;
}
} // namespace

void PerformanceAnalyzer::record_trade(const Fill& trade) {
    recorded_trades_.push_back(trade);
}

PerformanceMetrics BasicPerformanceAnalyzer::calculate_metrics(
    const std::vector<std::pair<Timestamp, Price>>& equity_curve,
    const std::vector<Fill>& trades,
    [[maybe_unused]] Price initial_capital,
    Price risk_free_rate
) const {
    PerformanceMetrics metrics;
    
    if (equity_curve.empty()) {
        return metrics;
    }
    
    // Ensure we have enough equity-curve points for return/drawdown calculations.
    if (equity_curve.size() < 2) {
        // Need at least 2 points for return calculation
        return metrics;
    }
    
    // Calculate return metrics from actual equity curve (not shortcuts)
    Price initial_value = equity_curve.front().second;
    Price final_value = equity_curve.back().second;
    
    if (initial_value <= 0.0) {
        // Invalid initial capital
        return metrics;
    }
    
    metrics.total_return = calculate_total_return(initial_value, final_value);
    metrics.annualized_return = calculate_annualized_return(equity_curve);
    
    // Compute return series from the full equity curve.
    auto returns = calculate_returns(equity_curve);
    if (!returns.empty()) {
        metrics.sharpe_ratio = calculate_sharpe_ratio(returns, risk_free_rate);
        metrics.volatility = calculate_volatility(returns);
    }
    
    // Compute max drawdown from the equity curve's peak-to-trough path.
    metrics.max_drawdown = calculate_max_drawdown(equity_curve);
    
    // Calculate trade metrics from actual fills (not approximations)
    // All metrics are derived from real trades recorded in trade history
    metrics.total_trades = BasicPerformanceAnalyzer::count_total_trades(trades);
    metrics.winning_trades = BasicPerformanceAnalyzer::count_winning_trades(trades);
    metrics.losing_trades = BasicPerformanceAnalyzer::count_losing_trades(trades);
    
    // Win rate and profit factor calculated from actual trade P&L
    if (metrics.total_trades > 0) {
        metrics.win_rate = BasicPerformanceAnalyzer::calculate_win_rate(trades);
        metrics.profit_factor = BasicPerformanceAnalyzer::calculate_profit_factor(trades);
    } else {
        metrics.win_rate = 0.0;
        metrics.profit_factor = 0.0;
    }

    return metrics;
}

Price PerformanceAnalyzer::calculate_total_return(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const {
    if (equity_curve.size() < 2) return 0.0;
    
    Price initial_value = equity_curve.front().second;
    Price final_value = equity_curve.back().second;
    
    return (final_value - initial_value) / initial_value;
}

Price PerformanceAnalyzer::calculate_annualized_return(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const {
    if (equity_curve.size() < 2) return 0.0;
    
    Price total_return = calculate_total_return(equity_curve);
    
    // Calculate time span in years
    auto start_time = equity_curve.front().first;
    auto end_time = equity_curve.back().first;
    auto duration = std::chrono::duration_cast<std::chrono::days>(end_time - start_time);
    double years = duration.count() / 365.25;
    
    if (years <= 0) return total_return;
    
    return std::pow(1.0 + total_return, 1.0 / years) - 1.0;
}

Price BasicPerformanceAnalyzer::calculate_sharpe_ratio(const std::vector<Price>& returns, Price risk_free_rate) const {
    if (returns.empty()) return 0.0;
    
    // Calculate mean return
    Price mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    
    // Calculate volatility
    Price volatility = calculate_volatility(returns);
    
    if (volatility == 0.0) return 0.0;
    
    // Annualized Sharpe ratio
    Price excess_return = mean_return - risk_free_rate / 252.0; // Daily risk-free rate
    return (excess_return * std::sqrt(252.0)) / volatility;
}

Price PerformanceAnalyzer::calculate_max_drawdown(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const {
    if (equity_curve.empty()) return 0.0;
    
    Price max_drawdown = 0.0;
    Price peak = equity_curve[0].second;
    
    for (const auto& point : equity_curve) {
        Price value = point.second;
        if (value > peak) {
            peak = value;
        }
        
        Price drawdown = (peak - value) / peak;
        max_drawdown = std::max(max_drawdown, drawdown);
    }
    
    return -max_drawdown; // Return as negative value
}

Price BasicPerformanceAnalyzer::calculate_volatility(const std::vector<Price>& returns) const {
    if (returns.size() < 2) return 0.0;
    
    // Calculate mean return
    Price mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    
    // Calculate variance
    Price variance = 0.0;
    for (Price ret : returns) {
        variance += std::pow(ret - mean_return, 2);
    }
    variance /= (returns.size() - 1);
    
    // Return annualized volatility
    return std::sqrt(variance * 252.0);
}

Price PerformanceAnalyzer::calculate_win_rate(const std::vector<Fill>& trades) const {
    auto pnls = compute_fifo_trade_pnls(trades);
    if (pnls.empty()) return 0.0;

    int winning = 0;
    for (auto p : pnls) if (p > 0) winning++;
    return static_cast<double>(winning) / pnls.size();
}

Price PerformanceAnalyzer::calculate_profit_factor(const std::vector<Fill>& trades) const {
    auto pnls = compute_fifo_trade_pnls(trades);
    if (pnls.empty()) return 0.0;

    Price gross_profit = 0.0;
    Price gross_loss = 0.0;
    for (Price pnl : pnls) {
        if (pnl > 0) gross_profit += pnl;
        else gross_loss += std::abs(pnl);
    }
    return gross_loss > 0 ? gross_profit / gross_loss : (gross_profit > 0 ? std::numeric_limits<Price>::infinity() : 0.0);
}

int PerformanceAnalyzer::count_total_trades(const std::vector<Fill>& trades) const {
    return static_cast<int>(compute_fifo_trade_pnls(trades).size());
}

int PerformanceAnalyzer::count_winning_trades(const std::vector<Fill>& trades) const {
    auto pnls = compute_fifo_trade_pnls(trades);
    return static_cast<int>(std::count_if(pnls.begin(), pnls.end(), [](Price p) { return p > 0; }));
}

int PerformanceAnalyzer::count_losing_trades(const std::vector<Fill>& trades) const {
    auto pnls = compute_fifo_trade_pnls(trades);
    return static_cast<int>(std::count_if(pnls.begin(), pnls.end(), [](Price p) { return p < 0; }));
}

void PerformanceAnalyzer::reset() {
    recorded_trades_.clear();
}

std::vector<Price> BasicPerformanceAnalyzer::calculate_returns(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const {
    std::vector<Price> returns;
    
    for (size_t i = 1; i < equity_curve.size(); ++i) {
        Price prev_value = equity_curve[i-1].second;
        Price curr_value = equity_curve[i].second;
        
        if (prev_value > 0) {
            returns.push_back((curr_value - prev_value) / prev_value);
        }
    }
    
    return returns;
}

std::vector<Price> BasicPerformanceAnalyzer::group_trades_by_pnl(const std::vector<Fill>& trades) const {
    return compute_fifo_trade_pnls(trades);
}

// Add missing method implementations for BasicPerformanceAnalyzer
std::string BasicPerformanceAnalyzer::generate_report(const PerformanceMetrics& metrics) {
    std::ostringstream report;
    report << "=== PERFORMANCE REPORT ===\n";
    report << "Total Return: " << std::fixed << std::setprecision(2) << (metrics.total_return * 100) << "%\n";
    report << "Annualized Return: " << std::fixed << std::setprecision(2) << (metrics.annualized_return * 100) << "%\n";
    report << "Sharpe Ratio: " << std::fixed << std::setprecision(3) << metrics.sharpe_ratio << "\n";
    report << "Max Drawdown: " << std::fixed << std::setprecision(2) << (metrics.max_drawdown * 100) << "%\n";
    report << "Volatility: " << std::fixed << std::setprecision(2) << (metrics.volatility * 100) << "%\n";
    report << "Win Rate: " << std::fixed << std::setprecision(2) << (metrics.win_rate * 100) << "%\n";
    report << "Profit Factor: " << std::fixed << std::setprecision(3) << metrics.profit_factor << "\n";
    report << "Total Trades: " << metrics.total_trades << "\n";
    report << "Winning Trades: " << metrics.winning_trades << "\n";
    report << "Losing Trades: " << metrics.losing_trades << "\n";
    return report.str();
}

nlohmann::json BasicPerformanceAnalyzer::export_to_json(const PerformanceMetrics& metrics) {
    nlohmann::json j;
    j["total_return"] = metrics.total_return;
    j["annualized_return"] = metrics.annualized_return;
    j["sharpe_ratio"] = metrics.sharpe_ratio;
    j["max_drawdown"] = metrics.max_drawdown;
    j["volatility"] = metrics.volatility;
    j["win_rate"] = metrics.win_rate;
    j["profit_factor"] = metrics.profit_factor;
    j["total_trades"] = metrics.total_trades;
    j["winning_trades"] = metrics.winning_trades;
    j["losing_trades"] = metrics.losing_trades;
    return j;
}

// Add missing method implementations
Price BasicPerformanceAnalyzer::calculate_win_rate(const std::vector<Fill>& trades) const {
    if (trades.empty()) return 0.0;
    
    // Derive win rate from round-trip trade P&L.
    // Match buys and sells to calculate round-trip P&L
    auto trade_pnls = group_trades_by_pnl(trades);
    
    if (trade_pnls.empty()) return 0.0;
    
    int winning_trades = 0;
    for (Price pnl : trade_pnls) {
        if (pnl > 0) {
            winning_trades++;
        }
    }
    
    return static_cast<double>(winning_trades) / trade_pnls.size();
}

Price BasicPerformanceAnalyzer::calculate_profit_factor(const std::vector<Fill>& trades) const {
    // Compute profit factor from round-trip trade P&L.
    auto trade_pnls = group_trades_by_pnl(trades);
    
    if (trade_pnls.empty()) return 0.0;
    
    Price gross_profit = 0.0;
    Price gross_loss = 0.0;
    
    for (Price pnl : trade_pnls) {
        if (pnl > 0) {
            gross_profit += pnl;
        } else {
            gross_loss += std::abs(pnl);
        }
    }
    
    return gross_loss > 0 ? gross_profit / gross_loss : (gross_profit > 0 ? std::numeric_limits<Price>::infinity() : 0.0);
}

int BasicPerformanceAnalyzer::count_total_trades(const std::vector<Fill>& trades) const {
    // Count round-trip trades by matching buys and sells.
    // This ensures accurate trade counting for performance metrics
    auto trade_pnls = group_trades_by_pnl(trades);
    return static_cast<int>(trade_pnls.size());
}

int BasicPerformanceAnalyzer::count_winning_trades(const std::vector<Fill>& trades) const {
    auto trade_pnls = group_trades_by_pnl(trades);
    
    return std::count_if(trade_pnls.begin(), trade_pnls.end(), [](Price pnl) {
        return pnl > 0;
    });
}

int BasicPerformanceAnalyzer::count_losing_trades(const std::vector<Fill>& trades) const {
    auto trade_pnls = group_trades_by_pnl(trades);
    
    return std::count_if(trade_pnls.begin(), trade_pnls.end(), [](Price pnl) {
        return pnl < 0;
    });
}

Price BasicPerformanceAnalyzer::calculate_max_drawdown(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const {
    if (equity_curve.empty()) return 0.0;
    
    Price max_drawdown = 0.0;
    Price peak = equity_curve[0].second;
    
    for (const auto& point : equity_curve) {
        Price value = point.second;
        if (value > peak) {
            peak = value;
        }
        
        Price drawdown = (peak - value) / peak;
        max_drawdown = std::max(max_drawdown, drawdown);
    }
    
    return -max_drawdown; // Return as negative value
}

Price BasicPerformanceAnalyzer::calculate_annualized_return(const std::vector<std::pair<Timestamp, Price>>& equity_curve) const {
    if (equity_curve.size() < 2) return 0.0;
    
    Price initial_value = equity_curve.front().second;
    Price final_value = equity_curve.back().second;
    Price total_return = (final_value - initial_value) / initial_value;
    
    // Calculate time span in years
    auto start_time = equity_curve.front().first;
    auto end_time = equity_curve.back().first;
    auto duration = std::chrono::duration_cast<std::chrono::days>(end_time - start_time);
    double years = duration.count() / 365.25;
    
    if (years <= 0) return total_return;
    
    return std::pow(1.0 + total_return, 1.0 / years) - 1.0;
}

Price BasicPerformanceAnalyzer::calculate_total_return(Price initial_capital, Price final_capital) const {
    if (initial_capital <= 0) return 0.0;
    return (final_capital - initial_capital) / initial_capital;
}

} // namespace backtesting 