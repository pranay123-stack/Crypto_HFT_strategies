#include <iostream>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <cmath>
#include <curl/curl.h>
#include <json/json.h>
#include <fstream>

class Logger {
public:
    static void log(const std::string& message) {
        std::ofstream logFile("hft_log.txt", std::ios_base::app);
        logFile << message << std::endl;
        std::cout << message << std::endl;
    }
};

struct Candle {
    double open;
    double close;
    double high;
    double low;
    double volume;
};

class MarketData {
public:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    static std::vector<Candle> fetch_market_data() {
        std::vector<Candle> candles;
        CURL* curl;
        CURLcode res;
        std::string readBuffer;

        curl = curl_easy_init();
        if(curl) {
            curl_easy_setopt(curl, CURLOPT_URL, "https://api.binance.com/api/v3/klines?symbol=BTCUSDT&interval=1m&limit=250");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);

            if(res == CURLE_OK) {
                Json::CharReaderBuilder reader;
                Json::Value jsonData;
                std::istringstream s(readBuffer);
                std::string parseErrors;

                if (Json::parseFromStream(reader, s, &jsonData, &parseErrors)) {
                    for (const auto& item : jsonData) {
                        candles.push_back({std::stod(item[1].asString()), std::stod(item[4].asString()), std::stod(item[2].asString()), std::stod(item[3].asString()), std::stod(item[5].asString())});
                    }
                }
            }
        }
        return candles;
    }
};

class RiskManagement {
private:
    double max_drawdown;
    double risk_per_trade;

public:
    RiskManagement(double drawdown, double risk) : max_drawdown(drawdown), risk_per_trade(risk) {}

    bool check_risk(double account_balance, double initial_balance) {
        return account_balance >= (initial_balance * (1 - max_drawdown));
    }

    double get_risk_per_trade() const {
        return risk_per_trade;
    }
};

class PositionManagement {
private:
    double account_balance;
    RiskManagement risk_manager;

public:
    PositionManagement(double initial_balance, double risk_percent, double drawdown) : account_balance(initial_balance), risk_manager(drawdown, risk_percent) {}

    double get_dynamic_position_size() {
        return (account_balance * risk_manager.get_risk_per_trade()) / 100.0;
    }

    void update_balance(double profit_or_loss) {
        account_balance += profit_or_loss;
    }

    bool check_risk(double initial_balance) {
        return risk_manager.check_risk(account_balance, initial_balance);
    }

    double get_balance() const {
        return account_balance;
    }
};

class HFTStrategy {
private:
    PositionManagement position_manager;
    int fast_ema_period = 3;
    int slow_ema_period = 8;
    int atr_period = 14;
    double risk_reward_ratio = 1.5;
    double volume_threshold = 1000.0;
    double initial_balance;

public:
    HFTStrategy(double initial_balance, double risk_percent, double drawdown) : position_manager(initial_balance, risk_percent, drawdown) {
        this->initial_balance = initial_balance;
    }

    void trade(const std::vector<Candle>& candles) {
        std::vector<double> closes;
        for (const auto& candle : candles) {
            closes.push_back(candle.close);
        }

        for (size_t i = slow_ema_period; i < closes.size(); ++i) {
            if (!position_manager.check_risk(initial_balance)) {
                Logger::log("Risk limit reached. Stopping trading.");
                break;
            }

            double fast_ema = closes[i] * (2.0 / (fast_ema_period + 1)) + closes[i - 1] * (1 - (2.0 / (fast_ema_period + 1)));
            double slow_ema = closes[i] * (2.0 / (slow_ema_period + 1)) + closes[i - 1] * (1 - (2.0 / (slow_ema_period + 1)));
            double atr = (candles[i].high - candles[i].low) * 1.5;
            double position_size = position_manager.get_dynamic_position_size();
            double stop_loss = 1.5 * atr;
            double take_profit = stop_loss * risk_reward_ratio;

            if (candles[i].volume > volume_threshold) {
                if (fast_ema > slow_ema) {
                    Logger::log("Long position: Entry at " + std::to_string(closes[i]) + " | SL: " + std::to_string(closes[i] - stop_loss) + " | TP: " + std::to_string(closes[i] + take_profit));
                } else if (fast_ema < slow_ema) {
                    Logger::log("Short position: Entry at " + std::to_string(closes[i]) + " | SL: " + std::to_string(closes[i] + stop_loss) + " | TP: " + std::to_string(closes[i] - take_profit));
                }
            }
        }
    }
};

int main() {
    HFTStrategy strategy(10000.0, 0.5, 0.1); // 10% max drawdown
    auto candles = MarketData::fetch_market_data();
    
    if (candles.empty()) {
        Logger::log("Failed to retrieve data from Binance.");
        return 1;
    }
    
    strategy.trade(candles);
    Logger::log("Final Account Balance: " + std::to_string(strategy.get_balance()));
    return 0;
}
