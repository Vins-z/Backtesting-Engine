#include "c_api/backtest_c.h"

#include "engine/backtest_engine.h"
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

struct BacktestEngineHandle {
    std::unique_ptr<backtesting::BacktestEngine> engine;
};

namespace {

static char* dup_cstr(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

static backtesting::BacktestConfig config_from_json(const nlohmann::json& j) {
    using backtesting::BacktestConfig;

    BacktestConfig cfg{};

    cfg.name = j.value("name", std::string("library_backtest"));
    cfg.description = j.value("description", std::string(""));

    if (j.contains("symbols") && j["symbols"].is_array()) {
        for (const auto& s : j["symbols"]) {
            if (s.is_string()) cfg.symbols.push_back(s.get<std::string>());
        }
    } else if (j.contains("symbol") && j["symbol"].is_string()) {
        cfg.symbols = {j["symbol"].get<std::string>()};
    }

    cfg.start_date = j.value("start_date", std::string(""));
    cfg.end_date = j.value("end_date", std::string(""));
    cfg.initial_capital = j.value("initial_capital", 100000.0);

    // Match engine config naming.
    cfg.commission_rate = j.value("commission_rate", j.value("commission", 0.001));
    cfg.slippage_rate = j.value("slippage_rate", j.value("slippage", 0.001));

    cfg.data_source = j.value("data_source", std::string("csv"));
    cfg.data_path = j.value("data_path", std::string(""));
    cfg.data_interval = j.value("data_interval", std::string("1d"));
    cfg.api_key = j.value("api_key", std::string(""));

    cfg.strategy_name = j.value("strategy_name", std::string("moving_average"));
    if (j.contains("strategy_params") && j["strategy_params"].is_object()) {
        for (auto it = j["strategy_params"].begin(); it != j["strategy_params"].end(); ++it) {
            if (it.value().is_number()) {
                cfg.strategy_params[it.key()] = it.value().get<double>();
            }
        }
    }

    // Optional raw strategy JSON passthrough.
    if (j.contains("strategy_definition_json") && j["strategy_definition_json"].is_object()) {
        cfg.strategy_definition_json = j["strategy_definition_json"];
    }

    return cfg;
}

} // namespace

extern "C" {

BacktestEngineHandle* bt_create_from_config_json(const char* config_json) {
    if (!config_json) return nullptr;

    try {
        nlohmann::json j = nlohmann::json::parse(config_json);
        auto cfg = config_from_json(j);

        auto h = std::make_unique<BacktestEngineHandle>();
        h->engine = backtesting::BacktestEngine::create_from_config(cfg);
        return h.release();
    } catch (...) {
        return nullptr;
    }
}

char* bt_run_to_result_json(BacktestEngineHandle* h) {
    if (!h || !h->engine) return nullptr;

    try {
        backtesting::BacktestResult r = h->engine->run_backtest();
        return dup_cstr(r.to_json().dump());
    } catch (...) {
        return nullptr;
    }
}

void bt_destroy(BacktestEngineHandle* h) {
    delete h;
}

void bt_free_string(char* s) {
    std::free(s);
}

} // extern "C"


