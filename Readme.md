# tpipe

Audio Transparency tool for Unix-based systems that use JACK audio engine.

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

Run `tpipe` from the command line to initialize the JACK node.
Currently, the application only creates a **stereo audio interface**.

```bash
tpipe [options]

```

### Options

- **`-c, --config <path>`**: Specify a custom path to a configuration file.
(Defaults to `/etc/tpipe/default.conf` if omitted).
- **`-h, --help`**: Display the help menu and exit.

## Routing Audio

Once `tpipe` is running, it will appear as a node within your JACK graph.
To route audio, you can use a connection manager like **QJackCtl**:

1. **Open the Graph/Patchbay** in QJackCtl.
2. **Input:** Connect an audio capture source (such as your **Microphone**)
          and audio from any application to the `tpipe` input node.
3. **Output:** Direct the `tpipe` output node to your desired playback
           device (such as **System Output**) or another application.
