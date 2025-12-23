#pragma once

class Ducker {
public:
    struct Parameters {
        float threshold_db = -30.0f;
        float ducking_db = -50.0f;
        float attack_ms = 5.0f;
        float release_ms = 150.0f;
        float knee_db = 10.0f;
    };

    explicit Ducker(float sample_rate) 
        : Ducker(sample_rate, Parameters{}) {}

    Ducker(float sample_rate, const Parameters& params);
    
    void set_sample_rate(float sample_rate);
    void set_parameters(const Parameters& params);
    
    float process(float mic_level, float secondary_sample);
    
    void reset();

private:
    float sample_rate_;
    Parameters params_;
    float env_ = 0.0f;
    float gain_ = 1.0f;
    
    float calculate_target_gain(float env_db) const;
    float calculate_coefficient(float time_ms) const;
};
