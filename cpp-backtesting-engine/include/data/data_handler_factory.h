#pragma once

#include "data_handler.h"
#include "alpha_vantage_handler.h"
#include <memory>
#include <string>

namespace backtesting {

class DataHandlerFactory {
public:
    static std::unique_ptr<DataHandler> create(
        const std::string& source_type,
        const std::string& data_path = "",
        const std::string& api_key = ""
    );
    
    static std::unique_ptr<DataHandler> create(
        const std::string& source_type,
        const std::string& data_path,
        const std::string& api_key,
        const std::string& data_interval
    );
    
    static std::vector<std::string> get_available_sources();
    
private:
    static std::unique_ptr<DataHandler> create_alpha_vantage_handler(const std::string& api_key);
    static std::unique_ptr<DataHandler> create_database_handler(const std::string& connection_string);
};

} // namespace backtesting 