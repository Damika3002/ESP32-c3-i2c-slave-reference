#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// --- PROTOCOL CONSTANTS ---
constexpr uint8_t I2C_ADDRESS           = 0x08;
constexpr uint8_t FRAME_START_BYTE      = 0x02;
constexpr uint8_t FRAME_END_BYTE        = 0x03;
constexpr size_t  FRAME_LENGTH          = 6;
constexpr int     BROADCAST_INTERVAL_MS = 2000;

// --- GRACEFUL SHUTDOWN FLAG ---
std::atomic<bool> shouldRun{true};

void handleSigint(int /*signalNumber*/) {
    shouldRun.store(false);
}

// --- SENSOR DEFINITION ---
struct ThermalSensor {
    uint8_t     sensorId;
    std::string sensorName;
    std::string sysfsPath;
};

const std::vector<ThermalSensor> thermalSensorList = {
    {0x01, "soc-thermal",        "/sys/class/thermal/thermal_zone0/temp"},
    {0x02, "bigcore0-thermal",   "/sys/class/thermal/thermal_zone1/temp"},
    {0x03, "bigcore1-thermal",   "/sys/class/thermal/thermal_zone2/temp"},
    {0x04, "littlecore-thermal", "/sys/class/thermal/thermal_zone3/temp"},
    {0x05, "center-thermal",     "/sys/class/thermal/thermal_zone4/temp"},
    {0x06, "gpu-thermal",        "/sys/class/thermal/thermal_zone5/temp"},
    {0x07, "npu-thermal",        "/sys/class/thermal/thermal_zone6/temp"}
};

int readRawMilliDegrees(const std::string& sysfsPath) {
    std::ifstream tempFile(sysfsPath);
    if (!tempFile.is_open()) return -1;

    int milliDegrees = 0;
    tempFile >> milliDegrees;
    return tempFile.fail() ? -1 : milliDegrees;
}

void printThermalReport(const std::vector<ThermalSensor>& sensors) {
    std::cout << "\n--- Current Thermal Status ---" << std::endl;
    for (const auto& sensor : sensors) {
        int rawMilliDegrees = readRawMilliDegrees(sensor.sysfsPath);
        if (rawMilliDegrees < 0) {
            std::cout << std::left << std::setw(20) << sensor.sensorName
                       << ": READ ERROR" << std::endl;
            continue;
        }
        double celsius = rawMilliDegrees / 1000.0;
        std::cout << std::left << std::setw(20) << sensor.sensorName
                   << ": " << std::fixed << std::setprecision(2)
                   << celsius << " C" << std::endl;
    }
}

uint8_t calculateChecksum(const uint8_t* payload, size_t payloadLength) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < payloadLength; ++i) checksum ^= payload[i];
    return checksum;
}

int openI2CDevice(const std::string& devicePath) {
    int i2cFd = open(devicePath.c_str(), O_RDWR);
    if (i2cFd < 0) {
        perror("Failed to open I2C device");
        return -1;
    }

    if (ioctl(i2cFd, I2C_SLAVE, I2C_ADDRESS) < 0) {
        perror("Failed to set I2C slave address");
        close(i2cFd);
        return -1;
    }

    std::cout << "I2C device opened: " << devicePath 
              << " (address 0x" << std::hex << (int)I2C_ADDRESS << std::dec << ")" << std::endl;
    
    return i2cFd;
}

bool sendSensorFrameI2C(int i2cFd, uint8_t sensorId, int rawMilliDegrees) {
    uint16_t tempX10 = static_cast<uint16_t>(rawMilliDegrees / 100);

    uint8_t frame[FRAME_LENGTH];
    frame[0] = FRAME_START_BYTE;
    frame[1] = sensorId;
    frame[2] = static_cast<uint8_t>(tempX10 >> 8);
    frame[3] = static_cast<uint8_t>(tempX10 & 0xFF);
    frame[4] = calculateChecksum(&frame[1], 3);
    frame[5] = FRAME_END_BYTE;

    ssize_t bytesWritten = write(i2cFd, frame, FRAME_LENGTH);
    
    if (bytesWritten != static_cast<ssize_t>(FRAME_LENGTH)) {
        std::cerr << "I2C write failed: " << bytesWritten << " bytes written" << std::endl;
        return false;
    }

    return true;
}

std::string promptForI2CPort() {
    const std::vector<std::string> availablePorts = {
        "/dev/i2c-0", "/dev/i2c-1", "/dev/i2c-2", 
        "/dev/i2c-3", "/dev/i2c-4", "/dev/i2c-5", "/dev/i2c-6"
    };

    std::cout << "Select I2C bus:\n";
    for (size_t i = 0; i < availablePorts.size(); ++i) {
        std::cout << "  " << (i + 1) << ": " << availablePorts[i] << "\n";
    }
    std::cout << "Enter number (default 7 for /dev/i2c-6): ";

    int choice = 0;
    std::cin >> choice;

    if (choice < 1 || choice > static_cast<int>(availablePorts.size())) {
        std::cout << "Defaulting to /dev/i2c-6" << std::endl;
        return "/dev/i2c-6";
    }
    return availablePorts[choice - 1];
}

int main() {
    std::signal(SIGINT, handleSigint);

    std::string selectedPort = promptForI2CPort();

    int i2cFd = openI2CDevice(selectedPort);
    if (i2cFd < 0) return 1;

    printThermalReport(thermalSensorList);

    std::cout << "\nStarting I2C temperature broadcast every "
               << BROADCAST_INTERVAL_MS << "ms (Ctrl+C to stop)...\n";

    uint32_t frameCount = 0;

    while (shouldRun.load()) {
        for (const auto& sensor : thermalSensorList) {
            if (!shouldRun.load()) break;

            int rawMilliDegrees = readRawMilliDegrees(sensor.sysfsPath);
            if (rawMilliDegrees < 0) {
                std::cerr << "Skipping " << sensor.sensorName << " (read error)\n";
                continue;
            }

            if (!sendSensorFrameI2C(i2cFd, sensor.sensorId, rawMilliDegrees)) {
                std::cerr << "I2C send failed for " << sensor.sensorName << std::endl;
            }
            
            frameCount++;
            usleep(10000);
        }

        for (int elapsedMs = 0; elapsedMs < BROADCAST_INTERVAL_MS && shouldRun.load(); elapsedMs += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    std::cout << "\nShutdown signal received. Total frames sent: " 
              << frameCount << std::endl;
    close(i2cFd);
    return 0;
}