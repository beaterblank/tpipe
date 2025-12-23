#include "LadspaLoader.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <sstream>
#include <cstdlib>
#include <iostream>

namespace fs = std::filesystem;

LadspaLoader::~LadspaLoader() {
    if (info_.instance && info_.descriptor && info_.descriptor->cleanup) {
        info_.descriptor->cleanup(info_.instance);
    }
    if (info_.library_handle) {
        dlclose(info_.library_handle);
    }
}

std::vector<std::string> LadspaLoader::get_ladspa_paths() const {
    const char* path_env = std::getenv("LADSPA_PATH");
    std::string paths = path_env ? path_env : "/usr/lib/ladspa:/usr/local/lib/ladspa";
    
    std::vector<std::string> result;
    std::stringstream ss(paths);
    std::string path;
    
    while (std::getline(ss, path, ':')) {
        if (fs::exists(path)) {
            result.push_back(path);
        }
    }
    
    return result;
}

void LadspaLoader::scan_ports() {
    if (!info_.descriptor) return;
    
    for (unsigned long i = 0; i < info_.descriptor->PortCount; ++i) {
        LADSPA_PortDescriptor pd = info_.descriptor->PortDescriptors[i];
        
        if (LADSPA_IS_PORT_AUDIO(pd)) {
            if (LADSPA_IS_PORT_INPUT(pd)) {
                info_.audio_in_ports.push_back(i);
            } else if (LADSPA_IS_PORT_OUTPUT(pd)) {
                info_.audio_out_ports.push_back(i);
            }
        } else if (LADSPA_IS_PORT_CONTROL(pd) && LADSPA_IS_PORT_INPUT(pd)) {
            info_.control_in_ports.push_back(i);
        }
    }
}

bool LadspaLoader::load_plugin(const std::string& label, float sample_rate) {
    auto paths = get_ladspa_paths();
    
    for (const auto& path : paths) {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.path().extension() != ".so") continue;
            
            void* lib = dlopen(entry.path().c_str(), RTLD_NOW);
            if (!lib) continue;
            
            auto descriptor_func = (LADSPA_Descriptor_Function)dlsym(lib, "ladspa_descriptor");
            if (!descriptor_func) {
                dlclose(lib);
                continue;
            }
            
            for (unsigned long i = 0; ; ++i) {
                const LADSPA_Descriptor* desc = descriptor_func(i);
                if (!desc) break;
                
                if (std::string(desc->Label) == label) {
                    info_.library_handle = lib;
                    info_.descriptor = desc;
                    info_.instance = desc->instantiate(desc, (unsigned long)sample_rate);
                    
                    if (info_.instance) {
                        scan_ports();
                        return true;
                    }
                    
                    dlclose(lib);
                    return false;
                }
            }
            
            dlclose(lib);
        }
    }
    
    return false;
}

void LadspaLoader::connect_audio_ports(const std::vector<float*>& inputs,
                                       const std::vector<float*>& outputs) {
    if (!info_.instance || !info_.descriptor) return;
    
    for (size_t i = 0; i < inputs.size() && i < info_.audio_in_ports.size(); ++i) {
        info_.descriptor->connect_port(info_.instance, info_.audio_in_ports[i], inputs[i]);
    }
    
    for (size_t i = 0; i < outputs.size() && i < info_.audio_out_ports.size(); ++i) {
        info_.descriptor->connect_port(info_.instance, info_.audio_out_ports[i], outputs[i]);
    }
}

void LadspaLoader::connect_control_ports(const std::vector<float>& parameters) {
    if (!info_.instance || !info_.descriptor) return;
    
    control_params_ = parameters;
    
    for (size_t i = 0; i < control_params_.size() && i < info_.control_in_ports.size(); ++i) {
        info_.descriptor->connect_port(info_.instance, info_.control_in_ports[i], 
                                      &control_params_[i]);
    }
}

void LadspaLoader::run(unsigned long sample_count) {
    if (info_.instance && info_.descriptor && info_.descriptor->run) {
        info_.descriptor->run(info_.instance, sample_count);
    }
}
