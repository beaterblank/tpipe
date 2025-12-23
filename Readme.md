# tpipe

Audio Transparency tool for Unix-based systems that use JACK audio engine.

## Architecture

### Core Components

- **AppConfig**: Configuration file parser supporting key-value pairs with comments.
- **VoiceIndoorFilter**: Bandpass filter with adaptive noise suppression.
- **Ducker**: Side-chain compressor for dynamic mixing.
- **LadspaLoader**: RAII-based LADSPA plugin loader and manager.
- **AudioEngine**: Main audio processing engine coordinating all components.

## Project Structure

```
tpipe/
├── include/
│   ├── AppConfig.hpp
│   ├── Ducker.hpp
│   ├── VoiceIndoorFilter.hpp
│   ├── LadspaLoader.hpp
│   └── AudioEngine.hpp
├── src/
│   ├── AppConfig.cpp
│   ├── Ducker.cpp
│   ├── VoiceIndoorFilter.cpp
│   ├── LadspaLoader.cpp
│   ├── AudioEngine.cpp
│   └── main.cpp
├── CMakeLists.txt
└── default.conf
```

## Building

```bash
mkdir build && cd build
cmake ..
make
sudo make install
```

### Requirements

- C++17 compatible compiler (GCC 7+, Clang 5+)
- CMake 3.15+
- JACK Audio Connection Kit
- LADSPA DeepFilerNet SDK (optional, for more roubust noise removal)

## Usage

```bash
./AudioPipeline [config_file]
```

If no config file is specified, it defaults to `etc/tpipe/default.conf`.
