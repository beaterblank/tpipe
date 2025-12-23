#pragma once

class VoiceIndoorFilter {
public:
    VoiceIndoorFilter(float sample_rate, float low_cut, float high_cut);
    
    float process(float input);
    void reset();
    
    void set_sample_rate(float sample_rate);
    void set_cutoffs(float low_cut, float high_cut);

private:
    float sample_rate_;
    float low_cut_;
    float high_cut_;
    
    // Filter state
    float lp_prev_ = 0.0f;
    float hp_prev_ = 0.0f;
    
    // Noise suppression state
    float power_smooth_ = 0.0f;
    float noise_floor_ = 1e-6f;
    int min_counter_ = 0;
    
    // Smoothing coefficients
    float alpha_power_;
    float alpha_noise_;
    
    static constexpr int MIN_WINDOW = 4800;
    
    void update_coefficients();
    float apply_bandpass(float input);
    float apply_noise_suppression(float filtered);
};
