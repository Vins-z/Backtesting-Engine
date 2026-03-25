#include "strategy/code_generator.h"
#include <sstream>
#include <algorithm>
#include <regex>

namespace backtesting {

CodeGenerator::CodeGenerator() {
    initialize_template();
}

void CodeGenerator::initialize_template() {
    strategy_template_ = R"(
#include "strategy/strategy.h"
#include "data/technical_indicators.h"
#include <vector>
#include <algorithm>

namespace backtesting {

class {CLASS_NAME} : public Strategy {
private:
    std::vector<OHLC> history_;
    bool is_ready_;
    
    // Indicator calculations
    {INDICATORS_CODE}
    
public:
    {CLASS_NAME}() : is_ready_(false) {}
    
    void initialize(const std::vector<OHLC>& historical_data) override {
        history_ = historical_data;
        is_ready_ = !history_.empty();
    }
    
    void update(const OHLC& new_data) override {
        history_.push_back(new_data);
        if (history_.size() > 1000) {
            history_.erase(history_.begin());
        }
    }
    
    Signal generate_signal(
        const std::string& symbol,
        const OHLC& current_data,
        const PortfolioManager& portfolio
    ) override {
        if (!is_ready_ || history_.size() < 50) {
            return Signal::NONE;
        }
        
        update(current_data);
        calculate_indicators();
        
        // Check entry condition
        {ENTRY_CONDITION_CODE}
        
        // Check exit condition
        {EXIT_CONDITION_CODE}
        
        {PREV_UPDATE_CODE}
        return Signal::NONE;
    }
    
    std::unordered_map<std::string, double> get_parameters() const override {
        return {};
    }
    
    void reset() override {
        history_.clear();
        is_ready_ = false;
    }
    
    bool is_ready() const override {
        return is_ready_;
    }
};

} // namespace backtesting
)";
}

std::string CodeGenerator::generate_class_name(const std::string& strategy_name) {
    std::string class_name = strategy_name;
    
    // Remove special characters
    std::replace(class_name.begin(), class_name.end(), ' ', '_');
    std::replace(class_name.begin(), class_name.end(), '-', '_');
    
    // Capitalize first letter of each word
    bool capitalize = true;
    for (char& c : class_name) {
        if (std::isalnum(c)) {
            if (capitalize) {
                c = std::toupper(c);
                capitalize = false;
            }
        } else {
            capitalize = true;
        }
    }
    
    // Ensure it starts with a letter
    if (!std::isalpha(class_name[0])) {
        class_name = "Strategy_" + class_name;
    }
    
    return class_name + "Strategy";
}

bool CodeGenerator::validate_dsl(const nlohmann::json& dsl, std::string& error_message) {
    if (!dsl.contains("indicators") || !dsl["indicators"].is_array()) {
        error_message = "DSL must contain 'indicators' array";
        return false;
    }
    
    if (!dsl.contains("entry_condition")) {
        error_message = "DSL must contain 'entry_condition'";
        return false;
    }
    
    if (!dsl.contains("exit_condition")) {
        error_message = "DSL must contain 'exit_condition'";
        return false;
    }
    
    return true;
}

std::string CodeGenerator::generate_strategy_code(const nlohmann::json& dsl) {
    std::string error_message;
    if (!validate_dsl(dsl, error_message)) {
        throw std::invalid_argument("Invalid DSL: " + error_message);
    }
    
    std::string class_name = generate_class_name(dsl.value("name", "Custom"));
    std::string indicators_code = generate_indicators_code(dsl["indicators"]);
    std::string prev_update_code = generate_prev_update_code(dsl["indicators"]);
    std::string entry_code = generate_entry_condition_code(dsl["entry_condition"]);
    std::string exit_code = generate_exit_condition_code(dsl["exit_condition"]);
    
    // Substitute placeholders into the strategy code template.
    std::string code = strategy_template_;
    
    // Replace {CLASS_NAME}
    size_t pos = code.find("{CLASS_NAME}");
    while (pos != std::string::npos) {
        code.replace(pos, 13, class_name);
        pos = code.find("{CLASS_NAME}", pos + class_name.length());
    }
    
    // Replace {INDICATORS_CODE}
    pos = code.find("{INDICATORS_CODE}");
    if (pos != std::string::npos) {
        code.replace(pos, 17, indicators_code);
    }
    
    // Replace {ENTRY_CONDITION_CODE}
    pos = code.find("{ENTRY_CONDITION_CODE}");
    if (pos != std::string::npos) {
        code.replace(pos, 23, entry_code);
    }
    
    // Replace {EXIT_CONDITION_CODE}
    pos = code.find("{EXIT_CONDITION_CODE}");
    if (pos != std::string::npos) {
        code.replace(pos, 22, exit_code);
    }
    
    // Replace {PREV_UPDATE_CODE}
    pos = code.find("{PREV_UPDATE_CODE}");
    if (pos != std::string::npos) {
        code.replace(pos, 18, prev_update_code);
    }
    
    return code;
}

std::string CodeGenerator::sanitize_var_name(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (std::isalnum(c) || c == '_') {
            result += c;
        } else if (c == '-' || c == ' ' || c == '.' || c == '(' || c == ')') {
            result += '_';
        }
    }
    if (result.empty() || std::isdigit(result[0])) {
        result = "v_" + result;
    }
    return result;
}

std::string CodeGenerator::generate_prev_update_code(const nlohmann::json& indicators) {
    std::ostringstream code;
    code << "        prev_close_ = current_data.close;\n";
    if (indicators.is_array()) {
        for (const auto& indicator : indicators) {
            std::string type = indicator.value("type", std::string(""));
            std::string lower_type = type;
            std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), ::tolower);
            if (lower_type == "number") continue;  // constants don't need prev
            std::string name = sanitize_var_name(indicator.value("name", indicator.value("type", std::string("ind"))));
            code << "        prev_" << name << " = " << name << ";\n";
        }
    }
    return code.str();
}

std::string CodeGenerator::generate_indicators_code(const nlohmann::json& indicators) {
    std::ostringstream code;
    
    if (!indicators.is_array()) {
        return "";
    }
    
    code << "    double prev_close_ = 0.0;\n";
    for (const auto& indicator : indicators) {
        std::string type = indicator.value("type", "");
        std::string raw_name = indicator.value("name", type);
        std::string name = sanitize_var_name(raw_name);
        std::string lower_type = type;
        std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), ::tolower);
        bool is_constant = (lower_type == "number");
        
        code << "    // " << raw_name << (is_constant ? " (constant)" : " indicator") << "\n";
        if (is_constant) {
            double val = indicator.value("params", nlohmann::json::object()).value("value", 0.0);
            code << "    double " << name << " = " << std::to_string(val) << ";\n";
        } else {
            code << "    double " << name << " = 0.0;\n";
            code << "    double prev_" << name << " = 0.0;\n";
        }
    }
    
    code << "\n    // Calculate indicators (uses close prices from OHLC history)\n";
    code << "    void calculate_indicators() {\n";
    code << "        auto closes = Strategy::extract_close_prices(history_);\n";
    code << "        if (closes.size() < 50) return;\n";
    code << "\n";
    
    for (const auto& indicator : indicators) {
        std::string type = indicator.value("type", "");
        std::string lower_type = type;
        std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), ::tolower);
        if (lower_type == "number") continue;  // constants are set in member init
        std::string name = sanitize_var_name(indicator.value("name", type));
        auto params = indicator.value("params", nlohmann::json::object());
        
        std::string expr = indicator_to_cpp(type, params, name);
        code << "        " << expr << "\n";
    }
    
    code << "    }\n";
    
    return code.str();
}

std::string CodeGenerator::indicator_to_cpp(const std::string& indicator_type, const nlohmann::json& params, const std::string& var_name) {
    std::string lower_type = indicator_type;
    std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), ::tolower);
    
    // Normalize param names: Visual Builder uses length/period, fastLength, mult, etc.
    auto get_period = [&params](int default_val) {
        if (params.contains("period") && params["period"].is_number()) return static_cast<int>(params["period"].get<double>());
        if (params.contains("length") && params["length"].is_number()) return static_cast<int>(params["length"].get<double>());
        return default_val;
    };
    
    if (lower_type == "sma") {
        int period = get_period(20);
        return var_name + " = Strategy::calculate_sma(closes, " + std::to_string(period) + ");";
    } else if (lower_type == "ema") {
        int period = get_period(20);
        return var_name + " = Strategy::calculate_ema(closes, " + std::to_string(period) + ");";
    } else if (lower_type == "rsi") {
        int period = get_period(14);
        return var_name + " = Strategy::calculate_rsi(closes, " + std::to_string(period) + ");";
    } else if (lower_type == "macd") {
        int fast = params.value("fast_period", params.value("fastLength", 12));
        int slow = params.value("slow_period", params.value("slowLength", 26));
        int sig = params.value("signal_period", params.value("signalLength", 9));
        std::string sig_var = var_name + "_sig";
        std::string hist_var = var_name + "_hist";
        return "double " + sig_var + " = 0, " + hist_var + " = 0;\n        " +
               var_name + " = Strategy::calculate_macd(closes, " + std::to_string(fast) + ", " +
               std::to_string(slow) + ", " + std::to_string(sig) + ", " + sig_var + ", " + hist_var + ");";
    } else if (lower_type == "bollinger" || lower_type == "bb" || lower_type == "bollingerbands") {
        int period = get_period(20);
        double std_dev = params.value("std_dev", params.value("mult", 2.0));
        std::string upper_var = var_name + "_upper";
        std::string lower_var = var_name + "_lower";
        return "double " + upper_var + " = 0, " + lower_var + " = 0;\n        " +
               var_name + " = Strategy::calculate_bollinger_bands(closes, " + std::to_string(period) + ", " +
               std::to_string(std_dev) + ", " + upper_var + ", " + lower_var + ");";
    } else if (lower_type == "atr") {
        int period = get_period(14);
        return var_name + " = Strategy::calculate_atr(history_, " + std::to_string(period) + ");";
    } else if (lower_type == "number") {
        double val = params.value("value", 0.0);
        return var_name + " = " + std::to_string(val) + ";  // constant";
    }
    
    return var_name + " = 0.0;  // Unsupported indicator: " + indicator_type;
}

std::string CodeGenerator::generate_entry_condition_code(const nlohmann::json& entry_condition) {
    std::ostringstream code;
    
    code << "        // Entry condition\n";
    code << "        if (" << condition_to_cpp(entry_condition) << ") {\n";
    code << "            return Signal::BUY;\n";
    code << "        }\n";
    
    return code.str();
}

std::string CodeGenerator::generate_exit_condition_code(const nlohmann::json& exit_condition) {
    std::ostringstream code;
    
    code << "        // Exit condition\n";
    code << "        if (" << condition_to_cpp(exit_condition) << ") {\n";
    code << "            return Signal::SELL;\n";
    code << "        }\n";
    
    return code.str();
}

static std::string to_cpp_expr(const std::string& name, bool prev) {
    std::string s = name;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "close") {
        return prev ? "prev_close_" : "current_data.close";
    }
    std::string sanitized;
    for (char c : name) {
        if (std::isalnum(c) || c == '_') sanitized += c;
        else if (c == '-' || c == ' ' || c == '.' || c == '(' || c == ')') sanitized += '_';
    }
    if (sanitized.empty() || (sanitized.size() && std::isdigit(sanitized[0]))) sanitized = "v_" + sanitized;
    return prev ? ("prev_" + sanitized) : sanitized;
}

std::string CodeGenerator::condition_to_cpp(const nlohmann::json& condition) {
    if (condition.is_string()) {
        return condition.get<std::string>();
    }
    
    if (condition.is_object()) {
        std::string type = condition.value("type", "");
        
        if (type == "comparison") {
            std::string left_raw = condition.value("left", std::string(""));
            std::string op = condition.value("operator", std::string(""));
            auto right = condition["right"];
            
            std::string left_expr = to_cpp_expr(left_raw, false);
            std::string prev_left = to_cpp_expr(left_raw, true);
            
            std::string right_str;
            std::string prev_right;
            if (right.is_number()) {
                double v = right.get<double>();
                right_str = std::to_string(v);
                prev_right = right_str;  // constant has no prev
            } else if (right.is_string()) {
                std::string r = right.get<std::string>();
                right_str = to_cpp_expr(r, false);
                prev_right = to_cpp_expr(r, true);
            } else {
                right_str = "0";
                prev_right = "0";
            }
            
            if (op == "crossAbove") {
                return "(" + left_expr + " > " + right_str + " && " + prev_left + " <= " + prev_right + ")";
            } else if (op == "crossBelow") {
                return "(" + left_expr + " < " + right_str + " && " + prev_left + " >= " + prev_right + ")";
            }
            return "(" + left_expr + " " + op + " " + right_str + ")";
        } else if (type == "and") {
            std::string left = condition_to_cpp(condition["left"]);
            std::string right = condition_to_cpp(condition["right"]);
            return "(" + left + " && " + right + ")";
        } else if (type == "or") {
            std::string left = condition_to_cpp(condition["left"]);
            std::string right = condition_to_cpp(condition["right"]);
            return "(" + left + " || " + right + ")";
        }
    }
    
    return "false";
}

std::string CodeGenerator::get_strategy_template() const {
    return strategy_template_;
}

} // namespace backtesting

