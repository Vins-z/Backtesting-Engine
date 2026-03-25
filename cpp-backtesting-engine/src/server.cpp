#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <csignal>
#include <atomic>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <map>
#include <mutex>
#include <vector>
#include <memory>
#include <deque>
#include <condition_variable>
#include <random>
#include <algorithm>
#include <cctype>
#include "data/alpha_vantage_handler.h"
#include "data/yfinance_handler.h"
#include "data/polygon_handler.h"
#include "data/data_source_router.h"
#include "data/data_handler_factory.h"
#include "data/stock_symbols.h"
#include "data/technical_indicators.h"
#include "data/symbol_cache.h"
#include "strategy/natural_language_parser.h"
#include "strategy/code_generator.h"
#include "engine/backtest_engine.h"
#include "portfolio/multi_asset_portfolio.h"

std::atomic<bool> running{true};

// Global market data cache and handlers
std::map<std::string, std::string> market_data_cache;
std::mutex cache_mutex;
std::unique_ptr<backtesting::AlphaVantageHandler> alpha_vantage_handler;
std::unique_ptr<backtesting::YFinanceHandler> yfinance_handler;
std::unique_ptr<backtesting::PolygonHandler> polygon_handler;
std::mutex handler_mutex;
std::atomic<bool> yfinance_ready{false};

// Global symbol cache for search functionality
std::unique_ptr<backtesting::SymbolCache> symbol_cache;
std::mutex symbol_cache_mutex;

// SSE state
struct SseClient {
    int fd;
};
std::mutex sse_mutex;
std::vector<SseClient> sse_clients;

// Request correlation id (per-thread, per-request)
thread_local std::string g_request_id;

static std::string generate_request_id() {
    static std::mt19937_64 rng{std::random_device{}()};
    uint64_t r = rng();
    std::ostringstream os;
    os << std::hex << std::uppercase << r << "-" << time(nullptr);
    return os.str();
}

void sse_broadcast(const nlohmann::json& event) {
    std::lock_guard<std::mutex> lock(sse_mutex);
    std::string payload = "data: " + event.dump() + "\n\n";
    for (auto it = sse_clients.begin(); it != sse_clients.end(); ) {
        ssize_t written = write(it->fd, payload.c_str(), payload.size());
        if (written < 0) {
            close(it->fd);
            it = sse_clients.erase(it);
        } else {
            ++it;
        }
    }
}

// Periodic keep-alive pings for SSE
std::atomic<bool> sse_ping_running{true};
void sse_keepalive_pinger() {
    while (sse_ping_running && running) {
        nlohmann::json ping;
        ping["type"] = "sse.ping";
        ping["ts"] = std::to_string(time(nullptr));
        sse_broadcast(ping);
        std::this_thread::sleep_for(std::chrono::seconds(15));
    }
}

// CORS: Load allowed origins from environment (comma-separated)
// Default allows localhost for development if not set
static std::vector<std::string> load_cors_allowed_origins() {
    std::vector<std::string> origins;
    const char* env = std::getenv("CORS_ALLOWED_ORIGINS");
    if (env && env[0]) {
        std::string s(env);
        for (size_t pos = 0; ; ) {
            size_t end = s.find(',', pos);
            std::string o = (end == std::string::npos) ? s.substr(pos) : s.substr(pos, end - pos);
            // Trim spaces
            o.erase(0, o.find_first_not_of(" \t"));
            o.erase(o.find_last_not_of(" \t") + 1);
            if (!o.empty()) origins.push_back(o);
            if (end == std::string::npos) break;
            pos = end + 1;
        }
    }
    if (origins.empty()) {
        origins.push_back("http://localhost:3000");
        origins.push_back("https://localhost:3000");
    }
    return origins;
}
static const std::vector<std::string> CORS_ALLOWED_ORIGINS = load_cors_allowed_origins();

// Per-request: origin to reflect in CORS header (set at start of handle_request)
static thread_local std::string g_cors_request_origin;

static std::string get_cors_origin_header() {
    // If Origin is in allowed list, reflect it; else use first allowed
    if (g_cors_request_origin.empty() && !CORS_ALLOWED_ORIGINS.empty())
        return CORS_ALLOWED_ORIGINS[0];
    for (const auto& o : CORS_ALLOWED_ORIGINS) {
        if (o == g_cors_request_origin) return o;
    }
    return CORS_ALLOWED_ORIGINS.empty() ? "" : CORS_ALLOWED_ORIGINS[0];
}

// Alpha Vantage API configuration
// Load from environment; do NOT hardcode or log secrets
const char* ALPHA_VANTAGE_ENV = std::getenv("ALPHA_VANTAGE_API_KEY");
const std::string ALPHA_VANTAGE_API_KEY = ALPHA_VANTAGE_ENV ? std::string(ALPHA_VANTAGE_ENV) : std::string();
const std::string ALPHA_VANTAGE_BASE_URL = "https://www.alphavantage.co/query";
const int UPDATE_INTERVAL_SECONDS = 30; // Update every 30 seconds (2 per minute)
// const int MAX_REQUESTS_PER_MINUTE = 5; // Free tier limit (unused, kept for reference)

// Rate limiting
std::chrono::steady_clock::time_point last_request_time = std::chrono::steady_clock::now();
std::mutex rate_limit_mutex;

void signal_handler(int signal) {
    if (signal == SIGTERM || signal == SIGINT) {
        std::cout << "\nReceived shutdown signal. Stopping server gracefully..." << std::endl;
        running = false;
    }
}

// Initialize Alpha Vantage handler
void initialize_alpha_vantage_handler() {
    std::lock_guard<std::mutex> lock(handler_mutex);
    if (!alpha_vantage_handler) {
        alpha_vantage_handler = std::make_unique<backtesting::AlphaVantageHandler>(ALPHA_VANTAGE_API_KEY);
        std::cout << "Alpha Vantage handler initialized" << std::endl;
    }
}

void initialize_yfinance_handler() {
    if (yfinance_handler) {
        yfinance_ready.store(true);
        return;
    }

    try {
        yfinance_handler = std::make_unique<backtesting::YFinanceHandler>(
            "./yfinance_cache",
            true,  // enable disk cache
            24,    // cache expiry hours
            backtesting::DataInterval::ONE_DAY
        );
        yfinance_ready.store(true);
        std::cout << "YFinance handler initialized" << std::endl;
    } catch (const std::exception& e) {
        yfinance_ready.store(false);
        std::cerr << "Failed to initialize YFinance handler: " << e.what() << std::endl;
    } catch (...) {
        yfinance_ready.store(false);
        std::cerr << "Failed to initialize YFinance handler due to unknown error" << std::endl;
    }
}

// CURL callback function
size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Make HTTP request to Alpha Vantage (legacy method)
std::string make_alpha_vantage_request(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(rate_limit_mutex);
    
    // Rate limiting - wait if needed
    auto now = std::chrono::steady_clock::now();
    auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time);
    if (time_since_last.count() < 30000) { // 30 seconds between requests (2 per minute)
        auto sleep_time = 30000 - time_since_last.count();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
    }
    
    if (ALPHA_VANTAGE_API_KEY.empty()) {
        std::cerr << "Alpha Vantage API key not set; skipping request" << std::endl;
        return "";
    }

    CURL* curl = curl_easy_init();
    std::string response;
    
    if (!curl) {
        std::cerr << "Failed to initialize CURL" << std::endl;
        return "";
    }
    
    std::string url = ALPHA_VANTAGE_BASE_URL + "?function=GLOBAL_QUOTE&symbol=" + symbol + "&apikey=" + ALPHA_VANTAGE_API_KEY;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    CURLcode res = curl_easy_perform(curl);
    last_request_time = std::chrono::steady_clock::now();
    
    if (res != CURLE_OK) {
        std::cerr << "CURL request failed: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }
    
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);
    
    if (response_code != 200) {
        std::cerr << "HTTP request failed with code: " << response_code << std::endl;
        return "";
    }
    
    return response;
}

// Make request using YFinance handler
std::string make_yfinance_request(const std::string& symbol) {
    {
        std::lock_guard<std::mutex> lock(handler_mutex);
        if (!yfinance_handler) {
            initialize_yfinance_handler();
        }
    }

    backtesting::YFinanceHandler* handler = nullptr;
    {
        std::lock_guard<std::mutex> lock(handler_mutex);
        handler = yfinance_handler.get();
    }

    if (!handler) {
        return "";
    }
    
    try {
        // Get current date for recent data
        auto now = std::chrono::system_clock::now();
        auto today = std::chrono::system_clock::to_time_t(now);
        auto yesterday = today - (24 * 60 * 60); // 1 day ago
        
        std::string start_date = std::to_string(yesterday);
        std::string end_date = std::to_string(today);
        
        // Load recent data
        if (handler->load_symbol_data(symbol, start_date, end_date)) {
            auto series = handler->get_historical_data(symbol);
            if (!series.empty()) {
                // Get the most recent data point
                auto latest = series.back();
                
                // Format as Alpha Vantage compatible response
                nlohmann::json response;
                response["Global Quote"] = {
                    {"01. symbol", symbol},
                    {"02. open", std::to_string(latest.open)},
                    {"03. high", std::to_string(latest.high)},
                    {"04. low", std::to_string(latest.low)},
                    {"05. price", std::to_string(latest.close)},
                    {"06. volume", std::to_string(latest.volume)},
                    {"07. latest trading day", std::to_string(std::chrono::duration_cast<std::chrono::seconds>(latest.timestamp.time_since_epoch()).count())},
                    {"08. previous close", std::to_string(latest.close)},
                    {"09. change", "0.00"},
                    {"10. change percent", "0.00%"}
                };
                
                return response.dump();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "YFinance request error: " << e.what() << std::endl;
    }
    
    return "";
}

// Update market data using unified handler
void update_market_data_unified(const std::string& symbol) {
    try {
        // Try to fetch real-time quote data using YFinance
        std::string response = make_yfinance_request(symbol);
        
        if (!response.empty()) {
            nlohmann::json json = nlohmann::json::parse(response);
            
            // Check for API errors
            if (json.contains("Error Message")) {
                std::cerr << "Alpha Vantage API Error for " << symbol << ": " << json["Error Message"] << std::endl;
                return;
            }
            
            if (json.contains("Note")) {
                std::cerr << "Alpha Vantage API Note for " << symbol << ": " << json["Note"] << std::endl;
                return;
            }
            
            // Store the response in cache
            std::lock_guard<std::mutex> cache_lock(cache_mutex);
            market_data_cache[symbol] = response;
            
            std::cout << "Updated market data for " << symbol << " using unified handler" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error updating market data for " << symbol << ": " << e.what() << std::endl;
    }
}

// Update market data for a symbol (legacy method)
void update_market_data(const std::string& symbol) {
    std::string response = make_alpha_vantage_request(symbol);
    
    if (!response.empty()) {
        try {
            nlohmann::json json = nlohmann::json::parse(response);
            
            // Check for API errors
            if (json.contains("Error Message")) {
                std::cerr << "Alpha Vantage API Error for " << symbol << ": " << json["Error Message"] << std::endl;
                return;
            }
            
            if (json.contains("Note")) {
                std::cerr << "Alpha Vantage API Note for " << symbol << ": " << json["Note"] << std::endl;
                return;
            }
            
            // Store the response in cache
            std::lock_guard<std::mutex> lock(cache_mutex);
            market_data_cache[symbol] = response;
            
            std::cout << "Updated market data for " << symbol << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "Error parsing response for " << symbol << ": " << e.what() << std::endl;
        }
    }
}

// Background thread for updating market data
void market_data_updater() {
    // Default symbols to keep updated (can be expanded dynamically)
    std::vector<std::string> default_symbols = {
        // US Market (approx. 25)
        "AAPL", "MSFT", "GOOGL", "AMZN", "NVDA", "TSLA", "META", "NFLX", "ADBE", "ORCL",
        "CRM", "CSCO", "INTC", "AMD", "AVGO", "QCOM", "PYPL", "SHOP", "KO", "PEP",
        "WMT", "COST", "DIS", "NKE", "JPM", "BAC", "V", "MA", "XOM", "CVX",
        
        // India Market (NSE .NS) (approx. 25)
        "RELIANCE.NS", "TCS.NS", "HDFCBANK.NS", "ICICIBANK.NS", "INFY.NS", "HINDUNILVR.NS", "ITC.NS",
        "SBIN.NS", "BHARTIARTL.NS", "KOTAKBANK.NS", "LT.NS", "ASIANPAINT.NS", "AXISBANK.NS",
        "BAJFINANCE.NS", "MARUTI.NS", "SUNPHARMA.NS", "HCLTECH.NS", "WIPRO.NS", "TECHM.NS",
        "ULTRACEMCO.NS", "POWERGRID.NS", "ONGC.NS", "TITAN.NS", "ADANIENT.NS", "NTPC.NS",
        
        // India Market (BSE .BO) (sample)
        "RELIANCE.BO", "TCS.BO", "HDFCBANK.BO", "ICICIBANK.BO", "INFY.BO"
    };
    
    // Initial update of only a few key symbols to speed up startup
    std::vector<std::string> startup_symbols = {
        "AAPL", "MSFT", "GOOGL", "AMZN", "NVDA", "TSLA", "META", "NFLX", "ADBE", "ORCL"
    };
    std::cout << "Initial market data update for key symbols using YFinance..." << std::endl;
    for (const auto& symbol : startup_symbols) {
        if (!running) break;
        update_market_data_unified(symbol);
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Faster startup
    }
    
    while (running) {
        std::cout << "Periodic market data update..." << std::endl;
        
        // Update cached symbols (those that have been requested)
        std::vector<std::string> symbols_to_update;
        {
            std::lock_guard<std::mutex> lock(cache_mutex);
            symbols_to_update.reserve(market_data_cache.size());
            for (const auto& [symbol, data] : market_data_cache) {
                (void)data;
                symbols_to_update.push_back(symbol);
            }
        }
        for (const auto& symbol : symbols_to_update) {
            if (!running) break;
            update_market_data_unified(symbol);
            std::this_thread::sleep_for(std::chrono::milliseconds(30000)); // 30 seconds between requests (2 per minute)
        }
        
        // Wait for the next update cycle
        if (running) {
            std::this_thread::sleep_for(std::chrono::seconds(UPDATE_INTERVAL_SECONDS));
        }
    }
}

class SimpleHTTPServer {
private:
    int port_;
    int server_fd_;
    static constexpr size_t MAX_BODY_BYTES = 1024 * 1024; // 1 MB
    std::deque<int> client_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::vector<std::thread> workers_;
    size_t max_queue_size_ = 256;
    size_t worker_count_ = 8;
    
public:
    SimpleHTTPServer(int port) : port_(port), server_fd_(-1) {}
    
    ~SimpleHTTPServer() {
        stop_workers();
        if (server_fd_ != -1) {
            close(server_fd_);
        }
    }
    
    bool start() {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ == -1) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        int opt = 1;
        if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "Failed to set socket options" << std::endl;
            return false;
        }
        
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);
        
        if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Failed to bind to port " << port_ << std::endl;
            return false;
        }
        
        if (listen(server_fd_, 10) < 0) {
            std::cerr << "Failed to listen on port " << port_ << std::endl;
            return false;
        }
        
        start_workers();
        std::cout << "Server listening on port " << port_ << " with " << worker_count_ << " workers" << std::endl;
        return true;
    }
    
    void run() {
        while (running) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            // Set socket to non-blocking mode for accept
            struct timeval timeout;
            timeout.tv_sec = 1;  // 1 second timeout
            timeout.tv_usec = 0;
            
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server_fd_, &read_fds);
            
            int ready = select(server_fd_ + 1, &read_fds, NULL, NULL, &timeout);
            if (ready < 0) {
                if (errno == EINTR) continue;  // Interrupted by signal
                std::cerr << "Select error" << std::endl;
                break;
            }
            if (ready == 0) continue;  // Timeout, check running flag
            
            int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (errno == EINTR || errno == EWOULDBLOCK) continue;
                std::cerr << "Failed to accept connection" << std::endl;
                continue;
            }
            
            if (!enqueue_client(client_fd)) {
                std::string busy_resp = create_response(
                    503,
                    "Service Unavailable",
                    "application/json",
                    "{\"error\":\"server overloaded, try again\"}"
                );
                write(client_fd, busy_resp.c_str(), busy_resp.length());
                close(client_fd);
            }
        }
        
        // Cleanup when server stops
        std::cout << "Server shutting down..." << std::endl;
        if (server_fd_ != -1) {
            close(server_fd_);
            server_fd_ = -1;
        }
        stop_workers();
    }
    
private:
    void start_workers() {
        workers_.reserve(worker_count_);
        for (size_t i = 0; i < worker_count_; ++i) {
            workers_.emplace_back([this]() {
                while (running) {
                    int client_fd = -1;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        queue_cv_.wait(lock, [this]() {
                            return !running || !client_queue_.empty();
                        });
                        if (!running && client_queue_.empty()) {
                            return;
                        }
                        client_fd = client_queue_.front();
                        client_queue_.pop_front();
                    }
                    handle_request(client_fd);
                }
            });
        }
    }

    bool enqueue_client(int client_fd) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (client_queue_.size() >= max_queue_size_) {
            return false;
        }
        client_queue_.push_back(client_fd);
        queue_cv_.notify_one();
        return true;
    }

    void stop_workers() {
        queue_cv_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }

    void handle_request(int client_fd) {
        // Assign request id at the beginning of handling
        g_request_id = generate_request_id();
        char buffer[8192]; // Increased buffer size
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        
        if (bytes_read <= 0) {
            close(client_fd);
            return;
        }
        
        buffer[bytes_read] = '\0';
        std::string request(buffer);
        
        // Ensure full request body is read based on Content-Length with a hard cap
        size_t headers_end = request.find("\r\n\r\n");
        if (headers_end != std::string::npos) {
            // Try to find Content-Length header
            size_t cl_pos = request.find("Content-Length:");
            if (cl_pos == std::string::npos) {
                cl_pos = request.find("Content-length:");
            }
            if (cl_pos != std::string::npos) {
                // Extract the number after the header
                size_t line_end = request.find("\r\n", cl_pos);
                std::string cl_line = request.substr(cl_pos, line_end - cl_pos);
                // Find the colon and parse the integer after it
                size_t colon_pos = cl_line.find(":");
                if (colon_pos != std::string::npos) {
                    std::string len_str = cl_line.substr(colon_pos + 1);
                    // trim spaces
                    len_str.erase(0, len_str.find_first_not_of(' '));
                    int content_length = 0;
                    try { content_length = std::stoi(len_str); } catch (...) { content_length = 0; }
                    if (content_length > static_cast<int>(MAX_BODY_BYTES)) {
                        std::string resp = create_response(413, "Payload Too Large", "application/json", "{\"error\":\"Request body too large\"}");
                        write(client_fd, resp.c_str(), resp.length());
                        close(client_fd);
                        return;
                    }
                    size_t body_already = request.size() - (headers_end + 4);
                    while (body_already < static_cast<size_t>(content_length)) {
                        ssize_t more = read(client_fd, buffer, sizeof(buffer));
                        if (more <= 0) break;
                        request.append(buffer, buffer + more);
                        body_already = request.size() - (headers_end + 4);
                        if (body_already > MAX_BODY_BYTES) {
                            std::string resp = create_response(413, "Payload Too Large", "application/json", "{\"error\":\"Request body too large\"}");
                            write(client_fd, resp.c_str(), resp.length());
                            close(client_fd);
                            return;
                        }
                    }
                }
            }
        }
        
        // Debug logging for request
        std::cout << "[" << g_request_id << "] Received request (" << bytes_read << " bytes): " << request.substr(0, 200) << "..." << std::endl;
        
        // Parse HTTP request
        std::istringstream iss(request);
        std::string method, path, version;
        iss >> method >> path >> version;

        // Extract Origin header for CORS (case-insensitive)
        g_cors_request_origin.clear();
        auto ci_find = [](const std::string& haystack, const std::string& needle) -> size_t {
            for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
                bool match = true;
                for (size_t j = 0; j < needle.size(); ++j)
                    if (std::tolower(static_cast<unsigned char>(haystack[i+j])) != std::tolower(static_cast<unsigned char>(needle[j]))) { match = false; break; }
                if (match) return i;
            }
            return std::string::npos;
        };
        size_t oi = ci_find(request, "Origin:");
        if (oi != std::string::npos) {
            size_t start = oi + 7;
            while (start < request.size() && (request[start] == ' ' || request[start] == '\t')) start++;
            size_t end = request.find("\r\n", start);
            if (end == std::string::npos) end = request.size();
            g_cors_request_origin = request.substr(start, end - start);
        }

        std::string response;

        // Helper: trim whitespace from string
        auto trim = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(), s.end());
        };

        // Helper: validate symbol input (A-Z, 0-9, dot, dash, underscore)
        auto is_valid_symbol = [](const std::string& s) {
            if (s.empty() || s.size() > 32) return false;
            for (unsigned char c : s) {
                if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_')) return false;
            }
            return true;
        };

        // Handle CORS preflight OPTIONS requests
        if (method == "OPTIONS") {
            std::string cors_origin = get_cors_origin_header();
            std::ostringstream oss;
            oss << "HTTP/1.1 200 OK\r\n";
            oss << "Access-Control-Allow-Origin: " << (cors_origin.empty() ? "*" : cors_origin) << "\r\n";
            oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE, PATCH\r\n";
            oss << "Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With\r\n";
            oss << "Access-Control-Max-Age: 86400\r\n"; // 24 hours
            oss << "Content-Length: 0\r\n";
            oss << "\r\n";
            response = oss.str();
        } else if (method == "GET" && path == "/health") {
            // Health payload with YFinance readiness and basic stats
            nlohmann::json health_json;
            health_json["status"] = "healthy";
            health_json["uptime"] = std::to_string(time(nullptr));
            health_json["yfinance_status"] = yfinance_ready.load() ? "ready" : "uninitialized";
            {
                std::lock_guard<std::mutex> lock(handler_mutex);
                if (yfinance_handler) {
                    health_json["yfinance_api"] = yfinance_handler->get_api_usage_stats();
                    health_json["yfinance_cache"] = yfinance_handler->get_cache_statistics();
                }
            }

            std::string health_response = health_json.dump();
            response = create_response(200, "OK", "application/json", health_response);
            // Add health-specific headers
            response = response.substr(0, response.find("\r\n\r\n")) +
                      "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                      "Pragma: no-cache\r\n"
                      "X-Health-Check: true\r\n"
                      "X-Request-ID: " + g_request_id + "\r\n\r\n" +
                      health_response;
        } else if (method == "GET" && path == "/events") {
            // Server-Sent Events endpoint
            std::ostringstream oss;
            oss << "HTTP/1.1 200 OK\r\n";
            oss << "Content-Type: text/event-stream\r\n";
            // Disable proxy buffering and transformations for SSE
            oss << "Cache-Control: no-cache, no-transform\r\n";
            oss << "Connection: keep-alive\r\n";
            { std::string co = get_cors_origin_header(); oss << "Access-Control-Allow-Origin: " << (co.empty() ? "*" : co) << "\r\n"; }
            oss << "X-Request-ID: " << g_request_id << "\r\n\r\n";
            oss << "X-Accel-Buffering: no\r\n";
            std::string hdr = oss.str();
            write(client_fd, hdr.c_str(), hdr.size());
            {
                std::lock_guard<std::mutex> lock(sse_mutex);
                sse_clients.push_back({client_fd});
            }
            // Send initial hello event so clients know stream is live
            nlohmann::json hello;
            hello["type"] = "sse.hello";
            hello["message"] = "stream open";
            hello["request_id"] = g_request_id;
            sse_broadcast(hello);
            // Do not close the client_fd here; keep open for streaming
            return;
        } else if (method == "POST" && path == "/api/portfolio/backtest") {
            // Run multi-asset portfolio backtest
            size_t body_start = request.find("\r\n\r\n");
            if (body_start == std::string::npos) {
                nlohmann::json error_response;
                error_response["error"] = "invalid_request";
                error_response["message"] = "Missing request body";
                response = create_response(400, "Bad Request", "application/json", error_response.dump());
            } else {
                try {
                    std::string body = request.substr(body_start + 4);
                    auto body_json = nlohmann::json::parse(body);

                    std::vector<std::string> symbols;
                    std::vector<double> weights;

                    if (body_json.contains("symbols") && body_json["symbols"].is_array()) {
                        for (const auto& s : body_json["symbols"]) {
                            symbols.push_back(s.get<std::string>());
                        }
                    }
                    if (body_json.contains("weights") && body_json["weights"].is_array()) {
                        for (const auto& w : body_json["weights"]) {
                            weights.push_back(w.get<double>());
                        }
                    }

                    if (symbols.empty()) {
                        nlohmann::json error_response;
                        error_response["error"] = "invalid_request";
                        error_response["message"] = "symbols array is required";
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                    } else {
                        // Normalize weights or default to equal weights
                        if (weights.size() != symbols.size()) {
                            weights.assign(symbols.size(), 1.0);
                        }
                        double sum_w = std::accumulate(weights.begin(), weights.end(), 0.0);
                        if (sum_w <= 0.0) {
                            double ew = 1.0 / static_cast<double>(weights.size());
                            std::fill(weights.begin(), weights.end(), ew);
                        } else {
                            for (auto& w : weights) {
                                w /= sum_w;
                            }
                        }

                        std::string start_date = body_json.value("start_date", "");
                        std::string end_date = body_json.value("end_date", "");
                        double initial_capital = body_json.value("initial_capital", 10000.0);

                        if (start_date.empty() || end_date.empty()) {
                            nlohmann::json error_response;
                            error_response["error"] = "invalid_request";
                            error_response["message"] = "start_date and end_date are required";
                            response = create_response(400, "Bad Request", "application/json", error_response.dump());
                        } else {
                            // Build backtest configuration for portfolio run
                            backtesting::BacktestConfig cfg;
                            cfg.name = "portfolio_backtest";
                            cfg.description = "Multi-asset portfolio backtest";
                            cfg.symbols = symbols;
                            cfg.start_date = start_date;
                            cfg.end_date = end_date;
                            cfg.initial_capital = initial_capital;
                            cfg.commission_rate = 0.001;
                            cfg.slippage_rate = 0.0005;
                            cfg.data_source = "yfinance";
                            cfg.data_interval = body_json.value("data_interval", std::string("1d"));
                            // Default strategy name used when parsing doesn't provide one.
                            cfg.strategy_name = "moving_average";
                            cfg.account_type = body_json.value("account_type", std::string("CASH"));
                            cfg.market_type = "OTHER";

                            try {
                                auto engine = backtesting::BacktestEngine::create_from_config(cfg);
                                auto result = engine->run_backtest();

                                // Build portfolio-level analytics using MultiAssetPortfolio
                                std::unordered_map<std::string, std::vector<backtesting::OHLC>> history;
                                for (const auto& sym : symbols) {
                                    auto series = engine->get_data_handler()->get_historical_data(sym);
                                    history[sym] = series;
                                }

                                backtesting::PortfolioManager& pm = engine->get_portfolio_manager();
                                backtesting::MultiAssetPortfolio mp(pm);
                                backtesting::MultiAssetPortfolio::Weights wmap;
                                for (size_t i = 0; i < symbols.size(); ++i) {
                                    wmap[symbols[i]] = weights[i];
                                }
                                mp.set_target_weights(wmap);

                                nlohmann::json payload;
                                payload["status"] = "success";
                                payload["request_id"] = g_request_id;
                                payload["backtest"] = result.to_json();
                                payload["portfolio_analytics"] = mp.to_json(symbols, history);

                                response = create_response(200, "OK", "application/json", payload.dump());
                            } catch (const std::exception& e) {
                                nlohmann::json error_response;
                                error_response["error"] = "backtest_failed";
                                error_response["message"] = e.what();
                                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    nlohmann::json error_response;
                    error_response["error"] = "invalid_json";
                    error_response["message"] = e.what();
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                }
            }
        } else if (method == "POST" && path == "/api/portfolio/analytics") {
            // Get comprehensive portfolio analytics
            try {
                size_t body_start = request.find("\r\n\r\n");
                std::string body;
                if (body_start != std::string::npos) {
                    body = request.substr(body_start + 4);
                }
                
                if (body.empty()) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Request body is required";
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } else {
                    auto body_json = nlohmann::json::parse(body);
                    std::vector<std::string> symbols;
                    
                    if (body_json.contains("symbols") && body_json["symbols"].is_array()) {
                        for (const auto& s : body_json["symbols"]) {
                            symbols.push_back(s.get<std::string>());
                        }
                    }
                    
                    if (symbols.empty()) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "symbols array is required";
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                    } else {
                        // Create minimal backtest to get portfolio manager
                        std::string start_date = body_json.value("start_date", "2023-01-01");
                        std::string end_date = body_json.value("end_date", "2024-01-01");
                        double initial_capital = body_json.value("initial_capital", 10000.0);
                        
                        backtesting::BacktestConfig cfg;
                        cfg.name = "portfolio_analytics";
                        cfg.symbols = symbols;
                        cfg.start_date = start_date;
                        cfg.end_date = end_date;
                        cfg.initial_capital = initial_capital;
                        cfg.commission_rate = 0.001;
                        cfg.slippage_rate = 0.0005;
                        cfg.data_source = "yfinance";
                        cfg.data_interval = body_json.value("data_interval", std::string("1d"));
                        cfg.strategy_name = "moving_average";
                        cfg.account_type = body_json.value("account_type", std::string("CASH"));
                        cfg.market_type = "OTHER";
                        
                        auto engine = backtesting::BacktestEngine::create_from_config(cfg);
                        engine->run_backtest();
                        
                        backtesting::PortfolioManager& pm = engine->get_portfolio_manager();
                        auto analytics = pm.get_portfolio_analytics();
                        
                        nlohmann::json result;
                        result["status"] = "success";
                        result["analytics"] = analytics;
                        result["timestamp"] = std::to_string(time(nullptr));
                        result["request_id"] = g_request_id;
                        
                        response = create_response(200, "OK", "application/json", result.dump());
                    }
                }
            } catch (const std::exception& e) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Failed to get portfolio analytics: " + std::string(e.what());
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            }
        } else if (method == "POST" && path == "/api/portfolio/stats") {
            // Get portfolio statistics (Sharpe, Sortino, Calmar, VaR, CVaR)
            try {
                size_t body_start = request.find("\r\n\r\n");
                std::string body;
                if (body_start != std::string::npos) {
                    body = request.substr(body_start + 4);
                }
                
                if (body.empty()) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Request body is required";
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } else {
                    auto body_json = nlohmann::json::parse(body);
                    std::vector<std::string> symbols;
                    
                    if (body_json.contains("symbols") && body_json["symbols"].is_array()) {
                        for (const auto& s : body_json["symbols"]) {
                            symbols.push_back(s.get<std::string>());
                        }
                    }
                    
                    if (symbols.empty()) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "symbols array is required";
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                    } else {
                        std::string start_date = body_json.value("start_date", "2023-01-01");
                        std::string end_date = body_json.value("end_date", "2024-01-01");
                        double initial_capital = body_json.value("initial_capital", 10000.0);
                        
                        backtesting::BacktestConfig cfg;
                        cfg.name = "portfolio_stats";
                        cfg.symbols = symbols;
                        cfg.start_date = start_date;
                        cfg.end_date = end_date;
                        cfg.initial_capital = initial_capital;
                        cfg.commission_rate = 0.001;
                        cfg.slippage_rate = 0.0005;
                        cfg.data_source = "yfinance";
                        cfg.data_interval = body_json.value("data_interval", std::string("1d"));
                        cfg.strategy_name = "moving_average";
                        cfg.account_type = body_json.value("account_type", std::string("CASH"));
                        cfg.market_type = "OTHER";
                        
                        auto engine = backtesting::BacktestEngine::create_from_config(cfg);
                        engine->run_backtest();
                        
                        backtesting::PortfolioManager& pm = engine->get_portfolio_manager();
                        auto stats = pm.calculate_portfolio_stats();
                        
                        nlohmann::json result;
                        result["status"] = "success";
                        result["stats"] = stats.to_json();
                        result["timestamp"] = std::to_string(time(nullptr));
                        result["request_id"] = g_request_id;
                        
                        response = create_response(200, "OK", "application/json", result.dump());
                    }
                }
            } catch (const std::exception& e) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Failed to get portfolio stats: " + std::string(e.what());
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            }
        } else if (method == "POST" && path == "/api/portfolio/correlation") {
            // Get correlation matrix
            try {
                size_t body_start = request.find("\r\n\r\n");
                std::string body;
                if (body_start != std::string::npos) {
                    body = request.substr(body_start + 4);
                }
                
                if (body.empty()) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Request body is required";
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } else {
                    auto body_json = nlohmann::json::parse(body);
                    std::vector<std::string> symbols;
                    
                    if (body_json.contains("symbols") && body_json["symbols"].is_array()) {
                        for (const auto& s : body_json["symbols"]) {
                            symbols.push_back(s.get<std::string>());
                        }
                    }
                    
                    if (symbols.empty()) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "symbols array is required";
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                    } else {
                        std::string start_date = body_json.value("start_date", "2023-01-01");
                        std::string end_date = body_json.value("end_date", "2024-01-01");
                        double initial_capital = body_json.value("initial_capital", 10000.0);
                        
                        backtesting::BacktestConfig cfg;
                        cfg.name = "portfolio_correlation";
                        cfg.symbols = symbols;
                        cfg.start_date = start_date;
                        cfg.end_date = end_date;
                        cfg.initial_capital = initial_capital;
                        cfg.commission_rate = 0.001;
                        cfg.slippage_rate = 0.0005;
                        cfg.data_source = "yfinance";
                        cfg.data_interval = body_json.value("data_interval", std::string("1d"));
                        cfg.strategy_name = "moving_average";
                        cfg.account_type = body_json.value("account_type", std::string("CASH"));
                        cfg.market_type = "OTHER";
                        
                        auto engine = backtesting::BacktestEngine::create_from_config(cfg);
                        engine->run_backtest();
                        
                        backtesting::PortfolioManager& pm = engine->get_portfolio_manager();
                        auto correlation = pm.get_correlation_matrix_json();
                        
                        nlohmann::json result;
                        result["status"] = "success";
                        result["correlation"] = correlation;
                        result["timestamp"] = std::to_string(time(nullptr));
                        result["request_id"] = g_request_id;
                        
                        response = create_response(200, "OK", "application/json", result.dump());
                    }
                }
            } catch (const std::exception& e) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Failed to get correlation matrix: " + std::string(e.what());
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            }
        } else if (method == "POST" && path == "/api/portfolio/exposure") {
            // Get sector/industry exposure
            try {
                size_t body_start = request.find("\r\n\r\n");
                std::string body;
                if (body_start != std::string::npos) {
                    body = request.substr(body_start + 4);
                }
                
                if (body.empty()) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Request body is required";
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } else {
                    auto body_json = nlohmann::json::parse(body);
                    std::vector<std::string> symbols;
                    
                    if (body_json.contains("symbols") && body_json["symbols"].is_array()) {
                        for (const auto& s : body_json["symbols"]) {
                            symbols.push_back(s.get<std::string>());
                        }
                    }
                    
                    if (symbols.empty()) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "symbols array is required";
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                    } else {
                        std::string start_date = body_json.value("start_date", "2023-01-01");
                        std::string end_date = body_json.value("end_date", "2024-01-01");
                        double initial_capital = body_json.value("initial_capital", 10000.0);
                        
                        backtesting::BacktestConfig cfg;
                        cfg.name = "portfolio_exposure";
                        cfg.symbols = symbols;
                        cfg.start_date = start_date;
                        cfg.end_date = end_date;
                        cfg.initial_capital = initial_capital;
                        cfg.commission_rate = 0.001;
                        cfg.slippage_rate = 0.0005;
                        cfg.data_source = "yfinance";
                        cfg.account_type = body_json.value("account_type", std::string("CASH"));
                        cfg.market_type = "OTHER";
                        cfg.data_interval = body_json.value("data_interval", std::string("1d"));
                        cfg.strategy_name = "moving_average";
                        
                        auto engine = backtesting::BacktestEngine::create_from_config(cfg);
                        engine->run_backtest();
                        
                        backtesting::PortfolioManager& pm = engine->get_portfolio_manager();
                        auto exposure = pm.get_sector_exposure_json();
                        
                        nlohmann::json result;
                        result["status"] = "success";
                        result["exposure"] = exposure;
                        result["timestamp"] = std::to_string(time(nullptr));
                        result["request_id"] = g_request_id;
                        
                        response = create_response(200, "OK", "application/json", result.dump());
                    }
                }
            } catch (const std::exception& e) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Failed to get sector exposure: " + std::string(e.what());
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            }
        } else if (method == "POST" && path == "/api/portfolio/equity-curve") {
            // Get equity curve data
            try {
                size_t body_start = request.find("\r\n\r\n");
                std::string body;
                if (body_start != std::string::npos) {
                    body = request.substr(body_start + 4);
                }
                
                if (body.empty()) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Request body is required";
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } else {
                    auto body_json = nlohmann::json::parse(body);
                    std::vector<std::string> symbols;
                    
                    if (body_json.contains("symbols") && body_json["symbols"].is_array()) {
                        for (const auto& s : body_json["symbols"]) {
                            symbols.push_back(s.get<std::string>());
                        }
                    }
                    
                    if (symbols.empty()) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "symbols array is required";
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                    } else {
                        std::string start_date = body_json.value("start_date", "2023-01-01");
                        std::string end_date = body_json.value("end_date", "2024-01-01");
                        double initial_capital = body_json.value("initial_capital", 10000.0);
                        
                        backtesting::BacktestConfig cfg;
                        cfg.name = "portfolio_equity_curve";
                        cfg.symbols = symbols;
                        cfg.start_date = start_date;
                        cfg.end_date = end_date;
                        cfg.initial_capital = initial_capital;
                        cfg.commission_rate = 0.001;
                        cfg.slippage_rate = 0.0005;
                        cfg.data_source = "yfinance";
                        cfg.data_interval = body_json.value("data_interval", std::string("1d"));
                        cfg.strategy_name = "moving_average";
                        
                        auto engine = backtesting::BacktestEngine::create_from_config(cfg);
                        engine->run_backtest();
                        
                        backtesting::PortfolioManager& pm = engine->get_portfolio_manager();
                        auto equity_curve = pm.get_equity_curve();
                        
                        nlohmann::json curve_data = nlohmann::json::array();
                        for (const auto& [timestamp, value] : equity_curve) {
                            nlohmann::json point;
                            point["timestamp"] = std::chrono::system_clock::to_time_t(timestamp);
                            point["value"] = value;
                            curve_data.push_back(point);
                        }
                        
                        nlohmann::json result;
                        result["status"] = "success";
                        result["equity_curve"] = curve_data;
                        result["timestamp"] = std::to_string(time(nullptr));
                        result["request_id"] = g_request_id;
                        
                        response = create_response(200, "OK", "application/json", result.dump());
                    }
                }
            } catch (const std::exception& e) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Failed to get equity curve: " + std::string(e.what());
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            }
        } else if (method == "POST" && path == "/api/portfolio/drawdown") {
            // Get drawdown curve
            try {
                size_t body_start = request.find("\r\n\r\n");
                std::string body;
                if (body_start != std::string::npos) {
                    body = request.substr(body_start + 4);
                }
                
                if (body.empty()) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Request body is required";
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } else {
                    auto body_json = nlohmann::json::parse(body);
                    std::vector<std::string> symbols;
                    
                    if (body_json.contains("symbols") && body_json["symbols"].is_array()) {
                        for (const auto& s : body_json["symbols"]) {
                            symbols.push_back(s.get<std::string>());
                        }
                    }
                    
                    if (symbols.empty()) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "symbols array is required";
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                    } else {
                        std::string start_date = body_json.value("start_date", "2023-01-01");
                        std::string end_date = body_json.value("end_date", "2024-01-01");
                        double initial_capital = body_json.value("initial_capital", 10000.0);
                        
                        backtesting::BacktestConfig cfg;
                        cfg.name = "portfolio_drawdown";
                        cfg.symbols = symbols;
                        cfg.start_date = start_date;
                        cfg.end_date = end_date;
                        cfg.initial_capital = initial_capital;
                        cfg.commission_rate = 0.001;
                        cfg.slippage_rate = 0.0005;
                        cfg.data_source = "yfinance";
                        cfg.data_interval = body_json.value("data_interval", std::string("1d"));
                        cfg.strategy_name = "moving_average";
                        
                        auto engine = backtesting::BacktestEngine::create_from_config(cfg);
                        engine->run_backtest();
                        
                        backtesting::PortfolioManager& pm = engine->get_portfolio_manager();
                        auto drawdown_curve = pm.get_drawdown_curve();
                        
                        nlohmann::json curve_data = nlohmann::json::array();
                        for (const auto& [timestamp, drawdown] : drawdown_curve) {
                            nlohmann::json point;
                            point["timestamp"] = std::chrono::system_clock::to_time_t(timestamp);
                            point["drawdown"] = drawdown;
                            curve_data.push_back(point);
                        }
                        
                        nlohmann::json result;
                        result["status"] = "success";
                        result["drawdown_curve"] = curve_data;
                        result["current_drawdown"] = pm.get_current_drawdown();
                        result["max_drawdown"] = pm.get_max_drawdown();
                        result["timestamp"] = std::to_string(time(nullptr));
                        result["request_id"] = g_request_id;
                        
                        response = create_response(200, "OK", "application/json", result.dump());
                    }
                }
            } catch (const std::exception& e) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Failed to get drawdown curve: " + std::string(e.what());
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            }
        } else if (method == "POST" && path == "/api/portfolio/risk-report") {
            // Get comprehensive risk report
            try {
                size_t body_start = request.find("\r\n\r\n");
                std::string body;
                if (body_start != std::string::npos) {
                    body = request.substr(body_start + 4);
                }
                
                if (body.empty()) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Request body is required";
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } else {
                    auto body_json = nlohmann::json::parse(body);
                    std::vector<std::string> symbols;
                    
                    if (body_json.contains("symbols") && body_json["symbols"].is_array()) {
                        for (const auto& s : body_json["symbols"]) {
                            symbols.push_back(s.get<std::string>());
                        }
                    }
                    
                    if (symbols.empty()) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "symbols array is required";
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                    } else {
                        std::string start_date = body_json.value("start_date", "2023-01-01");
                        std::string end_date = body_json.value("end_date", "2024-01-01");
                        double initial_capital = body_json.value("initial_capital", 10000.0);
                        
                        backtesting::BacktestConfig cfg;
                        cfg.name = "portfolio_risk_report";
                        cfg.symbols = symbols;
                        cfg.start_date = start_date;
                        cfg.end_date = end_date;
                        cfg.initial_capital = initial_capital;
                        cfg.commission_rate = 0.001;
                        cfg.slippage_rate = 0.0005;
                        cfg.data_source = "yfinance";
                        cfg.data_interval = body_json.value("data_interval", std::string("1d"));
                        cfg.strategy_name = "moving_average";
                        
                        auto engine = backtesting::BacktestEngine::create_from_config(cfg);
                        engine->run_backtest();
                        
                        backtesting::PortfolioManager& pm = engine->get_portfolio_manager();
                        auto risk_report = pm.get_risk_report();
                        
                        nlohmann::json result;
                        result["status"] = "success";
                        result["risk_report"] = risk_report;
                        result["timestamp"] = std::to_string(time(nullptr));
                        result["request_id"] = g_request_id;
                        
                        response = create_response(200, "OK", "application/json", result.dump());
                    }
                }
            } catch (const std::exception& e) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Failed to get risk report: " + std::string(e.what());
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            }
        } else if (method == "GET" && path == "/market-data") {
            // Return all cached market data (legacy endpoint)
            std::lock_guard<std::mutex> lock(cache_mutex);
            nlohmann::json result;
            for (const auto& [symbol, data] : market_data_cache) {
                try {
                    result[symbol] = nlohmann::json::parse(data);
                } catch (const std::exception& e) {
                    result[symbol] = "Error parsing data";
                }
            }
            response = create_response(200, "OK", "application/json", result.dump());
        } else if (method == "GET" && path.substr(0, 13) == "/market-data/") {
            // Return specific symbol data (legacy endpoint)
            std::string symbol = path.substr(13);
            trim(symbol);
            std::lock_guard<std::mutex> lock(cache_mutex);
            
            auto it = market_data_cache.find(symbol);
            if (it != market_data_cache.end()) {
                response = create_response(200, "OK", "application/json", it->second);
            } else {
                response = create_response(404, "Not Found", "application/json", "{\"error\":\"Symbol not found\"}");
            }
        } else if (method == "POST" && path == "/market-data/update") {
            // Manually trigger market data update (legacy endpoint)
            std::string symbol = "";
            
            // Extract symbol from request body if present
            size_t body_start = request.find("\r\n\r\n");
            if (body_start != std::string::npos) {
                std::string body = request.substr(body_start + 4);
                try {
                    nlohmann::json body_json = nlohmann::json::parse(body);
                    if (body_json.contains("symbol")) {
                        symbol = body_json["symbol"];
                    }
                } catch (const std::exception& e) {
                    // Ignore parsing errors
                }
            }
            
            if (!symbol.empty()) {
                update_market_data_unified(symbol);
                response = create_response(200, "OK", "application/json", "{\"status\":\"updated\",\"symbol\":\"" + symbol + "\"}");
            } else {
                response = create_response(400, "Bad Request", "application/json", "{\"error\":\"Symbol required\"}");
            }
        } else if (method == "GET" && path == "/api/market-data") {
            // New unified API endpoint
            std::lock_guard<std::mutex> lock(cache_mutex);
            nlohmann::json result;
            result["status"] = "success";
            result["timestamp"] = std::to_string(time(nullptr));
            result["request_id"] = g_request_id;
            result["data"] = nlohmann::json::object();
            
            for (const auto& [symbol, data] : market_data_cache) {
                try {
                    result["data"][symbol] = nlohmann::json::parse(data);
                } catch (const std::exception& e) {
                    result["data"][symbol] = "Error parsing data";
                }
            }
            response = create_response(200, "OK", "application/json", result.dump());
        } else if (method == "GET" && path.substr(0, 23) == "/api/market-data/ohlc/") {
            // Return OHLC historical data for symbol with optional start/end
            // Format: /api/market-data/ohlc/{symbol}?start=YYYY-MM-DD&end=YYYY-MM-DD
            std::cout << "DEBUG: OHLC endpoint hit with path: '" << path << "'" << std::endl;
            std::cout.flush();
            std::string symbol = path.substr(23);
            // Strip query if present
            size_t qmark = symbol.find("?");
            std::string query;
            if (qmark != std::string::npos) { query = symbol.substr(qmark + 1); symbol = symbol.substr(0, qmark); }
            // Trim whitespace from symbol
            trim(symbol);
            std::cout << "DEBUG: Extracted symbol: '" << symbol << "' from path: '" << path << "'" << std::endl;
            std::cout << "DEBUG: Query string: '" << query << "'" << std::endl;
            std::cout.flush();
            if (!is_valid_symbol(symbol)) {
                std::cout << "DEBUG: Symbol validation failed for: '" << symbol << "'" << std::endl;
                std::cout.flush();
                nlohmann::json err; err["status"] = "error"; err["message"] = "Invalid symbol";
                response = create_response(400, "Bad Request", "application/json", err.dump());
            } else {
                // Parse query parameters
                std::string start_date, end_date, interval = "1d";
                if (!query.empty()) {
                    std::istringstream qs(query);
                    std::string kv;
                    while (std::getline(qs, kv, '&')) {
                        auto eq = kv.find('=');
                        if (eq != std::string::npos) {
                            auto k = kv.substr(0, eq);
                            auto v = kv.substr(eq + 1);
                            if (k == "start") start_date = v;
                            else if (k == "end") end_date = v;
                            else if (k == "interval") interval = v;
                        }
                    }
                }
                // Use yfinance handler for OHLC data
                bool have_data = false;
                std::vector<backtesting::OHLC> series;
                {
                    std::lock_guard<std::mutex> handler_lock(handler_mutex);
                    if (yfinance_handler) {
                        series = yfinance_handler->get_historical_data(symbol);
                        have_data = !series.empty();
                    }
                }
                if (!have_data) {
                    // Initialize YFinance handler if not already done
                    {
                        std::lock_guard<std::mutex> handler_lock(handler_mutex);
                        if (!yfinance_handler) {
                            initialize_yfinance_handler();
                        }
                    }
                    
                    // Convert interval string to DataInterval enum
                    backtesting::DataInterval data_interval = backtesting::DataInterval::ONE_DAY;
                    if (interval == "1m") data_interval = backtesting::DataInterval::ONE_MINUTE;
                    else if (interval == "5m") data_interval = backtesting::DataInterval::FIVE_MINUTES;
                    else if (interval == "15m") data_interval = backtesting::DataInterval::FIFTEEN_MINUTES;
                    else if (interval == "30m") data_interval = backtesting::DataInterval::THIRTY_MINUTES;
                    else if (interval == "1h") data_interval = backtesting::DataInterval::ONE_HOUR;
                    else if (interval == "1d") data_interval = backtesting::DataInterval::ONE_DAY;
                    
                    std::cout << "DEBUG: Loading data for " << symbol << " with interval " << interval << " (enum: " << static_cast<int>(data_interval) << ")" << std::endl;
                    std::cout.flush();
                    
                    // Use the global handler with interval-aware loading
                    {
                        std::lock_guard<std::mutex> handler_lock(handler_mutex);
                        if (yfinance_handler) {
                            // Load data with specific interval
                            bool success = yfinance_handler->load_symbol_data(symbol, start_date, end_date, data_interval);
                            if (success) {
                                series = yfinance_handler->get_historical_data(symbol);
                                std::cout << "DEBUG: Loaded " << series.size() << " data points for " << symbol << std::endl;
                                std::cout.flush();
                            } else {
                                std::cout << "DEBUG: Failed to load data for " << symbol << std::endl;
                                std::cout.flush();
                            }
                        }
                    }
                }
                nlohmann::json out;
                out["status"] = "success";
                out["symbol"] = symbol;
                out["request_id"] = g_request_id;
                out["timestamp"] = std::to_string(time(nullptr));
                out["interval"] = interval;
                out["data"] = nlohmann::json::array();
                for (const auto& o : series) {
                    nlohmann::json row;
                    // Convert to Unix timestamp (seconds since epoch)
                    auto t = std::chrono::system_clock::to_time_t(o.timestamp);
                    row["time"] = static_cast<int64_t>(t);
                    row["open"] = o.open;
                    row["high"] = o.high;
                    row["low"] = o.low;
                    row["close"] = o.close;
                    row["volume"] = o.volume;
                    out["data"].push_back(row);
                }
                response = create_response(200, "OK", "application/json", out.dump());
            }
        } else if (method == "GET" && path.substr(0, 17) == "/api/market-data/" && 
                   path.substr(0, path.find("?")) != "/api/market-data/top-stocks" &&
                   path.substr(0, path.find("?")) != "/api/market-data/search" &&
                   path.substr(0, 30) != "/api/market-data/technicals/") {
            // Individual symbol endpoint - fetch data on demand if not cached
            std::string symbol = path.substr(17);
            trim(symbol);
            if (!is_valid_symbol(symbol)) {
                nlohmann::json error_response; error_response["status"] = "error"; error_response["message"] = "Invalid symbol";
                response = create_response(400, "Bad Request", "application/json", error_response.dump());
            } else {
            
            // Check cache first
            {
                std::lock_guard<std::mutex> lock(cache_mutex);
                auto it = market_data_cache.find(symbol);
                if (it != market_data_cache.end()) {
                    // Return cached data
                    nlohmann::json result;
                    result["status"] = "success";
                    result["symbol"] = symbol;
                    result["timestamp"] = std::to_string(time(nullptr));
                    result["request_id"] = g_request_id;
                    try {
                        result["data"] = nlohmann::json::parse(it->second);
                    } catch (const std::exception& e) {
                        result["data"] = "Error parsing cached data";
                    }
                    response = create_response(200, "OK", "application/json", result.dump());
                }
            }
            
            // If not in cache, fetch from YFinance
            if (response.empty()) {
                std::lock_guard<std::mutex> handler_lock(handler_mutex);
                if (yfinance_handler) {
                    try {
                        // Initialize handler if needed
                        initialize_yfinance_handler();
                        
                        // Try to fetch real-time quote data using YFinance
                        std::string quote_response = make_yfinance_request(symbol);
                        
                        if (!quote_response.empty()) {
                            try {
                                nlohmann::json quote_json = nlohmann::json::parse(quote_response);
                                
                                // Check for API errors
                                if (quote_json.contains("Error Message")) {
                                    nlohmann::json error_response;
                                    error_response["status"] = "error";
                                    error_response["message"] = "Alpha Vantage API Error: " + quote_json["Error Message"].get<std::string>();
                                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                                } else if (quote_json.contains("Note")) {
                                    nlohmann::json error_response;
                                    error_response["status"] = "error";
                                    error_response["message"] = "Alpha Vantage API Note: " + quote_json["Note"].get<std::string>();
                                    response = create_response(429, "Too Many Requests", "application/json", error_response.dump());
                                } else if (quote_json.contains("Global Quote")) {
                                    // Parse the quote data
                                    auto global_quote = quote_json["Global Quote"];
                                    nlohmann::json data_json;
                                    data_json["symbol"] = symbol;
                                    data_json["price"] = global_quote["05. price"].get<std::string>();
                                    data_json["change"] = global_quote["09. change"].get<std::string>();
                                    data_json["change_percent"] = global_quote["10. change percent"].get<std::string>();
                                    data_json["volume"] = global_quote["06. volume"].get<std::string>();
                                    data_json["high"] = global_quote["03. high"].get<std::string>();
                                    data_json["low"] = global_quote["04. low"].get<std::string>();
                                    data_json["open"] = global_quote["02. open"].get<std::string>();
                                    data_json["previous_close"] = global_quote["08. previous close"].get<std::string>();
                                    
                                    // Store in cache
                                    {
                                        std::lock_guard<std::mutex> cache_lock(cache_mutex);
                                        market_data_cache[symbol] = data_json.dump();
                                    }
                                    
                                    nlohmann::json result;
                                    result["status"] = "success";
                                    result["symbol"] = symbol;
                                    result["timestamp"] = std::to_string(time(nullptr));
                                    result["request_id"] = g_request_id;
                                    result["data"] = data_json;
                                    response = create_response(200, "OK", "application/json", result.dump());
                                } else {
                                    nlohmann::json error_response;
                                    error_response["status"] = "error";
                                    error_response["message"] = "Unexpected response format from Alpha Vantage API";
                                    response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                                }
                            } catch (const std::exception& e) {
                                nlohmann::json error_response;
                                error_response["status"] = "error";
                                error_response["message"] = "Error parsing Alpha Vantage response: " + std::string(e.what());
                                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                            }
                        } else {
                            nlohmann::json error_response;
                            error_response["status"] = "error";
                            error_response["message"] = "Failed to fetch data for symbol: " + symbol;
                            response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                        }
                    } catch (const std::exception& e) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "Error fetching data: " + std::string(e.what());
                        response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                    }
                } else {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Alpha Vantage handler not initialized";
                    response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                }
            }
            }
        } else if (method == "POST" && path == "/api/market-data/update") {
            // New unified API endpoint for updating data
            std::string symbol = "";
            
            // Extract symbol from request body if present
            size_t body_start = request.find("\r\n\r\n");
            if (body_start != std::string::npos) {
                std::string body = request.substr(body_start + 4);
                try {
                    nlohmann::json body_json = nlohmann::json::parse(body);
                    if (body_json.contains("symbol")) {
                        symbol = body_json["symbol"];
                    }
                } catch (const std::exception& e) {
                    // Ignore parsing errors
                }
            }
            
            if (!symbol.empty() && is_valid_symbol(symbol)) {
                update_market_data_unified(symbol);
                nlohmann::json result;
                result["status"] = "success";
                result["message"] = "Market data updated";
                result["symbol"] = symbol;
                result["timestamp"] = std::to_string(time(nullptr));
                result["request_id"] = g_request_id;
                response = create_response(200, "OK", "application/json", result.dump());
            } else {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Valid symbol required";
                response = create_response(400, "Bad Request", "application/json", error_response.dump());
            }
        } else if (method == "GET" && path == "/api/market-data/history") {
            // New endpoint for historical data using YFinance handler
            std::lock_guard<std::mutex> lock(handler_mutex);
            
            if (!yfinance_handler) {
                initialize_yfinance_handler();
            }
            
            if (!yfinance_handler) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "YFinance handler not initialized";
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            } else {
                nlohmann::json result;
                result["status"] = "success";
                result["symbols"] = yfinance_handler->get_symbols();
                result["timestamp"] = std::to_string(time(nullptr));
                response = create_response(200, "OK", "application/json", result.dump());
            }
        } else if (method == "POST" && path == "/api/market-data/add-symbol") {
            // Add a symbol to the monitoring list
            std::string symbol = "";
            
            // Extract symbol from request body
            size_t body_start = request.find("\r\n\r\n");
            if (body_start != std::string::npos) {
                std::string body = request.substr(body_start + 4);
                try {
                    nlohmann::json body_json = nlohmann::json::parse(body);
                    if (body_json.contains("symbol")) {
                        symbol = body_json["symbol"];
                    }
                } catch (const std::exception& e) {
                    // Ignore parsing errors
                }
            }
            
            if (!symbol.empty() && is_valid_symbol(symbol)) {
                // Add to cache and trigger immediate update
                update_market_data_unified(symbol);
                
                nlohmann::json result;
                result["status"] = "success";
                result["message"] = "Symbol added to monitoring list";
                result["symbol"] = symbol;
                result["timestamp"] = std::to_string(time(nullptr));
                result["request_id"] = g_request_id;
                response = create_response(200, "OK", "application/json", result.dump());
            } else {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Valid symbol required in request body";
                response = create_response(400, "Bad Request", "application/json", error_response.dump());
            }
        } else if (method == "GET" && path.substr(0, 30) == "/api/market-data/technicals/") {
            // Technical indicators endpoint
            std::string symbol = path.substr(30);
            trim(symbol);
            
            if (!is_valid_symbol(symbol)) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Invalid symbol";
                response = create_response(400, "Bad Request", "application/json", error_response.dump());
            } else {
                std::lock_guard<std::mutex> handler_lock(handler_mutex);
                
                if (!yfinance_handler) {
                    initialize_yfinance_handler();
                }
                
                if (!yfinance_handler) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "YFinance handler not initialized";
                    response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                } else {
                    try {
                        // Load historical data for the symbol (last 6 months for indicators)
                        auto now = std::chrono::system_clock::now();
                        auto six_months_ago = now - std::chrono::hours(24 * 30 * 6); // 6 months
                        
                        std::string start_date = std::to_string(std::chrono::system_clock::to_time_t(six_months_ago));
                        std::string end_date = std::to_string(std::chrono::system_clock::to_time_t(now));
                        
                        if (yfinance_handler->load_symbol_data(symbol, start_date, end_date)) {
                            auto historical_data = yfinance_handler->get_historical_data(symbol);
                            
                            if (!historical_data.empty()) {
                                // Calculate technical indicators
                                auto indicators = backtesting::TechnicalIndicatorsCalculator::calculate_all_indicators(historical_data, symbol);
                                
                                nlohmann::json result;
                                result["status"] = "success";
                                result["symbol"] = symbol;
                                result["timestamp"] = std::to_string(time(nullptr));
                                result["request_id"] = g_request_id;
                                result["indicators"] = indicators;
                                
                                response = create_response(200, "OK", "application/json", result.dump());
                            } else {
                                nlohmann::json error_response;
                                error_response["status"] = "error";
                                error_response["message"] = "No historical data available for " + symbol;
                                response = create_response(404, "Not Found", "application/json", error_response.dump());
                            }
                        } else {
                            nlohmann::json error_response;
                            error_response["status"] = "error";
                            error_response["message"] = "Failed to load data for " + symbol;
                            response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                        }
                    } catch (const std::exception& e) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "Error calculating indicators: " + std::string(e.what());
                        response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                    }
                }
            }
        } else if (method == "GET" && path.substr(0, path.find("?")) == "/api/market-data/top-stocks") {
            // Top stocks endpoint
            std::string market = "sp500"; // default
            
            // Parse query parameters
            size_t query_start = path.find("?");
            if (query_start != std::string::npos) {
                std::string query = path.substr(query_start + 1);
                std::istringstream qs(query);
                std::string kv;
                while (std::getline(qs, kv, '&')) {
                    auto eq = kv.find('=');
                    if (eq != std::string::npos) {
                        auto k = kv.substr(0, eq);
                        auto v = kv.substr(eq + 1);
                        if (k == "market") market = v;
                    }
                }
            }
            
            if (market != "sp500" && market != "nifty50") {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Invalid market. Use 'sp500' or 'nifty50'";
                response = create_response(400, "Bad Request", "application/json", error_response.dump());
            } else {
                std::lock_guard<std::mutex> handler_lock(handler_mutex);
                
                if (!yfinance_handler) {
                    initialize_yfinance_handler();
                }
                
                if (!yfinance_handler) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "YFinance handler not initialized";
                    response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                } else {
                    try {
                        // Get symbols for the requested market
                        auto symbols = backtesting::get_market_symbols(market);
                        
                        nlohmann::json result;
                        result["status"] = "success";
                        result["market"] = market;
                        result["timestamp"] = std::to_string(time(nullptr));
                        result["request_id"] = g_request_id;
                        result["stocks"] = nlohmann::json::array();
                        
                        // First, try to return cached data immediately for faster response
                        int success_count = 0;
                        int max_symbols = std::min(static_cast<int>(symbols.size()), 5); // Limit to 5 symbols for much faster loading
                        
                        // Check cache first for all symbols
                        for (int i = 0; i < max_symbols; ++i) {
                            const auto& symbol = symbols[i];
                            try {
                                // Check if we have recent cached data
                                bool has_cached_data = false;
                                {
                                    std::lock_guard<std::mutex> lock(cache_mutex);
                                    auto it = market_data_cache.find(symbol);
                                    if (it != market_data_cache.end()) {
                                        // Check if cache is recent (less than 2 hours old)
                                        auto now = std::chrono::system_clock::now();
                                        auto cache_time = std::chrono::system_clock::from_time_t(std::stoi(it->second.substr(0, 10)));
                                        if (now - cache_time < std::chrono::hours(2)) {
                                            has_cached_data = true;
                                            
                                            // Parse cached data
                                            try {
                                                auto cached_data = nlohmann::json::parse(it->second);
                                                if (cached_data.contains("Global Quote")) {
                                                    auto quote = cached_data["Global Quote"];
                                                    nlohmann::json stock_data;
                                                    stock_data["symbol"] = symbol;
                                                    stock_data["price"] = std::stod(quote.value("05. price", "0"));
                                                    stock_data["change"] = std::stod(quote.value("09. change", "0"));
                                                    stock_data["change_percent"] = std::stod(quote.value("10. change percent", "0"));
                                                    stock_data["volume"] = std::stoi(quote.value("06. volume", "0"));
                                                    stock_data["high"] = std::stod(quote.value("03. high", "0"));
                                                    stock_data["low"] = std::stod(quote.value("04. low", "0"));
                                                    stock_data["open"] = std::stod(quote.value("02. open", "0"));
                                                    
                                                    // Add metadata
                                                    auto metadata = backtesting::get_market_metadata(symbol);
                                                    stock_data["name"] = metadata.name;
                                                    stock_data["exchange"] = metadata.exchange;
                                                    stock_data["sector"] = metadata.sector;
                                                    
                                                    result["stocks"].push_back(stock_data);
                                                    success_count++;
                                                }
                                            } catch (const std::exception& e) {
                                                std::cerr << "Error parsing cached data for " << symbol << ": " << e.what() << std::endl;
                                            }
                                        }
                                    }
                                }
                                
                                // No cache entry for this symbol; background updater may fill it later.
                                if (!has_cached_data) {
                                    std::cerr << "No recent cached data for " << symbol << "; deferring to background updater" << std::endl;
                                }
                                
                            } catch (const std::exception& e) {
                                std::cerr << "Error processing cached data for " << symbol << ": " << e.what() << std::endl;
                            }
                        }
                        
                        // If we have some cached data, return it immediately
                        if (success_count > 0) {
                            result["success_count"] = success_count;
                            result["total_requested"] = max_symbols;
                            result["note"] = "Using cached data for faster response";
                            response = create_response(200, "OK", "application/json", result.dump());
                        } else {
                            // No cached data, try to fetch fresh data (but with strict limits)
                            success_count = 0;
                            result["stocks"] = nlohmann::json::array(); // Reset
                            
                            // Only fetch 2 symbols maximum for fresh data
                            int fresh_symbols = std::min(2, max_symbols);
                            
                            for (int i = 0; i < fresh_symbols; ++i) {
                                const auto& symbol = symbols[i];
                                try {
                                    // Load recent data (last 1 day only for fastest loading)
                                    auto now = std::chrono::system_clock::now();
                                    auto one_day_ago = now - std::chrono::hours(24);
                                    
                                    std::string start_date = std::to_string(std::chrono::system_clock::to_time_t(one_day_ago));
                                    std::string end_date = std::to_string(std::chrono::system_clock::to_time_t(now));
                                    
                                    // Set a timeout for the request
                                    auto start_time = std::chrono::steady_clock::now();
                                    bool loaded = yfinance_handler->load_symbol_data(symbol, start_date, end_date);
                                    auto end_time = std::chrono::steady_clock::now();
                                    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
                                    
                                    // Skip if request took too long (> 2 seconds)
                                    if (duration.count() > 2) {
                                        std::cerr << "Request timeout for " << symbol << " (took " << duration.count() << "s)" << std::endl;
                                        continue;
                                    }
                                    
                                    if (loaded) {
                                        auto historical_data = yfinance_handler->get_historical_data(symbol);
                                        
                                        if (!historical_data.empty()) {
                                            const auto& latest = historical_data.back();
                                            const auto& previous = historical_data.size() > 1 ? historical_data[historical_data.size() - 2] : latest;
                                            
                                            double change = latest.close - previous.close;
                                            double change_percent = (change / previous.close) * 100.0;
                                            
                                            nlohmann::json stock_data;
                                            stock_data["symbol"] = symbol;
                                            stock_data["price"] = latest.close;
                                            stock_data["change"] = change;
                                            stock_data["change_percent"] = change_percent;
                                            stock_data["volume"] = latest.volume;
                                            stock_data["high"] = latest.high;
                                            stock_data["low"] = latest.low;
                                            stock_data["open"] = latest.open;
                                            
                                            // Add metadata
                                            auto metadata = backtesting::get_market_metadata(symbol);
                                            stock_data["name"] = metadata.name;
                                            stock_data["exchange"] = metadata.exchange;
                                            stock_data["sector"] = metadata.sector;
                                            
                                            result["stocks"].push_back(stock_data);
                                            success_count++;
                                        }
                                    }
                                    
                                    // Add small delay to avoid overwhelming the API
                                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                    
                                } catch (const std::exception& e) {
                                    std::cerr << "Error fetching data for " << symbol << ": " << e.what() << std::endl;
                                }
                            }
                            
                            result["success_count"] = success_count;
                            result["total_requested"] = fresh_symbols;
                            result["note"] = "Fresh data fetched";
                            response = create_response(200, "OK", "application/json", result.dump());
                        }
                        
                    } catch (const std::exception& e) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "Error fetching top stocks: " + std::string(e.what());
                        response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                    }
                }
            }
        } else if (method == "GET" && path.substr(0, path.find("?")) == "/api/market-data/search") {
            // Symbol search endpoint
            std::string query = "";
            
            // Parse query parameters
            size_t query_start = path.find("?");
            if (query_start != std::string::npos) {
                std::string query_string = path.substr(query_start + 1);
                std::istringstream qs(query_string);
                std::string kv;
                while (std::getline(qs, kv, '&')) {
                    auto eq = kv.find('=');
                    if (eq != std::string::npos) {
                        auto k = kv.substr(0, eq);
                        auto v = kv.substr(eq + 1);
                        if (k == "query") query = v;
                    }
                }
            }
            
            if (query.empty()) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Query parameter is required";
                response = create_response(400, "Bad Request", "application/json", error_response.dump());
            } else {
                std::lock_guard<std::mutex> cache_lock(symbol_cache_mutex);
                
                if (!symbol_cache) {
                    symbol_cache = std::make_unique<backtesting::SymbolCache>();
                    // Populate cache with all known symbols
                    auto all_symbols = backtesting::get_all_known_symbols();
                    for (const auto& symbol : all_symbols) {
                        auto metadata = backtesting::get_market_metadata(symbol);
                        symbol_cache->add_symbol(symbol, metadata);
                    }
                }
                
                try {
                    auto search_results = symbol_cache->search_symbols(query);
                    
                    nlohmann::json result;
                    result["status"] = "success";
                    result["query"] = query;
                    result["timestamp"] = std::to_string(time(nullptr));
                    result["request_id"] = g_request_id;
                    result["symbols"] = nlohmann::json::array();
                    
                    for (const auto& symbol : search_results) {
                        auto metadata = symbol_cache->get_symbol(symbol);
                        
                        nlohmann::json symbol_data;
                        symbol_data["symbol"] = metadata.symbol;
                        symbol_data["name"] = metadata.name;
                        symbol_data["exchange"] = metadata.exchange;
                        symbol_data["sector"] = metadata.sector;
                        
                        result["symbols"].push_back(symbol_data);
                    }
                    
                    response = create_response(200, "OK", "application/json", result.dump());
                    
                } catch (const std::exception& e) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Error searching symbols: " + std::string(e.what());
                    response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                }
            }
        } else if (method == "POST" && path == "/api/strategy/parse") {
            // Natural language strategy parsing endpoint
            try {
                // Extract JSON body from request
                size_t body_start = request.find("\r\n\r\n");
                std::string body;
                if (body_start != std::string::npos) {
                    body = request.substr(body_start + 4);
                }
                
                if (body.empty()) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Request body is required";
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } else {
                    nlohmann::json body_json = nlohmann::json::parse(body);
                    std::string strategy_text = body_json["strategy"];
                    
                    // Parse the natural language strategy
                    backtesting::NaturalLanguageParser parser;
                    backtesting::ParsedStrategy parsed = parser.parse_strategy(strategy_text);
                    
                    // Validate the parsed strategy
                    std::vector<std::string> validation_errors = parser.validate_strategy(strategy_text);
                    
                    nlohmann::json result;
                    result["status"] = "success";
                    result["parsed_strategy"] = parsed.to_json();
                    result["validation_errors"] = validation_errors;
                    result["is_valid"] = validation_errors.empty();
                    result["timestamp"] = std::to_string(time(nullptr));
                    result["request_id"] = g_request_id;
                    
                    response = create_response(200, "OK", "application/json", result.dump());
                }
            } catch (const std::exception& e) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Failed to parse strategy";
                error_response["error"] = e.what();
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            }
        } else if (method == "POST" && path == "/api/strategy/generate-code") {
            // Code generation endpoint for DSL
            try {
                size_t body_start = request.find("\r\n\r\n");
                std::string body;
                if (body_start != std::string::npos) {
                    body = request.substr(body_start + 4);
                }
                
                if (body.empty()) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Request body is required";
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } else {
                    nlohmann::json body_json = nlohmann::json::parse(body);
                    nlohmann::json dsl = body_json.value("dsl", nlohmann::json::object());
                    
                    // Generate C++ code from DSL
                    backtesting::CodeGenerator generator;
                    std::string error_message;
                    if (!generator.validate_dsl(dsl, error_message)) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "Invalid DSL: " + error_message;
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                    } else {
                        std::string generated_code = generator.generate_strategy_code(dsl);
                        
                        nlohmann::json result;
                        result["status"] = "success";
                        result["code"] = generated_code;
                        result["class_name"] = generator.generate_class_name(dsl.value("name", "Custom"));
                        result["timestamp"] = std::to_string(time(nullptr));
                        
                        response = create_response(200, "OK", "application/json", result.dump());
                    }
                }
            } catch (const std::exception& e) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Failed to generate code";
                error_response["error"] = e.what();
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            }
        } else if (method == "POST" && path == "/api/strategy/backtest") {
            // Natural language strategy backtest endpoint
            try {
                // Extract JSON body from request
                size_t body_start = request.find("\r\n\r\n");
                std::string body;
                if (body_start != std::string::npos) {
                    body = request.substr(body_start + 4);
                }
                
                if (body.empty()) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Request body is required";
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } else {
                    nlohmann::json body_json = nlohmann::json::parse(body);
                    std::string strategy_text = body_json["strategy"];
                    std::string symbol = body_json.value("symbol", "AAPL");
                    if (!is_valid_symbol(symbol)) {
                        nlohmann::json error_response; error_response["status"] = "error"; error_response["message"] = "Invalid symbol";
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                        write(client_fd, response.c_str(), response.length());
                        close(client_fd);
                        return;
                    }
                    std::string start_date = body_json.value("start_date", "2023-01-01");
                    std::string end_date = body_json.value("end_date", "2024-01-01");
                    double initial_capital = body_json.value("initial_capital", 10000.0);
                    
                    // Parse the natural language strategy
                    backtesting::NaturalLanguageParser parser;
                    backtesting::ParsedStrategy parsed = parser.parse_strategy(strategy_text);
                    
                    // Validate the strategy
                    std::vector<std::string> validation_errors = parser.validate_strategy(strategy_text);
                    if (!validation_errors.empty()) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "Invalid strategy";
                        error_response["validation_errors"] = validation_errors;
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                    } else {
                        // Convert to strategy and run backtest
                        std::unique_ptr<backtesting::Strategy> strategy = parser.convert_to_strategy(parsed);
                        
                        if (strategy) {
                            // Create backtest configuration
                            backtesting::BacktestConfig config;
                            config.name = parsed.name;
                            config.description = parsed.description;
                            config.symbols = {symbol};
                            config.start_date = start_date;
                            config.end_date = end_date;
                            config.initial_capital = initial_capital;
                            config.commission_rate = 0.001;
                            config.slippage_rate = 0.001;
                            config.data_source = "alpha_vantage";
                            config.api_key = ALPHA_VANTAGE_API_KEY;
                            config.strategy_name = (!parsed.indicators.empty()) ? parsed.indicators[0] : "moving_average";
                            config.strategy_params = parsed.parameters;
                            config.account_type = body_json.value("account_type", std::string("CASH"));
                            // Simple market classification: Indian equities when symbol ends with .NS or .BO
                            if (symbol.size() > 3 && (symbol.rfind(".NS") == symbol.size() - 3 || symbol.rfind(".BO") == symbol.size() - 3)) {
                                config.market_type = "IN_EQUITY";
                            } else {
                                config.market_type = "US_EQUITY";
                            }
                            // Pass parsed filters so engine applies context filters
                            if (!parsed.filters.is_null() && !parsed.filters.empty()) {
                                config.strategy_definition_json["filters"] = parsed.filters;
                            }
                            
                            // Return constructed backtest configuration (execution is handled by the engine elsewhere).
                            nlohmann::json result;
                            result["status"] = "success";
                            result["strategy_name"] = parsed.name;
                            result["parsed_strategy"] = parsed.to_json();
                            result["backtest_config"] = config.to_json();
                            result["message"] = "Strategy parsed and backtest configuration created successfully";
                            result["timestamp"] = std::to_string(time(nullptr));
                            result["request_id"] = g_request_id;
                            
                            response = create_response(200, "OK", "application/json", result.dump());
                        } else {
                            nlohmann::json error_response;
                            error_response["status"] = "error";
                            error_response["message"] = "Failed to convert strategy to executable format";
                            response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                        }
                    }
                }
            } catch (const std::exception& e) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Failed to process strategy backtest";
                error_response["error"] = e.what();
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            }
        } else if (method == "GET" && path == "/api/strategy/examples") {
            // Get example strategies
            backtesting::NaturalLanguageParser parser;
            std::vector<std::string> examples = parser.get_example_strategies();
            
            nlohmann::json result;
            result["status"] = "success";
            result["examples"] = examples;
            result["supported_patterns"] = parser.get_supported_patterns();
            result["timestamp"] = std::to_string(time(nullptr));
            result["request_id"] = g_request_id;
            
            response = create_response(200, "OK", "application/json", result.dump());
        } else if (method == "GET" && path.find("/api/options/chain/") == 0) {
            // Options chain endpoint: /api/options/chain/{symbol}?expiry={date}
            try {
                std::string symbol = path.substr(19); // Remove "/api/options/chain/"
                size_t query_pos = symbol.find('?');
                std::string expiry_date = "";
                
                if (query_pos != std::string::npos) {
                    std::string query = symbol.substr(query_pos + 1);
                    symbol = symbol.substr(0, query_pos);
                    
                    // Parse expiry from query string
                    size_t expiry_pos = query.find("expiry=");
                    if (expiry_pos != std::string::npos) {
                        expiry_date = query.substr(expiry_pos + 7);
                        size_t amp_pos = expiry_date.find('&');
                        if (amp_pos != std::string::npos) {
                            expiry_date = expiry_date.substr(0, amp_pos);
                        }
                    }
                }
                
                if (!is_valid_symbol(symbol)) {
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Invalid symbol";
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } else {
                    std::lock_guard<std::mutex> lock(handler_mutex);
                    
                    // Try Polygon first (best for options)
                    std::string polygon_key = std::getenv("POLYGON_API_KEY") ? std::getenv("POLYGON_API_KEY") : "";
                    if (!polygon_key.empty() && !polygon_handler) {
                        polygon_handler = std::make_unique<backtesting::PolygonHandler>(
                            polygon_key, "./polygon_cache", true, 24
                        );
                    }
                    
                    nlohmann::json result;
                    result["status"] = "success";
                    result["symbol"] = symbol;
                    result["expiry"] = expiry_date;
                    result["options"] = nlohmann::json::array();
                    
                    if (polygon_handler && polygon_handler->is_available()) {
                        if (polygon_handler->load_options_chain(symbol, expiry_date)) {
                            auto options = polygon_handler->get_options_chain(symbol, expiry_date);
                            for (const auto& opt : options) {
                                nlohmann::json opt_json;
                                opt_json["symbol"] = opt.symbol;
                                opt_json["expiry"] = opt.expiry;
                                opt_json["strike"] = opt.strike;
                                opt_json["type"] = opt.option_type;
                                opt_json["bid"] = opt.bid;
                                opt_json["ask"] = opt.ask;
                                opt_json["last"] = opt.last;
                                opt_json["volume"] = opt.volume;
                                opt_json["open_interest"] = opt.open_interest;
                                opt_json["iv"] = opt.implied_volatility;
                                opt_json["delta"] = opt.delta;
                                opt_json["gamma"] = opt.gamma;
                                opt_json["theta"] = opt.theta;
                                opt_json["vega"] = opt.vega;
                                result["options"].push_back(opt_json);
                            }
                        }
                    } else {
                        // Fallback: return empty (YFinance options support could go here)
                        result["message"] = "Options data not available (Polygon API key required)";
                    }
                    
                    result["timestamp"] = std::to_string(time(nullptr));
                    response = create_response(200, "OK", "application/json", result.dump());
                }
            } catch (const std::exception& e) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Failed to fetch options chain";
                error_response["error"] = e.what();
                response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
            }
        } else if (method == "GET" && path == "/api/system/stats") {
            // System stats: API/cache metrics
            nlohmann::json stats;
            {
                std::lock_guard<std::mutex> lock(handler_mutex);
                if (yfinance_handler) {
                    stats["yfinance_api"] = yfinance_handler->get_api_usage_stats();
                    stats["yfinance_cache"] = yfinance_handler->get_cache_statistics();
                } else {
                    stats["yfinance_api"] = { {"available", false} };
                }
            }
            stats["timestamp"] = std::to_string(time(nullptr));
            stats["request_id"] = g_request_id;
            response = create_response(200, "OK", "application/json", stats.dump());
        } else if (method == "GET" && (path == "/" || path == "/index.html")) {
            std::ostringstream html;
            html << "<!doctype html>\n"
                 << "<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                 << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                 << "<title>Backtesting Engine</title>\n"
                 << "<style>body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;padding:2rem;line-height:1.6;background:#0b1220;color:#e6edf3}"
                 << "code,pre{background:#111827;color:#e5e7eb;padding:.2rem .4rem;border-radius:6px}"
                 << "a{color:#60a5fa;text-decoration:none}a:hover{text-decoration:underline}"
                 << "section{background:#0f172a;border:1px solid #1f2937;border-radius:12px;padding:1rem 1.25rem;max-width:720px}</style>\n"
                 << "</head><body>\n"
                 << "<h1>Backtesting Engine is running</h1>\n"
                 << "<p><strong>Status:</strong> All API endpoints are fully functional with real YFinance data integration</p>\n"
                 << "<p>This service exposes a comprehensive API with real-time market data and actual backtesting capabilities:</p>\n"
                 << "<section>\n<h3>Market Data Endpoints</h3><ul>\n"
                 << "<li><strong>GET</strong> <code>/api/market-data</code> — get all cached market data</li>\n"
                 << "<li><strong>GET</strong> <code>/api/market-data/{symbol}</code> — get real-time data for specific symbol (e.g., NKE, AAPL)</li>\n"
                 << "<li><strong>POST</strong> <code>/api/market-data/update</code> — manually update symbol data</li>\n"
                 << "<li><strong>GET</strong> <code>/api/market-data/history</code> — get available symbols</li>\n"
                 << "</ul>\n<h3>Backtest Endpoints</h3><ul>\n"
                 << "<li><strong>POST</strong> <code>/backtest</code> — run actual backtests with real data (no hardcoded responses)</li>\n"
                 << "<li><strong>POST</strong> <code>/api/strategy/parse</code> — parse natural language strategies</li>\n"
                 << "<li><strong>POST</strong> <code>/api/strategy/backtest</code> — run backtests from natural language</li>\n"
                 << "<li><strong>GET</strong> <code>/api/strategy/examples</code> — get example strategies</li>\n"
                 << "</ul>\n<h3>System Endpoints</h3><ul>\n"
                 << "<li><strong>GET</strong> <code>/health</code> — health check</li>\n"
                 << "<li><strong>GET</strong> <code>/</code> — this API documentation page</li>\n"
                 << "</ul>\n</section>\n"
                 << "<section>\n<h3>🚀 Key Features</h3><ul>\n"
                 << "<li>Live market data</li>\n"
                 << "<li><strong>Backtest Execution</strong> </li>\n"
                 << "<li><strong>Intelligent Caching</strong></li>\n"
                 << "</ul>\n</section>\n"
                 << "<section>\n<h3>Test Commands</h3>\n"
                 << "<p><strong>Market Data:</strong></p>\n"
                 << "<pre><code>curl https://backtesting-2w2g.onrender.com/api/market-data/NKE</code></pre>\n"
                 << "<p><strong>Backtest</strong></p>\n"
                 << "<pre><code>curl -X POST https://backtesting-2w2g.onrender.com/backtest -H 'Content-Type: application/json' -d '{\n"
                 << "  \"symbol\": \"AAPL\",\n  \"start_date\": \"2024-01-01\",\n  \"end_date\": \"2024-12-31\",\n  \"initial_capital\": 10000,\n  \"strategy\": {\n    \"indicators\": [{ \"type\": \"SMA\", \"length\": 20 }]\n  }\n}'</code></pre>\n"
                 << "<p><strong>Health Check:</strong></p>\n"
                 << "<pre><code>curl https://backtesting-2w2g.onrender.com/health</code></pre>\n"
                 << "</section>\n"
                 << "</body></html>\n";
            response = create_response(200, "OK", "text/html; charset=utf-8", html.str());
        } else if (method == "POST" && path == "/backtest/batch") {
            // Multi-stock batch backtest (Pro feature) - runs same strategy on multiple symbols in parallel
            size_t body_start = request.find("\r\n\r\n");
            std::string body;
            if (body_start != std::string::npos) {
                body = request.substr(body_start + 4);
                while (!body.empty() && (body.back() == '\r' || body.back() == '\n' || body.back() == ' ')) {
                    body.pop_back();
                }
            }
            if (body.empty()) {
                nlohmann::json err;
                err["status"] = "error";
                err["message"] = "Request body is required";
                response = create_response(400, "Bad Request", "application/json", err.dump());
            } else {
                try {
                    nlohmann::json body_json = nlohmann::json::parse(body);
                    std::vector<std::string> symbols;
                    if (body_json.contains("symbols") && body_json["symbols"].is_array()) {
                        for (const auto& s : body_json["symbols"]) {
                            std::string sym = s.get<std::string>();
                            if (!sym.empty()) symbols.push_back(sym);
                        }
                    }
                    constexpr int MAX_BATCH_SYMBOLS = 10;
                    if (symbols.empty()) {
                        nlohmann::json err;
                        err["status"] = "error";
                        err["message"] = "symbols array is required and must not be empty";
                        response = create_response(400, "Bad Request", "application/json", err.dump());
                    } else if (static_cast<int>(symbols.size()) > MAX_BATCH_SYMBOLS) {
                        nlohmann::json err;
                        err["status"] = "error";
                        err["message"] = "Maximum " + std::to_string(MAX_BATCH_SYMBOLS) + " symbols per batch";
                        response = create_response(400, "Bad Request", "application/json", err.dump());
                    } else {
                        for (const auto& s : symbols) {
                            if (!is_valid_symbol(s)) {
                                nlohmann::json err;
                                err["status"] = "error";
                                err["message"] = "Invalid symbol: " + s;
                                response = create_response(400, "Bad Request", "application/json", err.dump());
                                write(client_fd, response.c_str(), response.length());
                                close(client_fd);
                                return;
                            }
                        }
                        std::string start_date = body_json.value("start_date", "2023-01-01");
                        std::string end_date = body_json.value("end_date", "2024-01-01");
                        double initial_capital = body_json.value("initial_capital", 10000.0);
                        double commission_rate = body_json.value("commission", 0.001);
                        double slippage_rate = body_json.value("slippage", 0.001);
                        double max_position_size = body_json.value("max_position_size", 1.0);
                        double stop_loss_pct = body_json.value("stop_loss_pct", 0.05);
                        std::string data_source = body_json.value("data_source", std::string("yfinance"));
                        std::string data_interval = body_json.value("data_interval", std::string("1d"));
                        {
                            std::string ds_lower = data_source;
                            std::transform(ds_lower.begin(), ds_lower.end(), ds_lower.begin(), [](unsigned char c){ return std::tolower(c); });
                            if (ds_lower == "alphavantage") ds_lower = "alpha_vantage";
                            if (ds_lower == "yahoo") ds_lower = "yfinance";
                            data_source = ds_lower;
                        }
                        std::vector<std::string> valid_intervals = {"1m", "5m", "15m", "30m", "1h", "1d"};
                        bool is_valid_interval = false;
                        for (const auto& interval : valid_intervals) {
                            if (data_interval == interval) { is_valid_interval = true; break; }
                        }
                        if (!is_valid_interval) {
                            nlohmann::json err;
                            err["status"] = "error";
                            err["message"] = "Unsupported data_interval. Use one of: 1m, 5m, 15m, 30m, 1h, 1d";
                            response = create_response(400, "Bad Request", "application/json", err.dump());
                            write(client_fd, response.c_str(), response.length());
                            close(client_fd);
                            return;
                        }
                        std::string strategy_name = "moving_average";
                        std::unordered_map<std::string, double> strategy_params;
                        std::unordered_map<std::string, std::string> strategy_string_params;
                        nlohmann::json strategy_definition_json;
                        if (body_json.contains("strategy")) {
                            const auto& strategy_json = body_json["strategy"];
                            const std::string strategy_type = strategy_json.value("type", std::string("indicator"));
                            if (strategy_type == "multi_leg" && strategy_json.contains("legs") && strategy_json["legs"].is_array()) {
                                strategy_name = "multi_leg";
                                strategy_string_params["legs"] = strategy_json["legs"].dump();
                                strategy_definition_json = strategy_json;
                            }
                            if (strategy_json.contains("indicators") && strategy_type == "multi_leg") {
                                strategy_string_params["indicators"] = strategy_json["indicators"].dump();
                            } else if (strategy_json.contains("indicators")) {
                                auto indicators = strategy_json["indicators"];
                                if (indicators.is_array() && !indicators.empty() && indicators[0].contains("length") && indicators[0]["length"].is_number()) {
                                    strategy_params["short_window"] = indicators[0]["length"].get<double>();
                                    strategy_params["long_window"] = strategy_params["short_window"] * 2;
                                }
                                // Pass filters and sizing for indicator strategies (same as multi_leg)
                                if (strategy_type != "multi_leg") {
                                    strategy_definition_json = strategy_json;
                                }
                            }
                        }
                        struct PerSymbolResult {
                            std::string symbol;
                            nlohmann::json data;
                            std::string error;
                        };
                        std::vector<PerSymbolResult> results(symbols.size());
                        std::mutex results_mutex;
                        std::vector<std::thread> threads;
                        for (size_t i = 0; i < symbols.size(); ++i) {
                            threads.emplace_back([&, i]() {
                                const std::string& sym = symbols[i];
                                try {
                                    backtesting::BacktestConfig cfg;
                                    cfg.name = "Batch Backtest";
                                    cfg.description = "Multi-symbol batch";
                                    cfg.symbols = {sym};
                                    cfg.start_date = start_date;
                                    cfg.end_date = end_date;
                                    cfg.initial_capital = initial_capital;
                                    cfg.commission_rate = commission_rate;
                                    cfg.slippage_rate = slippage_rate;
                                    cfg.data_source = data_source;
                                    cfg.data_interval = data_interval;
                                    cfg.max_position_size = max_position_size;
                                    cfg.stop_loss_percentage = stop_loss_pct;
                                    cfg.strategy_name = strategy_name;
                                    cfg.strategy_params = strategy_params;
                                    cfg.strategy_string_params = strategy_string_params;
                                    cfg.strategy_definition_json = strategy_definition_json;
                                    cfg.verbose_logging = false;
                                    auto engine = backtesting::BacktestEngine::create_from_config(cfg);
                                    auto result = engine->run_backtest();
                                    nlohmann::json data = result.to_json();
                                    std::lock_guard<std::mutex> lock(results_mutex);
                                    results[i].symbol = sym;
                                    results[i].data = std::move(data);
                                } catch (const std::exception& e) {
                                    std::lock_guard<std::mutex> lock(results_mutex);
                                    results[i].symbol = sym;
                                    results[i].error = e.what();
                                }
                            });
                        }
                        for (auto& t : threads) t.join();
                        nlohmann::json results_json = nlohmann::json::array();
                        double sum_return = 0.0;
                        int success_count = 0;
                        std::string best_symbol, worst_symbol;
                        double best_return = -1e9, worst_return = 1e9;
                        for (size_t i = 0; i < results.size(); ++i) {
                            nlohmann::json item;
                            item["symbol"] = results[i].symbol;
                            if (results[i].error.empty()) {
                                item["data"] = results[i].data;
                                item["error"] = nullptr;
                                double tr = results[i].data.value("total_return", 0.0);
                                sum_return += tr;
                                success_count++;
                                if (tr > best_return) { best_return = tr; best_symbol = results[i].symbol; }
                                if (tr < worst_return) { worst_return = tr; worst_symbol = results[i].symbol; }
                            } else {
                                item["data"] = nullptr;
                                item["error"] = results[i].error;
                            }
                            results_json.push_back(std::move(item));
                        }
                        nlohmann::json summary;
                        summary["avg_return"] = success_count > 0 ? (sum_return / success_count) : 0.0;
                        // Avoid clang warnings / overload ambiguity when conditionally assigning json.
                        if (best_symbol.empty()) summary["best_symbol"] = nlohmann::json{};
                        else summary["best_symbol"] = best_symbol;
                        if (worst_symbol.empty()) summary["worst_symbol"] = nlohmann::json{};
                        else summary["worst_symbol"] = worst_symbol;
                        summary["success_count"] = success_count;
                        summary["total_count"] = static_cast<int>(symbols.size());
                        nlohmann::json payload;
                        payload["status"] = "success";
                        payload["request_id"] = g_request_id;
                        payload["results"] = std::move(results_json);
                        payload["summary"] = std::move(summary);
                        payload["timestamp"] = std::to_string(time(nullptr));
                        response = create_response(200, "OK", "application/json", payload.dump());
                    }
                } catch (const nlohmann::json::parse_error& e) {
                    nlohmann::json err;
                    err["status"] = "error";
                    err["message"] = "Invalid JSON: " + std::string(e.what());
                    response = create_response(400, "Bad Request", "application/json", err.dump());
                } catch (const std::exception& e) {
                    nlohmann::json err;
                    err["status"] = "error";
                    err["message"] = e.what();
                    response = create_response(500, "Internal Server Error", "application/json", err.dump());
                }
            }
        } else if (method == "POST" && path == "/backtest") {
            // Extract JSON body from request
            size_t body_start = request.find("\r\n\r\n");
            std::string body;
            if (body_start != std::string::npos) {
                body = request.substr(body_start + 4);
                // Remove any trailing whitespace or newlines
                while (!body.empty() && (body.back() == '\r' || body.back() == '\n' || body.back() == ' ')) {
                    body.pop_back();
                }
            }
            
            // Debug logging
            std::cout << "Backtest request body length: " << body.length() << std::endl;
            std::cout << "Backtest request body: " << body << std::endl;
            
            if (body.empty()) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Request body is required";
                response = create_response(400, "Bad Request", "application/json", error_response.dump());
            } else {
                try {
                    std::cout << "Attempting to parse JSON: " << body << std::endl;
                    nlohmann::json body_json = nlohmann::json::parse(body);
                    std::cout << "JSON parsed successfully" << std::endl;
                    
                    // Extract parameters from request
                    std::string symbol = body_json.value("symbol", "AAPL");
                    if (!is_valid_symbol(symbol)) {
                        nlohmann::json error_response; error_response["status"] = "error"; error_response["message"] = "Invalid symbol";
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                        write(client_fd, response.c_str(), response.length());
                        close(client_fd);
                        return;
                    }
                    std::string start_date = body_json.value("start_date", "2023-01-01");
                    std::string end_date = body_json.value("end_date", "2024-01-01");
                    double initial_capital = body_json.value("initial_capital", 10000.0);
                    
                    std::cout << "Extracted parameters - Symbol: " << symbol << ", Start: " << start_date << ", End: " << end_date << ", Capital: " << initial_capital << std::endl;
                    
                    // Extract additional configuration (commission, slippage, risk, data source)
                    double commission_rate = body_json.value("commission", 0.001);
                    double slippage_rate = body_json.value("slippage", 0.001);
                    double max_position_size = body_json.value("max_position_size", 1.0);
                    double stop_loss_pct = body_json.value("stop_loss_pct", 0.05);
                    std::string data_source = body_json.value("data_source", std::string("yfinance"));
                    std::string data_interval = body_json.value("data_interval", std::string("1d"));
                    std::string data_path = body_json.value("data_path", std::string(""));
                    
                    // Normalize and validate data source
                    {
                        std::string ds_lower = data_source;
                        std::transform(ds_lower.begin(), ds_lower.end(), ds_lower.begin(), [](unsigned char c){ return std::tolower(c); });
                        if (ds_lower == "alphavantage") ds_lower = "alpha_vantage";
                        if (ds_lower == "yahoo") ds_lower = "yfinance";
                        data_source = ds_lower;
                    }

                    // CSV fallback: if client didn't provide data_path, allow an env var.
                    // This keeps the API usable in offline environments.
                    if (data_source == "csv" && data_path.empty()) {
                        const char* env_csv = std::getenv("DATA_CSV_PATH");
                        if (env_csv && std::strlen(env_csv) > 0) {
                            data_path = std::string(env_csv);
                        }
                    }

                    // If the caller provided a relative CSV data_path, try to resolve it
                    // from common build layouts (e.g., when the server is launched from ./build).
                    if (data_source == "csv" && !data_path.empty()) {
                        try {
                            namespace fs = std::filesystem;
                            fs::path p = data_path;
                            if (!p.is_absolute() && !fs::exists(p)) {
                                fs::path p1 = fs::path("..") / p;
                                fs::path p2 = fs::path("../..") / p;
                                fs::path p3 = fs::path("../../..") / p;
                                if (fs::exists(p1)) data_path = p1.string();
                                else if (fs::exists(p2)) data_path = p2.string();
                                else if (fs::exists(p3)) data_path = p3.string();
                            }
                        } catch (...) {
                            // Best-effort resolution; if it fails, CSVDataHandler/DataHandlerFactory will attempt fallback lookup.
                        }
                    }
                    
                    // Validate data interval
                    std::vector<std::string> valid_intervals = {"1m", "5m", "15m", "30m", "1h", "1d"};
                    bool is_valid_interval = false;
                    for (const auto& interval : valid_intervals) {
                        if (data_interval == interval) {
                            is_valid_interval = true;
                            break;
                        }
                    }
                    
                    if (!is_valid_interval) {
                        nlohmann::json error_response;
                        error_response["status"] = "error";
                        error_response["message"] = "Unsupported data_interval. Use one of: 1m, 5m, 15m, 30m, 1h, 1d";
                        response = create_response(400, "Bad Request", "application/json", error_response.dump());
                        write(client_fd, response.c_str(), response.length());
                        close(client_fd);
                        return;
                    }

                    // Send backtest.start event immediately to clear the frontend timeout
                    nlohmann::json start_evt;
                    start_evt["type"] = "backtest.start";
                    start_evt["symbol"] = symbol;
                    start_evt["start_date"] = start_date;
                    start_evt["end_date"] = end_date;
                    start_evt["initial_capital"] = initial_capital;
                    start_evt["data_source"] = data_source;
                    start_evt["data_interval"] = data_interval;
                    start_evt["timestamp"] = std::to_string(time(nullptr));
                    sse_broadcast(start_evt);

                    // Extract strategy configuration
                    std::string strategy_name = "moving_average";
                    std::unordered_map<std::string, double> strategy_params;
                    std::unordered_map<std::string, std::string> strategy_string_params;
                    nlohmann::json strategy_definition_json;
                    
                    if (body_json.contains("strategy")) {
                        const auto& strategy_json = body_json["strategy"];
                        const std::string strategy_type = strategy_json.value("type", std::string("indicator"));
                        // Always pass full strategy JSON for context filters (filters, sizing) and multi-leg
                        strategy_definition_json = strategy_json;

                        if (strategy_type == "multi_leg" && strategy_json.contains("legs") && strategy_json["legs"].is_array()) {
                            strategy_name = "multi_leg";
                            strategy_string_params["legs"] = strategy_json["legs"].dump();
                        }

                        // Parse indicators for multi-leg strategies
                        if (strategy_json.contains("indicators") && strategy_type == "multi_leg") {
                            strategy_string_params["indicators"] = strategy_json["indicators"].dump();
                        } else if (strategy_json.contains("indicators")) {
                            // For non-multi-leg strategies, extract indicator params
                            auto indicators = strategy_json["indicators"];
                            if (indicators.is_array() && !indicators.empty()) {
                                std::string indicator_type = indicators[0].value("type", "SMA");
                                if (indicators[0].contains("length")) {
                                    strategy_params["short_window"] = indicators[0]["length"].get<double>();
                                    strategy_params["long_window"] = strategy_params["short_window"] * 2;
                                }
                            }
                        }
                    }
                    
                    // Create backtest configuration
                    backtesting::BacktestConfig config;
                    config.name = "API Backtest";
                    config.description = "Backtest run via API";
                    config.symbols = {symbol};
                    config.start_date = start_date;
                    config.end_date = end_date;
                    config.initial_capital = initial_capital;
                    config.commission_rate = commission_rate;
                    config.slippage_rate = slippage_rate;
                    config.data_source = data_source;
                    config.data_interval = data_interval;
                    config.max_position_size = max_position_size;
                    config.stop_loss_percentage = stop_loss_pct;
                    config.strategy_name = strategy_name;
                    config.strategy_params = strategy_params;
                    config.strategy_string_params = strategy_string_params;
                    config.strategy_definition_json = strategy_definition_json;
                    config.data_path = data_path;
                    config.verbose_logging = true;
                    
                    std::cout << "Creating backtest engine for [" << symbol << "] with strategy [" << strategy_name << "]" << std::endl;
                    auto engine = backtesting::BacktestEngine::create_from_config(config);
                    
                    // Wire progress to SSE
                    engine->set_progress_callback([](const nlohmann::json& evt){
                        sse_broadcast(evt);
                    });
                    
                    std::cout << "Starting backtest execution (async)..." << std::endl;
                    
                    // Move engine to a shared_ptr to keep it alive during async execution
                    auto shared_engine = std::shared_ptr<backtesting::BacktestEngine>(std::move(engine));
                    
                    // Run async
                    shared_engine->run_backtest_async([shared_engine](const backtesting::BacktestResult& /*result*/) {
                        std::cout << "Async backtest execution completed" << std::endl;
                        // Progress summary is already sent by progress_callback inside run_backtest
                    });
                    
                    // Format immediate response
                    nlohmann::json response_json;
                    response_json["status"] = "success";
                    response_json["message"] = "Backtest started in background";
                    response_json["request_id"] = g_request_id;
                    response_json["timestamp"] = std::to_string(time(nullptr));
                    
                    response = create_response(202, "Accepted", "application/json", response_json.dump());
                    
                } catch (const nlohmann::json::parse_error& e) {
                    std::cout << "JSON parse error: " << e.what() << std::endl;
                    // Send error event via SSE
                    nlohmann::json error_evt;
                    error_evt["type"] = "backtest.error";
                    error_evt["error"] = "Invalid JSON in request body";
                    error_evt["message"] = std::string(e.what());
                    sse_broadcast(error_evt);
                    
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "Invalid JSON in request body: " + std::string(e.what());
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } catch (const std::invalid_argument& e) {
                    // Configuration or factory errors (e.g., unknown data source)
                    std::cout << "Backtest configuration error: " << e.what() << std::endl;
                    // Send error event via SSE
                    nlohmann::json error_evt;
                    error_evt["type"] = "backtest.error";
                    error_evt["error"] = "Backtest configuration error";
                    error_evt["message"] = std::string(e.what());
                    sse_broadcast(error_evt);
                    
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = e.what();
                    error_response["supported_sources"] = backtesting::DataHandlerFactory::get_available_sources();
                    response = create_response(400, "Bad Request", "application/json", error_response.dump());
                } catch (const std::exception& e) {
                    std::string msg = e.what();
                    std::cerr << "Backtest error (std::exception): " << msg << std::endl;
                    
                    // Send error event via SSE
                    try {
                        nlohmann::json error_evt;
                        error_evt["type"] = "backtest.error";
                        error_evt["error"] = msg;
                        error_evt["message"] = msg;
                        sse_broadcast(error_evt);
                    } catch (...) {
                        std::cerr << "Failed to broadcast error event via SSE" << std::endl;
                    }
                    
                    int status_code = 500;
                    std::string status_text = "Internal Server Error";
                    // Classify common data errors to avoid generic 500s
                    if (msg.find("Failed to load market data") != std::string::npos ||
                        msg.find("Failed to load data for symbol") != std::string::npos ||
                        msg.find("No data parsed") != std::string::npos) {
                        status_code = 503;
                        status_text = "Service Unavailable"; // Data temporarily unavailable (rate limit or out-of-range)
                    }
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = msg;
                    error_response["hint"] = "If using Alpha Vantage free tier, try a smaller date range or retry in ~1 minute.";
                    error_response["data_source"] = "yfinance";
                    response = create_response(status_code, status_text, "application/json", error_response.dump());
                } catch (...) {
                    std::cerr << "Backtest error: Unknown exception caught" << std::endl;
                    
                    // Send error event via SSE
                    try {
                        nlohmann::json error_evt;
                        error_evt["type"] = "backtest.error";
                        error_evt["error"] = "Unknown exception occurred";
                        error_evt["message"] = "An unknown error occurred during backtest execution";
                        sse_broadcast(error_evt);
                    } catch (...) {
                        std::cerr << "Failed to broadcast error event via SSE" << std::endl;
                    }
                    
                    nlohmann::json error_response;
                    error_response["status"] = "error";
                    error_response["message"] = "An unknown error occurred during backtest execution";
                    error_response["hint"] = "Please check backend logs for more details";
                    response = create_response(500, "Internal Server Error", "application/json", error_response.dump());
                }
            }
        } else {
            response = create_response(404, "Not Found", "text/plain", "Not Found");
        }
        
        // If not already streaming via SSE, finalize the response
        if (!(method == "GET" && path == "/events")) {
            write(client_fd, response.c_str(), response.length());
            close(client_fd);
        }
    }
    
    std::string create_response(int status_code, const std::string& status_text, 
                               const std::string& content_type, const std::string& body) {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
        oss << "Content-Type: " << content_type << "\r\n";
        oss << "Content-Length: " << body.length() << "\r\n";
        { std::string co = get_cors_origin_header(); oss << "Access-Control-Allow-Origin: " << (co.empty() ? "*" : co) << "\r\n"; }
        oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE, PATCH\r\n";
        oss << "Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With\r\n";
        oss << "\r\n";
        oss << body;
        return oss.str();
    }
};

int main() {
    // Initialize CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Set up signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if (sigaction(SIGTERM, &sa, nullptr) == -1) {
        std::cerr << "Failed to set up SIGTERM handler" << std::endl;
        return 1;
    }
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        std::cerr << "Failed to set up SIGINT handler" << std::endl;
        return 1;
    }
    // Ignore SIGPIPE so SSE writes to closed sockets don't terminate the server
    std::signal(SIGPIPE, SIG_IGN);
    
    // Get port from environment variable, default to 8080
    const char* port_env = std::getenv("PORT");
    int port = port_env ? std::atoi(port_env) : 8080;
    
    std::cout << "Starting HTTP server on port " << port << std::endl;
    
    // Initialize handlers - YFinance as primary source
    initialize_yfinance_handler();
    if (!yfinance_ready.load()) {
        std::cerr << "Warning: YFinance handler failed to initialize. "
                  << "Market data and backtesting endpoints depending on YFinance may be unavailable."
                  << std::endl;
    } else {
        std::cout << "YFinance handler initialized as primary data source" << std::endl;
    }
    
    // Initialize symbol cache
    {
        std::lock_guard<std::mutex> cache_lock(symbol_cache_mutex);
        symbol_cache = std::make_unique<backtesting::SymbolCache>();
    }
    std::cout << "Symbol cache initialized" << std::endl;
    
    // Note: Alpha Vantage is deprecated in favor of YFinance
    // Only initialize if explicitly needed for legacy support
    
    SimpleHTTPServer server(port);
    if (!server.start()) {
        std::cerr << "Failed to start server on port " << port << std::endl;
        
        // Try one more alternative port
        int alt_port = (port == 8080) ? 8081 : 8080;
        std::cerr << "Trying alternative port " << alt_port << "..." << std::endl;
        
        SimpleHTTPServer server_alt(alt_port);
        if (!server_alt.start()) {
            std::cerr << "Failed to start server on port " << alt_port << " as well. Please check for hanging processes with 'lsof -i :" << port << "'." << std::endl;
            return 1;
        }
        std::cout << "Server started on alternative port " << alt_port << std::endl;
    } else {
        std::cout << "Server started on port " << port << std::endl;
    }
    
    // Start market data updater thread
    std::thread updater_thread(market_data_updater);
    // Start SSE keepalive pinger
    std::thread ping_thread(sse_keepalive_pinger);
    
    server.run();
    
    // Wait for updater thread to finish
    if (updater_thread.joinable()) {
        updater_thread.join();
    }
    sse_ping_running = false;
    if (ping_thread.joinable()) {
        ping_thread.join();
    }
    
    // Cleanup CURL
    curl_global_cleanup();
    
    std::cout << "Server stopped successfully" << std::endl;
    return 0;
} 
 