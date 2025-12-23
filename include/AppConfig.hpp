#pragma once

#include <string>
#include <map>
#include <optional>

class AppConfig {
public:
    bool load(const std::string& filename);
    
    float get(const std::string& key, float default_val) const;
    
    std::optional<float> get(const std::string& key) const;

private:
    std::map<std::string, float> params_;
};
