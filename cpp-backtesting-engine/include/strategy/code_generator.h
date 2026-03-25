#pragma once

#include "common/types.h"
#include "strategy/strategy.h"
#include <string>
#include <memory>
#include <nlohmann/json.hpp>

namespace backtesting {

// Code generator for creating C++ strategy classes from DSL
class CodeGenerator {
public:
    CodeGenerator();
    ~CodeGenerator() = default;
    
    // Generate C++ strategy code from JSON DSL
    std::string generate_strategy_code(const nlohmann::json& dsl);
    
    // Generate strategy class name from DSL name
    std::string generate_class_name(const std::string& strategy_name);
    
    // Validate DSL structure
    bool validate_dsl(const nlohmann::json& dsl, std::string& error_message);
    
    // Get strategy template
    std::string get_strategy_template() const;
    
private:
    // Helper methods for code generation
    std::string generate_indicators_code(const nlohmann::json& indicators);
    std::string generate_prev_update_code(const nlohmann::json& indicators);
    std::string sanitize_var_name(const std::string& name);
    std::string generate_entry_condition_code(const nlohmann::json& entry_condition);
    std::string generate_exit_condition_code(const nlohmann::json& exit_condition);
    std::string generate_risk_management_code(const nlohmann::json& risk_params);
    
    // Convert indicator type to C++ code (returns full assignment statement, uses closes for price data)
    std::string indicator_to_cpp(const std::string& indicator_type, const nlohmann::json& params, const std::string& var_name);
    
    // Convert condition to C++ expression
    std::string condition_to_cpp(const nlohmann::json& condition);
    
    // Strategy template
    std::string strategy_template_;
    
    void initialize_template();
};

} // namespace backtesting

