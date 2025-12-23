#pragma once

#include <jack/jack.h>
#include <memory>
#include <vector>
#include "AppConfig.hpp"
#include "Ducker.hpp"
#include "VoiceIndoorFilter.hpp"
#include "LadspaLoader.hpp"

class AudioEngine {
public:
    explicit AudioEngine(const AppConfig& config);
    ~AudioEngine();
    
    // Non-copyable, non-movable
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;
    
    bool initialize();
    
    bool is_active() const { return client_ != nullptr; }

private:
    // JACK callbacks
    static int static_process_callback(jack_nframes_t nframes, void* arg);
    static int static_bufsize_callback(jack_nframes_t nframes, void* arg);
    
    int process(jack_nframes_t nframes);
    int on_buffer_size_change(jack_nframes_t nframes);
    
    // Initialization helpers
    bool create_jack_client();
    void register_jack_ports();
    void initialize_processors(float sample_rate);
    bool load_ladspa_plugin(float sample_rate);
    
    // Processing
    void process_input_filters(jack_nframes_t nframes, float* in_l, float* in_r);
    void process_ladspa(jack_nframes_t nframes);
    void process_output_mix(jack_nframes_t nframes, float* sec_l, float* sec_r, 
                           float* out_l, float* out_r);
    
    // Configuration
    const AppConfig& config_;
    
    // JACK resources
    jack_client_t* client_ = nullptr;
    jack_port_t* in_l_ = nullptr;
    jack_port_t* in_r_ = nullptr;
    jack_port_t* out_l_ = nullptr;
    jack_port_t* out_r_ = nullptr;
    jack_port_t* sec_l_ = nullptr;
    jack_port_t* sec_r_ = nullptr;
    
    // Audio processors
    std::unique_ptr<VoiceIndoorFilter> filter_l_;
    std::unique_ptr<VoiceIndoorFilter> filter_r_;
    std::unique_ptr<Ducker> ducker_l_;
    std::unique_ptr<Ducker> ducker_r_;
    std::unique_ptr<LadspaLoader> ladspa_loader_;
    
    // Audio buffers
    std::vector<float> buf_in_l_;
    std::vector<float> buf_in_r_;
    std::vector<float> buf_out_l_;
    std::vector<float> buf_out_r_;
};
