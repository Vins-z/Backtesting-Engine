#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace backtesting {

struct IndicatorConfig {
    std::string type;
    std::unordered_map<std::string, double> params;
};

struct RiskParams {
    double risk_per_trade_pct = 1.0;
    double max_daily_loss_pct = 5.0;
    double max_portfolio_risk_pct = 10.0;
    bool enable_breakeven = false;
    double breakeven_trigger_R = 1.0;
    bool enable_trailing_stop = false;
    std::string trailing_type;   // "PERCENT" or "ATR"
    double trailing_value = 0.0; // percentage (0-1) or ATR multiples
};

struct StrategyLegSchema {
    std::string symbol;
    std::string segment;   // "EQUITY", "OPTIONS", "FUTURES"
    std::string direction; // "BUY", "SELL"
    double quantity = 0.0;
    double stop_loss_pct = 0.0;
    double take_profit_pct = 0.0;
    std::string strike;
};

struct StrategyDefinition {
    std::string name;
    std::string description;
    std::vector<StrategyLegSchema> legs;
    std::vector<IndicatorConfig> indicators;
    RiskParams risk;
};

inline void from_json(const nlohmann::json& j, IndicatorConfig& ic) {
    ic.type = j.value("type", "");
    ic.params.clear();
    if (j.contains("params") && j["params"].is_object()) {
        for (auto it = j["params"].begin(); it != j["params"].end(); ++it) {
            if (it.value().is_number()) {
                ic.params[it.key()] = it.value().get<double>();
            }
        }
    }
}

inline nlohmann::json to_json(const IndicatorConfig& ic) {
    nlohmann::json j;
    j["type"] = ic.type;
    j["params"] = ic.params;
    return j;
}

inline void from_json(const nlohmann::json& j, RiskParams& r) {
    r.risk_per_trade_pct = j.value("risk_per_trade_pct", 1.0);
    r.max_daily_loss_pct = j.value("max_daily_loss_pct", 5.0);
    r.max_portfolio_risk_pct = j.value("max_portfolio_risk_pct", 10.0);
    r.enable_breakeven = j.value("enable_breakeven", false);
    r.breakeven_trigger_R = j.value("breakeven_trigger_R", 1.0);
    r.enable_trailing_stop = j.value("enable_trailing_stop", false);
    r.trailing_type = j.value("trailing_type", "");
    r.trailing_value = j.value("trailing_value", 0.0);
}

inline nlohmann::json to_json(const RiskParams& r) {
    nlohmann::json j;
    j["risk_per_trade_pct"] = r.risk_per_trade_pct;
    j["max_daily_loss_pct"] = r.max_daily_loss_pct;
    j["max_portfolio_risk_pct"] = r.max_portfolio_risk_pct;
    j["enable_breakeven"] = r.enable_breakeven;
    j["breakeven_trigger_R"] = r.breakeven_trigger_R;
    j["enable_trailing_stop"] = r.enable_trailing_stop;
    j["trailing_type"] = r.trailing_type;
    j["trailing_value"] = r.trailing_value;
    return j;
}

inline void from_json(const nlohmann::json& j, StrategyLegSchema& leg) {
    leg.symbol = j.value("symbol", "");
    leg.segment = j.value("segment", "EQUITY");
    leg.direction = j.value("direction", "BUY");
    leg.quantity = j.value("quantity", 0.0);
    leg.stop_loss_pct = j.value("stop_loss_pct", 0.0);
    leg.take_profit_pct = j.value("take_profit_pct", 0.0);
    leg.strike = j.value("strike", "");
}

inline nlohmann::json to_json(const StrategyLegSchema& leg) {
    nlohmann::json j;
    j["symbol"] = leg.symbol;
    j["segment"] = leg.segment;
    j["direction"] = leg.direction;
    j["quantity"] = leg.quantity;
    j["stop_loss_pct"] = leg.stop_loss_pct;
    j["take_profit_pct"] = leg.take_profit_pct;
    if (!leg.strike.empty()) {
        j["strike"] = leg.strike;
    }
    return j;
}

inline void from_json(const nlohmann::json& j, StrategyDefinition& def) {
    def.name = j.value("name", "Multi-leg Strategy");
    def.description = j.value("description", "");
    def.legs.clear();
    if (j.contains("legs") && j["legs"].is_array()) {
        for (const auto& jl : j["legs"]) {
            StrategyLegSchema leg{};
            from_json(jl, leg);
            def.legs.push_back(leg);
        }
    }
    def.indicators.clear();
    if (j.contains("indicators") && j["indicators"].is_array()) {
        for (const auto& ji : j["indicators"]) {
            IndicatorConfig ic{};
            from_json(ji, ic);
            def.indicators.push_back(ic);
        }
    }
    if (j.contains("risk") && j["risk"].is_object()) {
        from_json(j["risk"], def.risk);
    } else {
        def.risk = RiskParams{};
    }
}

inline nlohmann::json to_json(const StrategyDefinition& def) {
    nlohmann::json j;
    j["name"] = def.name;
    j["description"] = def.description;
    j["legs"] = nlohmann::json::array();
    for (const auto& leg : def.legs) {
        j["legs"].push_back(to_json(leg));
    }
    j["indicators"] = nlohmann::json::array();
    for (const auto& ic : def.indicators) {
        j["indicators"].push_back(to_json(ic));
    }
    j["risk"] = to_json(def.risk);
    return j;
}

} // namespace backtesting


