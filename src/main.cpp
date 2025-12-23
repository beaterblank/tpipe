#include <jack/jack.h>
#include <ladspa.h>
#include <dlfcn.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <map>
#include <fstream>
#include <memory>
#include <sstream>
#include <atomic>
#include <csignal>
#include <thread>

namespace fs = std::filesystem;

class AppConfig {
public:
    bool load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            if (auto pos = line.find('#'); pos != std::string::npos) line.erase(pos);
            if (line.empty()) continue;

            std::stringstream ss(line);
            std::string key, value;
            if (std::getline(ss, key, '=') && std::getline(ss, value)) {
                params[key] = std::stof(value);
            }
        }
        return true;
    }

    float get(const std::string& key, float default_val) const {
        auto it = params.find(key);
        return (it != params.end()) ? it->second : default_val;
    }

private:
    std::map<std::string, float> params;
};

struct Ducker {
    float threshold_db, ducking_db, attack_ms, release_ms, knee_db;
    float sample_rate = 48000.0f;
    float env = 0.0f;
    float gain = 1.0f;

    float process(float mic_level, float secondary_sample) {
        if (secondary_sample == 0.0f) return 0.0f;

        float env_db = 20.0f * std::log10(mic_level + 1e-8f);
        float target_gain_db = 0.0f;

        if (env_db > threshold_db - knee_db * 0.5f) {
            float over = env_db - threshold_db;
            if (over >= knee_db * 0.5f) target_gain_db = ducking_db;
            else target_gain_db = ducking_db * (over + knee_db * 0.5f) / knee_db;
        }

        float target_gain = std::pow(10.0f, target_gain_db / 20.0f);
        float attack_coeff  = std::exp(-1.0f / (attack_ms * 0.001f * sample_rate));
        float release_coeff = std::exp(-1.0f / (release_ms * 0.001f * sample_rate));

        gain = (target_gain < gain) 
            ? target_gain + attack_coeff * (gain - target_gain)
            : target_gain + release_coeff * (gain - target_gain);

        return secondary_sample * gain;
    }
};

class VoiceIndoorFilter {
    float sample_rate, low_cut, high_cut;
    float lp_prev = 0, hp_prev = 0, power_smooth = 0, noise_floor = 1e-6f;
    float alpha_power, alpha_noise;
    int min_counter = 0;
    static constexpr int MIN_WINDOW = 4800;

public:
    VoiceIndoorFilter(float sr, float low, float high) 
        : sample_rate(sr), low_cut(low), high_cut(high) {
        alpha_power = std::exp(-1.0f / (0.010f * sample_rate));
        alpha_noise = std::exp(-1.0f / (0.200f * sample_rate));
    }

    float process(float x) {
        // Bandpass
        float dt = 1.0f / sample_rate;
        lp_prev += (dt / (1.0f / (2.0f * M_PI * low_cut) + dt)) * (x - lp_prev);
        hp_prev += (dt / (1.0f / (2.0f * M_PI * high_cut) + dt)) * (x - hp_prev);
        float filtered = lp_prev - hp_prev;

        // Suppress
        float p = filtered * filtered;
        power_smooth = alpha_power * power_smooth + (1.0f - alpha_power) * p;
        if (power_smooth < noise_floor) noise_floor = power_smooth;
        if (++min_counter >= MIN_WINDOW) {
            min_counter = 0;
            noise_floor = alpha_noise * noise_floor + (1.0f - alpha_noise) * power_smooth;
        }
        float gain = std::max(power_smooth - noise_floor, 0.0f) / (power_smooth + 1e-9f);
        return filtered * gain;
    }
};

class AudioEngine {
public:
    AudioEngine(const AppConfig& cfg) : config(cfg) {}
    ~AudioEngine() {
        if (client) jack_client_close(client);
        if (handle && desc->cleanup) desc->cleanup(handle);
        if (ladspa_lib_handle) dlclose(ladspa_lib_handle);
    }

    bool init() {
        jack_status_t status;
        client = jack_client_open("tpipe", JackNullOption, &status);
        if (!client) return false;

        float sr = (float)jack_get_sample_rate(client);
        filter_l = std::make_unique<VoiceIndoorFilter>(sr, config.get("low_cut", 120.0f), config.get("high_cut", 200.0f));
        filter_r = std::make_unique<VoiceIndoorFilter>(sr, config.get("low_cut", 120.0f), config.get("high_cut", 200.0f));

        setup_ducker(ducker_l, sr);
        setup_ducker(ducker_r, sr);

        if (!load_ladspa(sr)) std::cerr << "LADSPA Plugin not found. Running in bypass mode.\n";

        register_ports();
        jack_set_process_callback(client, AudioEngine::static_process, this);
        jack_set_buffer_size_callback(client, AudioEngine::static_bufsize, this);
        
        return jack_activate(client) == 0;
    }

private:
    void setup_ducker(Ducker& d, float sr) {
        d.sample_rate = sr;
        d.threshold_db = config.get("threshold_db", -30.0f);
        d.ducking_db   = config.get("ducking_db", -50.0f);
        d.attack_ms    = config.get("attack_ms", 5.0f);
        d.release_ms   = config.get("release_ms", 150.0f);
        d.knee_db      = config.get("knee_db", 10.0f);
    }

    void register_ports() {
        in_l = jack_port_register(client, "in_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        in_r = jack_port_register(client, "in_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        out_l = jack_port_register(client, "out_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        out_r = jack_port_register(client, "out_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        sec_l = jack_port_register(client, "sec_in_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        sec_r = jack_port_register(client, "sec_in_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    }

    bool load_ladspa(float sr) {
        const char* path_env = std::getenv("LADSPA_PATH");
        std::string paths = path_env ? path_env : "/usr/lib/ladspa:/usr/local/lib/ladspa";
        std::stringstream ss(paths);
        std::string path;

        while (std::getline(ss, path, ':')) {
            if (!fs::exists(path)) continue;
            for (auto& entry : fs::directory_iterator(path)) {
                if (entry.path().extension() != ".so") continue;
                void* lib = dlopen(entry.path().c_str(), RTLD_NOW);
                if (!lib) continue;
                auto func = (LADSPA_Descriptor_Function)dlsym(lib, "ladspa_descriptor");
                for (unsigned long i = 0; func && (desc = func(i)); ++i) {
                    if (std::string(desc->Label) == "deep_filter_stereo") {
                        ladspa_lib_handle = lib;
                        handle = desc->instantiate(desc, sr);
                        map_ladspa_ports();
                        return true;
                    }
                }
                dlclose(lib);
            }
        }
        return false;
    }

    void map_ladspa_ports() {
        ladspa_params = {
            config.get("attenuation_limit", 80.0f), config.get("min_thresh", -15.0f),
            config.get("max_erb", 35.0f), config.get("max_df", 35.0f),
            config.get("min_buf", 0.0f), config.get("post_beta", 0.0f)
        };
        
        unsigned long ctrl_ptr = 0;
        for (unsigned long j = 0; j < desc->PortCount; j++) {
            LADSPA_PortDescriptor pd = desc->PortDescriptors[j];
            if (LADSPA_IS_PORT_AUDIO(pd)) {
                if (LADSPA_IS_PORT_INPUT(pd)) audio_in_idx.push_back(j);
                else audio_out_idx.push_back(j);
            } else if (LADSPA_IS_PORT_CONTROL(pd) && LADSPA_IS_PORT_INPUT(pd) && ctrl_ptr < ladspa_params.size()) {
                desc->connect_port(handle, j, &ladspa_params[ctrl_ptr++]);
            }
        }
    }

    static int static_process(jack_nframes_t n, void* arg) { return ((AudioEngine*)arg)->process(n); }
    static int static_bufsize(jack_nframes_t n, void* arg) { return ((AudioEngine*)arg)->resize_buffers(n); }

    int resize_buffers(jack_nframes_t n) {
        buf_in_l.resize(n); buf_in_r.resize(n);
        buf_out_l.resize(n); buf_out_r.resize(n);
        if (handle && audio_in_idx.size() >= 2) {
            desc->connect_port(handle, audio_in_idx[0], buf_in_l.data());
            desc->connect_port(handle, audio_in_idx[1], buf_in_r.data());
            desc->connect_port(handle, audio_out_idx[0], buf_out_l.data());
            desc->connect_port(handle, audio_out_idx[1], buf_out_r.data());
        }
        return 0;
    }

    int process(jack_nframes_t n) {
        auto get_buf = [&](jack_port_t* p) { return (float*)jack_port_get_buffer(p, n); };
        float *iL = get_buf(in_l), *iR = get_buf(in_r), *sL = get_buf(sec_l), *sR = get_buf(sec_r);
        float *oL = get_buf(out_l), *oR = get_buf(out_r);

        for (jack_nframes_t i = 0; i < n; ++i) {
            buf_in_l[i] = filter_l->process(iL[i]);
            buf_in_r[i] = filter_r->process(iR[i]);
        }

        if (handle) desc->run(handle, n);
        else { std::copy(buf_in_l.begin(), buf_in_l.end(), buf_out_l.begin()); 
               std::copy(buf_in_r.begin(), buf_in_r.end(), buf_out_r.begin()); }

        for (jack_nframes_t i = 0; i < n; ++i) {
            float mic_lvl = std::abs(buf_out_l[i] + buf_out_r[i]) * 0.5f;
            oL[i] = buf_out_l[i] + ducker_l.process(mic_lvl, sL[i]);
            oR[i] = buf_out_r[i] + ducker_r.process(mic_lvl, sR[i]);
        }
        return 0;
    }

    jack_client_t* client = nullptr;
    jack_port_t *in_l, *in_r, *out_l, *out_r, *sec_l, *sec_r;
    std::unique_ptr<VoiceIndoorFilter> filter_l, filter_r;
    Ducker ducker_l, ducker_r;
    AppConfig config;

    void* ladspa_lib_handle = nullptr;
    LADSPA_Handle handle = nullptr;
    const LADSPA_Descriptor* desc = nullptr;
    std::vector<float> buf_in_l, buf_in_r, buf_out_l, buf_out_r, ladspa_params;
    std::vector<unsigned long> audio_in_idx, audio_out_idx;
};


std::atomic<bool> keepRunning(true);
void signalHandler(int signum) {
    std::cout << "\n[Interrupt signal (" << signum << ") received]. Cleaning up...\n";
    keepRunning = false; 
}

int main() {
    
    AppConfig cfg;
    if (!cfg.load("default.conf")) {
        std::cerr << "Config error.\n";
        return 1;
    }
    AudioEngine engine(cfg);
    
    if (!engine.init()) return 1;
    std::cout << "Engine running.\n";

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    while(keepRunning){
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    return 0;
}
