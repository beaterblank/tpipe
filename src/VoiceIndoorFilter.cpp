#include "VoiceIndoorFilter.hpp"
#include <cmath>
#include <algorithm>

VoiceIndoorFilter::VoiceIndoorFilter(float sample_rate, float low_cut, float high_cut)
    : sample_rate_(sample_rate), low_cut_(low_cut), high_cut_(high_cut) {
    update_coefficients();
}

void VoiceIndoorFilter::update_coefficients() {
    alpha_power_ = std::exp(-1.0f / (0.010f * sample_rate_));
    alpha_noise_ = std::exp(-1.0f / (0.200f * sample_rate_));
}

void VoiceIndoorFilter::set_sample_rate(float sample_rate) {
    sample_rate_ = sample_rate;
    update_coefficients();
}

void VoiceIndoorFilter::set_cutoffs(float low_cut, float high_cut) {
    low_cut_ = low_cut;
    high_cut_ = high_cut;
}

float VoiceIndoorFilter::apply_bandpass(float input) {
    float dt = 1.0f / sample_rate_;
    
    lp_prev_ += (dt / (1.0f / (2.0f * M_PI * low_cut_) + dt)) * (input - lp_prev_);
    hp_prev_ += (dt / (1.0f / (2.0f * M_PI * high_cut_) + dt)) * (input - hp_prev_);
    
    return lp_prev_ - hp_prev_;
}

float VoiceIndoorFilter::apply_noise_suppression(float filtered) {
    float power = filtered * filtered;
    power_smooth_ = alpha_power_ * power_smooth_ + (1.0f - alpha_power_) * power;
    
    if (power_smooth_ < noise_floor_) {
        noise_floor_ = power_smooth_;
    }
    
    if (++min_counter_ >= MIN_WINDOW) {
        min_counter_ = 0;
        noise_floor_ = alpha_noise_ * noise_floor_ + (1.0f - alpha_noise_) * power_smooth_;
    }
    
    float gain = std::max(power_smooth_ - noise_floor_, 0.0f) / (power_smooth_ + 1e-9f);
    return filtered * gain;
}

float VoiceIndoorFilter::process(float input) {
    float filtered = apply_bandpass(input);
    return apply_noise_suppression(filtered);
}

void VoiceIndoorFilter::reset() {
    lp_prev_ = 0.0f;
    hp_prev_ = 0.0f;
    power_smooth_ = 0.0f;
    noise_floor_ = 1e-6f;
    min_counter_ = 0;
}
