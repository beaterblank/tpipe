#include "AudioEngine.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

AudioEngine::AudioEngine(const AppConfig& config)
    : config_(config) {}

AudioEngine::~AudioEngine() {
    if (client_) {
        jack_client_close(client_);
    }
}

bool AudioEngine::create_jack_client() {
    jack_status_t status;
    client_ = jack_client_open("tpipe", JackNullOption, &status);
    
    if (!client_) {
        std::cerr << "Failed to create JACK client. Status: " << status << "\n";
        return false;
    }
    
    return true;
}

void AudioEngine::register_jack_ports() {
    in_l_ = jack_port_register(client_, "in_l", JACK_DEFAULT_AUDIO_TYPE, 
                               JackPortIsInput, 0);
    in_r_ = jack_port_register(client_, "in_r", JACK_DEFAULT_AUDIO_TYPE, 
                               JackPortIsInput, 0);
    out_l_ = jack_port_register(client_, "out_l", JACK_DEFAULT_AUDIO_TYPE, 
                                JackPortIsOutput, 0);
    out_r_ = jack_port_register(client_, "out_r", JACK_DEFAULT_AUDIO_TYPE, 
                                JackPortIsOutput, 0);
    sec_l_ = jack_port_register(client_, "sec_in_l", JACK_DEFAULT_AUDIO_TYPE, 
                                JackPortIsInput, 0);
    sec_r_ = jack_port_register(client_, "sec_in_r", JACK_DEFAULT_AUDIO_TYPE, 
                                JackPortIsInput, 0);
}

void AudioEngine::initialize_processors(float sample_rate) {
    float low_cut = config_.get("low_cut", 120.0f);
    float high_cut = config_.get("high_cut", 200.0f);
    
    filter_l_ = std::make_unique<VoiceIndoorFilter>(sample_rate, low_cut, high_cut);
    filter_r_ = std::make_unique<VoiceIndoorFilter>(sample_rate, low_cut, high_cut);
    
    Ducker::Parameters ducker_params;
    ducker_params.threshold_db = config_.get("threshold_db", -30.0f);
    ducker_params.ducking_db = config_.get("ducking_db", -50.0f);
    ducker_params.attack_ms = config_.get("attack_ms", 5.0f);
    ducker_params.release_ms = config_.get("release_ms", 150.0f);
    ducker_params.knee_db = config_.get("knee_db", 10.0f);
    
    ducker_l_ = std::make_unique<Ducker>(sample_rate, ducker_params);
    ducker_r_ = std::make_unique<Ducker>(sample_rate, ducker_params);
}

bool AudioEngine::load_ladspa_plugin(float sample_rate) {
    ladspa_loader_ = std::make_unique<LadspaLoader>();
    
    if (!ladspa_loader_->load_plugin("deep_filter_stereo", sample_rate)) {
        std::cerr << "LADSPA Plugin not found. Running in bypass mode.\n";
        return false;
    }
    
    std::vector<float> params = {
        config_.get("attenuation_limit", 80.0f),
        config_.get("min_thresh", -15.0f),
        config_.get("max_erb", 35.0f),
        config_.get("max_df", 35.0f),
        config_.get("min_buf", 0.0f),
        config_.get("post_beta", 0.0f)
    };
    
    ladspa_loader_->connect_control_ports(params);
    return true;
}

bool AudioEngine::initialize() {
    if (!create_jack_client()) {
        return false;
    }
    
    float sample_rate = static_cast<float>(jack_get_sample_rate(client_));
    
    register_jack_ports();
    initialize_processors(sample_rate);
    load_ladspa_plugin(sample_rate);
    
    jack_set_process_callback(client_, AudioEngine::static_process_callback, this);
    jack_set_buffer_size_callback(client_, AudioEngine::static_bufsize_callback, this);
    
    if (jack_activate(client_) != 0) {
        std::cerr << "Failed to activate JACK client\n";
        return false;
    }
    
    return true;
}

int AudioEngine::static_process_callback(jack_nframes_t nframes, void* arg) {
    return static_cast<AudioEngine*>(arg)->process(nframes);
}

int AudioEngine::static_bufsize_callback(jack_nframes_t nframes, void* arg) {
    return static_cast<AudioEngine*>(arg)->on_buffer_size_change(nframes);
}

int AudioEngine::on_buffer_size_change(jack_nframes_t nframes) {
    buf_in_l_.resize(nframes);
    buf_in_r_.resize(nframes);
    buf_out_l_.resize(nframes);
    buf_out_r_.resize(nframes);
    
    if (ladspa_loader_ && ladspa_loader_->is_loaded()) {
        std::vector<float*> inputs = {buf_in_l_.data(), buf_in_r_.data()};
        std::vector<float*> outputs = {buf_out_l_.data(), buf_out_r_.data()};
        ladspa_loader_->connect_audio_ports(inputs, outputs);
    }
    
    return 0;
}

void AudioEngine::process_input_filters(jack_nframes_t nframes, 
                                       float* in_l, float* in_r) {
    for (jack_nframes_t i = 0; i < nframes; ++i) {
        buf_in_l_[i] = filter_l_->process(in_l[i]);
        buf_in_r_[i] = filter_r_->process(in_r[i]);
    }
}

void AudioEngine::process_ladspa(jack_nframes_t nframes) {
    if (ladspa_loader_ && ladspa_loader_->is_loaded()) {
        ladspa_loader_->run(nframes);
    } else {
        // Bypass mode
        std::copy(buf_in_l_.begin(), buf_in_l_.begin() + nframes, buf_out_l_.begin());
        std::copy(buf_in_r_.begin(), buf_in_r_.begin() + nframes, buf_out_r_.begin());
    }
}

void AudioEngine::process_output_mix(jack_nframes_t nframes,
                                     float* sec_l, float* sec_r,
                                     float* out_l, float* out_r) {
    for (jack_nframes_t i = 0; i < nframes; ++i) {
        float mic_level = std::abs(buf_out_l_[i] + buf_out_r_[i]) * 0.5f;
        
        out_l[i] = buf_out_l_[i] + ducker_l_->process(mic_level, sec_l[i]);
        out_r[i] = buf_out_r_[i] + ducker_r_->process(mic_level, sec_r[i]);
    }
}

int AudioEngine::process(jack_nframes_t nframes) {
    auto get_buffer = [this, nframes](jack_port_t* port) {
        return static_cast<float*>(jack_port_get_buffer(port, nframes));
    };
    
    float* in_l = get_buffer(in_l_);
    float* in_r = get_buffer(in_r_);
    float* sec_l = get_buffer(sec_l_);
    float* sec_r = get_buffer(sec_r_);
    float* out_l = get_buffer(out_l_);
    float* out_r = get_buffer(out_r_);
    
    process_input_filters(nframes, in_l, in_r);
    process_ladspa(nframes);
    process_output_mix(nframes, sec_l, sec_r, out_l, out_r);
    
    return 0;
}
