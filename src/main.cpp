#include <jack/jack.h>
#include <ladspa.h>
#include <dlfcn.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <map>
#include <fstream>

namespace fs = std::filesystem;

struct AppConfig {
    std::map<std::string, float> params;

    bool load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            // Remove comments
            size_t comment_pos = line.find('#');
            if (comment_pos != std::string::npos) line.erase(comment_pos);
            
            if (line.empty()) continue;

            std::stringstream ss(line);
            std::string key;
            if (std::getline(ss, key, '=')) {
                std::string value;
                if (std::getline(ss, value)) {
                    params[key] = std::stof(value);
                }
            }
        }
        return true;
    }

    float get(const std::string& key, float default_val) {
        return params.count(key) ? params[key] : default_val;
    }
};

struct Ducker {
    // User parameters
    float threshold_db = -30.0f;   // Mic level that triggers ducking
    float ducking_db   = -50.0f;   // How much to reduce secondary signal
    float attack_ms    = 5.0f;     // How fast ducking engages
    float release_ms   = 150.0f;   // How fast it recovers
    float knee_db      = 120.0f;     // Soft knee width

    float sample_rate  = 48000.0f;

    // Internal state
    float env = 0.0f;
    float gain = 1.0f;

    // Convert dB to linear
    static inline float db_to_lin(float db) {
        return std::pow(10.0f, db / 20.0f);
    }

    float process(float mic_sample, float secondary_sample) {
        if (secondary_sample == 0.0f) {
            return 0.0f;
        }

        // --- Envelope follower (RMS-like) ---
        float mic_abs = std::fabs(mic_sample);
        env += (mic_abs - env) * 0.01f;  // smoothing

        float env_db = 20.0f * std::log10(env + 1e-8f);

        // --- Gain computer with soft knee ---
        float target_gain_db = 0.0f;

        if (env_db > threshold_db - knee_db * 0.5f) {
            float over = env_db - threshold_db;

            if (over >= knee_db * 0.5f)
                target_gain_db = ducking_db;
            else
                target_gain_db = ducking_db * (over + knee_db * 0.5f) / knee_db;
        }

        float target_gain = db_to_lin(target_gain_db);

        // --- Attack / Release smoothing ---
        float attack_coeff  = std::exp(-1.0f / (attack_ms * 0.001f * sample_rate));
        float release_coeff = std::exp(-1.0f / (release_ms * 0.001f * sample_rate));

        if (target_gain < gain)
            gain = target_gain + attack_coeff * (gain - target_gain);
        else
            gain = target_gain + release_coeff * (gain - target_gain);

        return secondary_sample * gain;
    }
};
Ducker ducker_l, ducker_r;
struct VoiceIndoorFilter {
    float sample_rate;
    float low_cut, high_cut;
    float lp_prev, hp_prev;
    float power_smooth, noise_floor;
    float alpha_power, alpha_noise;
    int min_counter;
    static constexpr int MIN_WINDOW = 4800; // ~100ms @ 48kHz

    VoiceIndoorFilter(float sr, float low = 120.0f, float high = 200.0f)
        : sample_rate(sr), low_cut(low), high_cut(high),
          lp_prev(0), hp_prev(0), power_smooth(0), noise_floor(1e-6f),
          min_counter(0)
    {
        alpha_power = std::exp(-1.0f / (0.010f * sample_rate));
        alpha_noise = std::exp(-1.0f / (0.200f * sample_rate));
    }

    inline float bandpass(float x) {
        float dt = 1.0f / sample_rate;
        float a_lp = dt / (1.0f / (2.0f * M_PI * low_cut) + dt);
        float a_hp = dt / (1.0f / (2.0f * M_PI * high_cut) + dt);
        lp_prev += a_lp * (x - lp_prev);
        hp_prev += a_hp * (x - hp_prev);
        return lp_prev - hp_prev;
    }

    inline float suppress(float x) {
        float p = x * x;
        power_smooth = alpha_power * power_smooth + (1.0f - alpha_power) * p;
        if (power_smooth < noise_floor) noise_floor = power_smooth;
        if (++min_counter >= MIN_WINDOW) {
            min_counter = 0;
            noise_floor = alpha_noise * noise_floor + (1.0f - alpha_noise) * power_smooth;
        }
        float signal_est = std::max(power_smooth - noise_floor, 0.0f);
        float gain = signal_est / (signal_est + noise_floor + 1e-9f);
        return x * gain;
    }

    inline float process(float x) {
        return suppress(bandpass(x));
    }

    void process_buffer(float* in, float* out, jack_nframes_t nframes) {
        for (jack_nframes_t i = 0; i < nframes; ++i)
            out[i] = process(in[i]);
    }
};

jack_port_t *input_port_l, *input_port_r, *output_port_l, *output_port_r;
jack_port_t *sec_input_l, *sec_input_r;
jack_client_t* client = nullptr;

// Voice Filters
VoiceIndoorFilter* filter_l = nullptr;
VoiceIndoorFilter* filter_r = nullptr;

// LADSPA
const LADSPA_Descriptor* desc = nullptr;
LADSPA_Handle handle = nullptr;
void* ladspa_lib_handle = nullptr;
std::vector<unsigned long> audio_in_indices, audio_out_indices, control_indices;
std::vector<float> control_values;
std::vector<float> ladspa_buf_in_l, ladspa_buf_in_r, ladspa_buf_out_l, ladspa_buf_out_r;
const char* PLUGIN_LABEL = "deep_filter_stereo";

const LADSPA_Descriptor* find_ladspa_plugin(const char* label) {
    const char* path_env = std::getenv("LADSPA_PATH");
    std::string paths = path_env ? path_env : "/usr/lib/ladspa:/usr/local/lib/ladspa";
    std::string path;
    std::stringstream ss(paths);
    while (std::getline(ss, path, ':')) {
        if (!fs::exists(path)) continue;
        for (auto& entry : fs::directory_iterator(path)) {
            if (entry.path().extension() != ".so") continue;
            void* lib = dlopen(entry.path().c_str(), RTLD_NOW);
            if (!lib) continue;
            auto func = (LADSPA_Descriptor_Function)dlsym(lib, "ladspa_descriptor");
            if (!func) { dlclose(lib); continue; }
            for (unsigned long i = 0; ; ++i) {
                const LADSPA_Descriptor* d = func(i);
                if (!d) break;
                if (std::strcmp(d->Label, label) == 0) {
                    ladspa_lib_handle = lib;
                    for (unsigned long j = 0; j < d->PortCount; j++) {
                        LADSPA_PortDescriptor pd = d->PortDescriptors[j];
                        if (LADSPA_IS_PORT_AUDIO(pd)) {
                            if (LADSPA_IS_PORT_INPUT(pd)) audio_in_indices.push_back(j);
                            else if (LADSPA_IS_PORT_OUTPUT(pd)) audio_out_indices.push_back(j);
                        } else if (LADSPA_IS_PORT_CONTROL(pd) && LADSPA_IS_PORT_INPUT(pd)) {
                            control_indices.push_back(j);
                        }
                    }
                    return d;
                }
            }
            dlclose(lib);
        }
    }
    return nullptr;
}

int bufsize_callback(jack_nframes_t nframes, void* arg) {
    ladspa_buf_in_l.resize(nframes);
    ladspa_buf_in_r.resize(nframes);
    ladspa_buf_out_l.resize(nframes);
    ladspa_buf_out_r.resize(nframes);

    if (handle && desc && audio_in_indices.size() >= 2 && audio_out_indices.size() >= 2) {
        desc->connect_port(handle, audio_in_indices[0], ladspa_buf_in_l.data());
        desc->connect_port(handle, audio_in_indices[1], ladspa_buf_in_r.data());
        desc->connect_port(handle, audio_out_indices[0], ladspa_buf_out_l.data());
        desc->connect_port(handle, audio_out_indices[1], ladspa_buf_out_r.data());
    }
    return 0;
}

int process_callback(jack_nframes_t nframes, void* arg) {
    float* inL = (float*)jack_port_get_buffer(input_port_l, nframes);
    float* inR = (float*)jack_port_get_buffer(input_port_r, nframes);
    float* outL = (float*)jack_port_get_buffer(output_port_l, nframes);
    float* outR = (float*)jack_port_get_buffer(output_port_r, nframes);
    float* secL = (float*)jack_port_get_buffer(sec_input_l, nframes);
    float* secR = (float*)jack_port_get_buffer(sec_input_r, nframes);
    // First apply VoiceIndoorFilter
    filter_l->process_buffer(inL, ladspa_buf_in_l.data(), nframes);
    filter_r->process_buffer(inR, ladspa_buf_in_r.data(), nframes);

    // Then LADSPA
    if (handle && desc && audio_in_indices.size() >= 2 && audio_out_indices.size() >= 2) {
        desc->run(handle, nframes);
        std::memcpy(outL, ladspa_buf_out_l.data(), nframes * sizeof(float));
        std::memcpy(outR, ladspa_buf_out_r.data(), nframes * sizeof(float));
    } else {
        std::memcpy(outL, ladspa_buf_in_l.data(), nframes * sizeof(float));
        std::memcpy(outR, ladspa_buf_in_r.data(), nframes * sizeof(float));
    }

    // 3. Ducking and Mixing
    for (jack_nframes_t i = 0; i < nframes; ++i) {
        // Calculate instantaneous level (envelope) of the filtered mic
        float mic_level = std::abs(ladspa_buf_out_l[i] + ladspa_buf_out_r[i]) * 0.5f;

        // Duck the secondary signal
        float duckedL = ducker_l.process(mic_level, secL[i]);
        float duckedR = ducker_r.process(mic_level, secR[i]);

        // Output = Cleaned Ambient + Ducked Secondary
        outL[i] = ladspa_buf_out_l[i] + duckedL;
        outR[i] = ladspa_buf_out_r[i] + duckedR;
    }

    return 0;
}

float attenuation_limit = 80.0f;
float min_thresh        = -15.0f;
float max_erb           = 35.0f;
float max_df            = 35.0f;
float min_buf           = 0.0f;
float post_beta         = 0.0f;

int main() {
    
    AppConfig cfg;

    if(cfg.load("default.conf")){
      std::cout << "Configuration Loaded Successfully";
    } else {
      std::cout << "Unable to load Configuration";
      return 1;
    }

    jack_status_t status;
    client = jack_client_open("transparecy-pipe", JackNullOption, &status);
    if (!client) return 1;

    float sr = (float)jack_get_sample_rate(client);
    
    filter_l = new VoiceIndoorFilter(sr, cfg.get("low_cut", 120.0f), cfg.get("high_cut", 200.0f));
    filter_r = new VoiceIndoorFilter(sr, cfg.get("low_cut", 120.0f), cfg.get("high_cut", 200.0f));

    // --- Apply Config to Duckers ---
    auto setup_ducker = [&](Ducker& d) {
        d.sample_rate = sr;
        d.threshold_db = cfg.get("threshold_db", -30.0f);
        d.ducking_db   = cfg.get("ducking_db", -50.0f);
        d.attack_ms    = cfg.get("attack_ms", 5.0f);
        d.release_ms   = cfg.get("release_ms", 150.0f);
        d.knee_db      = cfg.get("knee_db", 10.0f);
    };
    setup_ducker(ducker_l);
    setup_ducker(ducker_r);

    float attenuation_limit = cfg.get("attenuation_limit", 80.0f);;
    float min_thresh        = cfg.get("min_thresh", -15.0f);;
    float max_erb           = cfg.get("max_erb", 35.0f);
    float max_df            = cfg.get("max_df", 35.0f);
    float min_buf           = cfg.get("min_buf",0.0f);
    float post_beta         = cfg.get("post_beta",0.0f);

    // Find LADSPA plugin
    desc = find_ladspa_plugin(PLUGIN_LABEL);
    if (desc) {
        handle = desc->instantiate(desc, sr);
        control_values.resize(control_indices.size(), 0.0f);
        for (size_t i = 0; i < control_indices.size(); ++i)
            desc->connect_port(handle, control_indices[i], &control_values[i]);
        desc->connect_port(handle, 4, &attenuation_limit);
        desc->connect_port(handle, 5, &min_thresh);
        desc->connect_port(handle, 6, &max_erb);
        desc->connect_port(handle, 7, &max_df);
        desc->connect_port(handle, 8, &min_buf);
        desc->connect_port(handle, 9, &post_beta);
    } else {
        std::cerr << "Could not find plugin " << PLUGIN_LABEL << std::endl;
    }

    input_port_l  = jack_port_register(client, "in_l",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    input_port_r  = jack_port_register(client, "in_r",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    output_port_l = jack_port_register(client, "out_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    output_port_r = jack_port_register(client, "out_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    sec_input_l = jack_port_register(client, "sec_in_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    sec_input_r = jack_port_register(client, "sec_in_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    
    bufsize_callback(jack_get_buffer_size(client), nullptr);

    jack_set_process_callback(client, process_callback, nullptr);
    jack_set_buffer_size_callback(client, bufsize_callback, nullptr);

    if (jack_activate(client)) return 1;

    std::cout << "VoiceIndoorFilter + LADSPA plugin running. Press Enter to exit.\n";
    std::cin.get();

    jack_client_close(client);
    delete filter_l;
    delete filter_r;

    if (handle && desc->cleanup) desc->cleanup(handle);
    if (ladspa_lib_handle) dlclose(ladspa_lib_handle);

    return 0;
}
