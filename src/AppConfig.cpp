#include "AppConfig.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

bool AppConfig::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        // Remove comments
        if (auto pos = line.find('#'); pos != std::string::npos) {
            line.erase(pos);
        }
        
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string key, value;
        if (std::getline(ss, key, '=') && std::getline(ss, value)) {
            try {
                params_[key] = std::stof(value);
            } catch (const std::exception&) {
                // Skip invalid values
                continue;
            }
        }
    }
    return true;
}

float AppConfig::get(const std::string& key, float default_val) const {
    auto it = params_.find(key);
    return (it != params_.end()) ? it->second : default_val;
}

std::optional<float> AppConfig::get(const std::string& key) const {
    auto it = params_.find(key);
    if (it != params_.end()) {
        return it->second;
    }
    return std::nullopt;
}
