/*
This file implements the integration between Wearable Sensing DSI C/C++ API and
the LSL library.

This is a C++ conversion of the original C code.
Copyright (C) 2014-2020 Syntrogi Inc dba Intheon.
*/

#include "DSI.h"
#include "lsl_c.h"
#include <iostream>
#include <string>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <random>

// --- C++ Forward Declarations ---
int          StartUp(int argc, const char* argv[], DSI_Headset* headsetOut, bool* helpOut);
int          Finish(DSI_Headset h);
int          GlobalHelp(const char* appName);
lsl_outlet   InitLSL(DSI_Headset h, const std::string& streamName);
void         OnSample(DSI_Headset h, double packetOffsetTime, void* userData);
std::string  getRandomString(int len);
const char* GetStringOpt(int argc, const char* argv[], const char* keyword1, const char* keyword2);
int          GetIntegerOpt(int argc, const char* argv[], const char* keyword1, const char* keyword2, int defaultValue);
int          startAnalogReset(DSI_Headset h);
int          CheckImpedance(DSI_Headset h);
void         PrintImpedances(DSI_Headset h, double packetOffsetTime, void* userData);
void         streamLoop(DSI_Headset h);

// --- Global Variables ---
// Use a std::vector for the sample buffer, managed automatically.
std::vector<float> sample;
// Use std::atomic for thread-safe signaling.
std::atomic<bool> keepRunning(true);

// --- Signal Handler ---
void QuitHandler(int signal) {
    keepRunning = false;
}

// --- Error Checking ---
// Error checking remains mostly the same, but uses C++ streams.
int CheckError() {
    if (DSI_Error()) {
        std::cerr << "DSI Error: " << DSI_ClearError() << std::endl;
        return -1;
    }
    return 0;
}
#define CHECK if (CheckError() != 0) return -1;

// --- Streaming Thread Function ---
// The loop that keeps the DSI connection alive.
void streamLoop(DSI_Headset h) {
    std::cout << "Streaming thread started." << std::endl;
    while (keepRunning) {
        DSI_Headset_Idle(h, 0.0);
    }
    std::cout << "Streaming thread stopping." << std::endl;
}

// --- Main Application Logic ---
int main(int argc, const char* argv[]) {
    // Load the libDSI dynamic library
    const char* dllname = nullptr;
    int load_error = Load_DSI_API(dllname);
    if (load_error < 0) {
        std::cerr << "Failed to load dynamic library \"" << DSI_DYLIB_NAME(dllname) << "\"" << std::endl;
        return 1;
    }
    if (load_error > 0) {
        std::cerr << "Failed to import " << load_error << " functions from dynamic library \"" << DSI_DYLIB_NAME(dllname) << "\"" << std::endl;
        return 1;
    }
    std::cout << "DSI API version " << DSI_GetAPIVersion() << " loaded" << std::endl;
    if (strcmp(DSI_GetAPIVersion(), DSI_API_VERSION) != 0) {
        std::cerr << "WARNING - mismatched versioning: program was compiled with DSI.h version " << DSI_API_VERSION
                  << " but just loaded shared library version " << DSI_GetAPIVersion()
                  << ". You should ensure that you are using matching versions of the API files." << std::endl;
    }

    // Set up Ctrl+C handler to gracefully quit.
    signal(SIGINT, QuitHandler);

    // Initialize the API and headset
    DSI_Headset h;
    bool help = false;
    if (StartUp(argc, argv, &h, &help) != 0 || help) {
        GlobalHelp(argv[0]);
        return help ? 0 : 1;
    }

    // Initialize LSL outlet
    const char* streamNameCStr = GetStringOpt(argc, argv, "lsl-stream-name", "m");
    std::string streamName = streamNameCStr ? streamNameCStr : "WS-default";
    std::cout << "Initializing LSL outlet with name: " << streamName << std::endl;
    lsl_outlet outlet = InitLSL(h, streamName);
    CHECK;

    // Set the sample callback
    DSI_Headset_SetSampleCallback(h, OnSample, outlet);
    CHECK;

    // Start data acquisition
    std::cout << "Starting data acquisition..." << std::endl;
    DSI_Headset_StartDataAcquisition(h);
    CHECK;

    // Start the streaming thread using std::thread
    std::cout << "Starting data streaming thread..." << std::endl;
    std::thread streamThread(streamLoop, h);

    // Main command loop
    std::cout << "Ready for commands (e.g., 'checkZ', 'resetZ', 'exit')." << std::endl;
    std::string command_line;
    while (keepRunning) {
        if (!std::getline(std::cin, command_line)) {
            if (std::cin.eof()) {
                std::cout << "\nInput stream closed (EOF). Exiting." << std::endl;
                keepRunning = false; // Signal other threads to stop
                break;
            }
            std::cerr << "Error reading input." << std::endl;
            break;
        }
        
        if (command_line.empty()) continue;

        // Parse command using stringstream
        std::stringstream ss(command_line);
        std::string command;
        ss >> command;

        if (command == "exit") {
            keepRunning = false; // Signal exit
        } else if (command == "checkZ") {
            CheckImpedance(h);
            command = ' ';
            CHECK;
        } else if (command == "resetZ") {
            startAnalogReset(h); CHECK;
            command = ' ';
            
        } else {
            std::cout << "Unknown command: '" << command << "'" << std::endl;
        }
    }

    // Wait for the streaming thread to finish
    std::cout << "Waiting for data thread to join..." << std::endl;
    if (streamThread.joinable()) {
        streamThread.join();
    }

    // Gracefully exit
    std::cout << "\n" << argv[0] << " will exit now..." << std::endl;
    lsl_destroy_outlet(outlet);
    return Finish(h);
}

// --- DSI and LSL Functions ---

int startAnalogReset(DSI_Headset h) {
    if (h == nullptr) {
        std::cerr << "Error: Invalid headset handle." << std::endl;
        return -1;
    }
    std::cerr << "---------Starting Analog Reset----------------" << std::endl;
    std::cout << "--> Initial analog reset mode: " << DSI_Headset_GetAnalogResetMode(h) << std::endl;
    
    DSI_Headset_StartAnalogReset(h);
    CHECK;
    
    std::cout << "--> Waiting 3 seconds for reset to complete..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    std::cerr << "---------Analog Reset Complete----------------" << std::endl;
    return 0;
}

int CheckImpedance(DSI_Headset h) {
    // DSI_Headset_StopDataAcquisition(h);
    std::cerr << "---------Starting Impedance Driver----------------" << std::endl;
    DSI_Headset_StartImpedanceDriver(h);
    CHECK;
    
    std::cerr << "---------Starting Data Acquisition----------------" << std::endl;
    DSI_Headset_StartDataAcquisition(h);
    CHECK;
    
    PrintImpedances(h, 0, (void*)1); // Print headings
    CHECK;
    
    DSI_Headset_SetSampleCallback(h, PrintImpedances, nullptr);
    CHECK;

    DSI_Headset_Receive( h, -1, 1.0 ); CHECK
    return 0;
}

void OnSample(DSI_Headset h, double packetOffsetTime, void* outlet) {
    unsigned int numberOfChannels = DSI_Headset_GetNumberOfChannels(h);
    if (sample.size() != numberOfChannels) {
        sample.resize(numberOfChannels);
    }
    for (unsigned int i = 0; i < numberOfChannels; ++i) {
        sample[i] = static_cast<float>(DSI_Channel_GetSignal(DSI_Headset_GetChannelByIndex(h, i)));
    }
    lsl_push_sample_f(static_cast<lsl_outlet>(outlet), sample.data());
}

int Message(const char* msg, int debugLevel) {
    std::cerr << "DSI Message (level " << debugLevel << "): " << msg << std::endl;
    return 0;
}

int StartUp(int argc, const char* argv[], DSI_Headset* headsetOut, bool* helpOut) {
    const char* serialPort = GetStringOpt(argc, argv, "port", "p");
    const char* montage = GetStringOpt(argc, argv, "montage", "m");
    const char* reference = GetStringOpt(argc, argv, "reference", "r");
    int verbosity = GetIntegerOpt(argc, argv, "verbosity", "v", 2);
    
    if (helpOut) *helpOut = (GetStringOpt(argc, argv, "help", "h") != nullptr);
    if (headsetOut) *headsetOut = nullptr;
    if (helpOut && *helpOut) return 0;
    
    DSI_Headset h = DSI_Headset_New(nullptr);
    CHECK;
    
    DSI_Headset_SetMessageCallback(h, Message);
    CHECK;
    DSI_Headset_SetVerbosity(h, verbosity);
    CHECK;
    
    DSI_Headset_Connect(h, serialPort);
    CHECK;
    
    DSI_Headset_ChooseChannels(h, montage, reference, 1);
    CHECK;
    
    std::cerr << DSI_Headset_GetInfoString(h) << std::endl;
    CHECK;
    
    if (headsetOut) *headsetOut = h;
    return 0;
}

int Finish(DSI_Headset h) {
    DSI_Headset_SetSampleCallback(h, nullptr, nullptr);
    CHECK;
    DSI_Headset_StopDataAcquisition(h);
    CHECK;
    DSI_Headset_StopImpedanceDriver(h);
    CHECK;
    DSI_Headset_Idle(h, 1.0);
    CHECK;
    DSI_Headset_Delete(h);
    CHECK;
    return 0;
}

std::string getRandomString(int len) {
    static const char alphanum[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string s(len, 0);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, sizeof(alphanum) - 2);
    for (int i = 0; i < len; ++i) {
        s[i] = alphanum[distrib(gen)];
    }
    return s;
}

lsl_outlet InitLSL(DSI_Headset h, const std::string& streamName) {
    unsigned int numberOfChannels = DSI_Headset_GetNumberOfChannels(h);
    int samplingRate = DSI_Headset_GetSamplingRate(h);
    std::string source_id = getRandomString(15);

    lsl_streaminfo info = lsl_create_streaminfo(const_cast<char*>(streamName.c_str()), "EEG", numberOfChannels, samplingRate, cft_float32, const_cast<char*>(source_id.c_str()));
    
    lsl_xml_ptr desc = lsl_get_desc(info);
    lsl_append_child_value(desc, "manufacturer", "WearableSensing");
    
    lsl_xml_ptr chns = lsl_append_child(desc, "channels");
    for (unsigned int i = 0; i < numberOfChannels; ++i) {
        lsl_xml_ptr chn = lsl_append_child(chns, "channel");
        
        const char* long_label_c = DSI_Channel_GetString(DSI_Headset_GetChannelByIndex(h, i));
        std::string long_label(long_label_c ? long_label_c : "");
        std::string short_label = long_label.substr(0, long_label.find('-'));

        lsl_append_child_value(chn, "label", const_cast<char*>(short_label.c_str()));
        lsl_append_child_value(chn, "unit", "microvolts");
        lsl_append_child_value(chn, "type", "EEG");
    }
    
    const char* reference = DSI_Headset_GetReferenceString(h);
    lsl_xml_ptr ref = lsl_append_child(desc, "reference");
    lsl_append_child_value(ref, "label", reference ? reference : "Unknown");
    std::cout << "LSL Reference: " << (reference ? reference : "Unknown") << std::endl;

    return lsl_create_outlet(info, 0, 360);
}

// --- Helper and Display Functions ---

int GlobalHelp(const char* appName) {
    std::cerr << "Usage: " << appName << " [ --OPTIONS... ]\n\n"
              << "With the exception of --help,\n"
              << "the options should be given in --NAME=VALUE format.\n\n"
              << "  --help\n"
              << "      Displays this help text.\n\n"
              << "  --port\n"
              << "      Specifies the serial port address (e.g. --port=COM4).\n\n"
              << "  --montage\n"
              << "      A comma-separated list of channel specifications.\n\n"
              << "  --reference\n"
              << "      The sensor(s) to be used as reference.\n\n"
              << "  --verbosity\n"
              << "      The higher the number, the more messages are printed (default 2).\n\n"
              << "  --lsl-stream-name\n"
              << "      The name of the LSL outlet (default WS-default).\n\n";
    return 0;
}

// These two functions are kept from the original for command-line parsing.
const char* GetStringOpt(int argc, const char* argv[], const char* keyword1, const char* keyword2) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!arg) continue;
        const char* p = arg;
        if (*p == '-' || *p == '/') ++p;
        if (*p == '-' || *p == '/') ++p;

        for (int j = 0; j < 2; ++j) {
            const char* keyword = (j == 0) ? keyword1 : keyword2;
            if (keyword && strncmp(p, keyword, strlen(keyword)) == 0) {
                const char* val = p + strlen(keyword);
                if (*val == '=' || *val == ':') return val + 1;
                if (*val == '\0') return val;
            }
        }
    }
    return nullptr;
}

int GetIntegerOpt(int argc, const char* argv[], const char* keyword1, const char* keyword2, int defaultValue) {
    const char* stringValue = GetStringOpt(argc, argv, keyword1, keyword2);
    if (!stringValue || !*stringValue) return defaultValue;
    char* end;
    long result = strtol(stringValue, &end, 10);
    if (end != stringValue && *end == '\0') return static_cast<int>(result);
    std::cerr << "WARNING: could not interpret \"" << stringValue << "\" as an integer for \"" << keyword1
              << "\". Reverting to default " << defaultValue << std::endl;
    return defaultValue;
}

void PrintImpedances(DSI_Headset h, double packetOffsetTime, void* userData) {
    std::cout << std::fixed << std::setprecision(4);

    if (userData) std::cout << std::setw(9) << "Time";
    else std::cout << std::setw(9) << packetOffsetTime;

    unsigned int numberOfSources = DSI_Headset_GetNumberOfSources(h);
    for (unsigned int i = 0; i < numberOfSources; ++i) {
        DSI_Source s = DSI_Headset_GetSourceByIndex(h, i);
        if (DSI_Source_IsReferentialEEG(s) && !DSI_Source_IsFactoryReference(s)) {
            if (userData) std::cout << "," << std::setw(9) << DSI_Source_GetName(s);
            else std::cout << "," << std::setw(9) << DSI_Source_GetImpedanceEEG(s);
        }
    }

    if (userData) std::cout << ", CMF=" << DSI_Headset_GetFactoryReferenceString(h) << std::endl;
    else std::cout << "," << std::setw(9) << DSI_Headset_GetImpedanceCMF(h) << std::endl;
}