#pragma once

#include <ladspa.h>
#include <string>
#include <vector>
#include <memory>

class LadspaLoader {
public:
    struct PluginInfo {
        void* library_handle = nullptr;
        LADSPA_Handle instance = nullptr;
        const LADSPA_Descriptor* descriptor = nullptr;
        std::vector<unsigned long> audio_in_ports;
        std::vector<unsigned long> audio_out_ports;
        std::vector<unsigned long> control_in_ports;
    };

    LadspaLoader() = default;
    ~LadspaLoader();
    
    // Non-copyable, movable
    LadspaLoader(const LadspaLoader&) = delete;
    LadspaLoader& operator=(const LadspaLoader&) = delete;
    LadspaLoader(LadspaLoader&&) noexcept = default;
    LadspaLoader& operator=(LadspaLoader&&) noexcept = default;
    
    bool load_plugin(const std::string& label, float sample_rate);
    
    void connect_audio_ports(const std::vector<float*>& inputs, 
                            const std::vector<float*>& outputs);
    
    void connect_control_ports(const std::vector<float>& parameters);
    
    void run(unsigned long sample_count);
    
    bool is_loaded() const { return info_.instance != nullptr; }
    
    const PluginInfo& get_info() const { return info_; }

private:
    PluginInfo info_;
    std::vector<float> control_params_;
    
    void scan_ports();
    std::vector<std::string> get_ladspa_paths() const;
};
