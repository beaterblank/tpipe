#include "Ducker.hpp"
#include <cmath>
#include <algorithm>

Ducker::Ducker(float sample_rate, const Parameters& params)
    : sample_rate_(sample_rate), params_(params), env_(0.0f), gain_(1.0f) {}

void Ducker::set_sample_rate(float sample_rate) {
    sample_rate_ = sample_rate;
}

void Ducker::set_parameters(const Parameters& params) {
    params_ = params;
}

float Ducker::calculate_target_gain(float env_db) const {
    float target_gain_db = 0.0f;
    
    // Knee logic for smooth ducking transition
    float half_knee = params_.knee_db * 0.5f;
    if (env_db > params_.threshold_db - half_knee) {
        float over = env_db - params_.threshold_db;
        if (over >= half_knee) {
            target_gain_db = params_.ducking_db;
        } else {
            // Soft knee interpolation
            target_gain_db = params_.ducking_db * (over + half_knee) / params_.knee_db;
        }
    }
    
    return std::pow(10.0f, target_gain_db / 20.0f);
}

float Ducker::calculate_coefficient(float time_ms) const {
    // Avoid division by zero if sample_rate or time_ms is 0
    if (sample_rate_ <= 0.0f || time_ms <= 0.0f) return 0.0f;
    return std::exp(-1.0f / (time_ms * 0.001f * sample_rate_));
}

float Ducker::process(float mic_level, float secondary_sample) {
    // Standard ducking: 
    // mic_level is the "sidechain" (the talker)
    // secondary_sample is the "carrier" (the music/background)

    // Convert mic level to dB for threshold comparison
    float env_db = 20.0f * std::log10(std::abs(mic_level) + 1e-8f);
    float target_gain = calculate_target_gain(env_db);
    
    float attack_coeff = calculate_coefficient(params_.attack_ms);
    float release_coeff = calculate_coefficient(params_.release_ms);
    
    // Smooth gain transitions
    if (target_gain < gain_) {
        // Attacking (Ducking down)
        gain_ = target_gain + attack_coeff * (gain_ - target_gain);
    } else {
        // Releasing (Returning to full volume)
        gain_ = target_gain + release_coeff * (gain_ - target_gain);
    }

    return secondary_sample * gain_;
}

void Ducker::reset() {
    env_ = 0.0f;
    gain_ = 1.0f;
}
