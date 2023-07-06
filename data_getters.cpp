#include <iostream>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <thread>
#include <unistd.h>
#include <fstream>
#include <iomanip>

#define QOS 1
#define BROKER_ADDRESS "tcp://localhost:1883"

#include <fstream>
#include <sstream>
#include <string>

// Function to get the used disk percentage
double getUsedDiskPercentage() {
    double percentage = 0.0;

    // Open the "/proc/mounts" file to read disk information
    std::ifstream mounts("/proc/mounts");
    if (mounts.is_open()) {
        std::string line;
        while (std::getline(mounts, line)) {
            // Split the line by spaces
            std::istringstream iss(line);
            std::string device, mountPoint, fsType;
            iss >> device >> mountPoint >> fsType;

            // Skip non-filesystem lines
            if (fsType != "ext4" && fsType != "ext3" && fsType != "ext2") {
                continue;
            }

            // Get disk space information using the mount point
            std::ifstream statvfs(mountPoint);
            if (statvfs.is_open()) {
                // Get the total disk space and used disk space
                unsigned long long totalSpace, usedSpace;
                statvfs >> totalSpace >> usedSpace;

                // Calculate the used disk space percentage
                if (totalSpace > 0) {
                    percentage = static_cast<double>(usedSpace) / totalSpace * 100.0;
                }

                statvfs.close();
                break;  // Exit the loop after finding the first valid filesystem
            }
        }
        mounts.close();
    }

    return percentage;
}

double getUsedCPUPercentage() {
    double percentage = 0.0;

    // Open the "/proc/stat" file to read CPU information
    std::ifstream stat("/proc/stat");
    if (stat.is_open()) {
        std::string line;
        while (std::getline(stat, line)) {
            if (line.substr(0, 3) == "cpu") {
                std::istringstream iss(line);
                std::string cpuLabel;
                iss >> cpuLabel;

                // Check if the line corresponds to the "cpu" or "cpu0" entry
                if (cpuLabel == "cpu" || cpuLabel == "cpu0") {
                    long user, nice, system, idle, iowait, irq, softirq, steal, guest, guestNice;
                    iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guestNice;

                    // Calculate the total CPU time
                    long totalCpuTime = user + nice + system + idle + iowait + irq + softirq + steal;

                    // Calculate the idle CPU time
                    long idleCpuTime = idle + iowait;

                    // Calculate the used CPU time
                    long usedCpuTime = totalCpuTime - idleCpuTime;

                    // Calculate the used CPU percentage
                    percentage = static_cast<double>(usedCpuTime) / totalCpuTime * 100.0;
                    break;
                }
            }
        }
        stat.close();
    }

    return percentage;
}

