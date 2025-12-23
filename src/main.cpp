#include "AudioEngine.hpp"
#include "AppConfig.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <filesystem>


#ifndef DEFAULT_CONFIG_PATH
#define DEFAULT_CONFIG_PATH "/etc/tpipe/default.conf"
#endif

namespace {
    std::atomic<bool> keep_running{true};
    
    void signal_handler(int signal) {
        std::cout << "\n[Interrupt signal (" << signal << ") received]. Cleaning up...\n";
        keep_running = false;
    }
    
    void setup_signal_handlers() {
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
    }
}


void print_usage(const char* bin_name) {
    std::cout << "Usage: " << bin_name << " [options]\n"
              << "Options:\n"
              << "  -c, --config <path>    Path to configuration file\n"
              << "  -h, --help             Show this help message\n";
}

int main(int argc, char* argv[]) {
    std::string config_file = DEFAULT_CONFIG_PATH;
    std::vector<std::string> args(argv + 1, argv + argc);

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-h" || args[i] == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (args[i] == "-c" || args[i] == "--config") {
            if (i + 1 < args.size()) {
                config_file = args[++i];
            } else {
                std::cerr << "Error: -c requires a file path.\n";
                return 1;
            }
        }
    }

    if (!std::filesystem::exists(config_file)) {
        std::cerr << "Error: Configuration file not found: " << config_file << "\n";
        return 1;
    }

    std::cout << "Starting tpipe with config: " << config_file << "\n";
    
    AppConfig config;
    if (!config.load(config_file)) {
        std::cerr << "Error: Failed to load configuration from '" 
                  << config_file << "'\n";
        return 1;
    }
    
    AudioEngine engine(config);
    
    if (!engine.initialize()) {
        std::cerr << "Error: Failed to initialize audio engine\n";
        return 1;
    }
    
    std::cout << "Audio engine running. Press Ctrl+C to stop.\n";
    
    setup_signal_handlers();
    
    while (keep_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::cout << "Shutting down gracefully...\n";
    
    return 0;
}
