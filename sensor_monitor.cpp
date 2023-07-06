#include <iostream>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <thread>
#include <unistd.h>
#include <fstream>
#include "json.hpp" // json handling
#include "data_getters.hpp"
#include "mqtt/client.h" // paho mqtt
#include <iomanip>

#define QOS 1
#define BROKER_ADDRESS "tcp://localhost:1883"

#include <fstream>
#include <sstream>
#include <string>

std::string now()
{
    // Get the current time in ISO 8601 format
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm *now_tm = std::localtime(&now_c);
    std::stringstream ss;
    ss << std::put_time(now_tm, "%FT%TZ");
    std::string timestamp = ss.str();

    return timestamp;
}

// Function for the first thread to publish memory usage
void publishDiskUsage(nlohmann::json config, mqtt::client &client)
{
    std::clog << "publishDiskUsage" << std::endl;
    std::string sensor_id = "";
    std::string machine_id = config["machine_id"];

    nlohmann::json sensor_config;
    nlohmann::json j;

    for (auto it = config["sensors"].rbegin(); it != config["sensors"].rend(); ++it)
    {
        if (it.value()["sensor_id"] == "disk_usage")
        {
            sensor_id = it.value()["sensor_id"];
            sensor_config = it.value();
            break;
        }
    }

    while (true)
    {
        // Construct the JSON message

        j["timestamp"] = now();
        j["value"] = getUsedDiskPercentage();

        // Publish the JSON message to the appropriate topic
        std::string topic = "/sensors/" + machine_id + "/" + sensor_id;
        mqtt::message msg(topic, j.dump(), QOS, false);
        std::clog << "Memory message published - topic: " << topic << " - message: " << j.dump() << std::endl;
        client.publish(msg);

        // // Sleep for some time
        std::this_thread::sleep_for(std::chrono::milliseconds(sensor_config["data_interval"]));
    }
}

// Function for the second thread to publish CPU usage
void publishCPUUsage(nlohmann::json config, mqtt::client &client)
{
    std::clog << "publishCPUUsage" << std::endl;

    std::string sensor_id = "";
    std::string machine_id = config["machine_id"];

    nlohmann::json sensor_config;
    nlohmann::json j;

    for (auto it = config["sensors"].rbegin(); it != config["sensors"].rend(); ++it)
    {
        if (it.value()["sensor_id"] == "cpu_usage")
        {
            sensor_id = it.value()["sensor_id"];
            sensor_config = it.value();
            break;
        }
    }

    while (true)
    {
        // Construct the JSON message
        j["timestamp"] = now();
        j["value"] = getUsedCPUPercentage();

        // Publish the JSON message to the appropriate topic
        std::string topic = "/sensors/" + machine_id + "/" + sensor_id;
        mqtt::message msg(topic, j.dump(), QOS, false);
        std::clog << "Memory message published - topic: " << topic << " - message: " << j.dump() << std::endl;
        client.publish(msg);

        // // Sleep for some time
        std::this_thread::sleep_for(std::chrono::milliseconds(sensor_config["data_interval"]));
    }
}

// Function for the second thread to publish CPU usage
void publish_config_message(nlohmann::json config, mqtt::client &client)
{
    std::clog << "publish_config_message" << std::endl;

    while (true)
    {
        // Publish the JSON message to the appropriate topic
        std::string topic = "/sensor_monitors";
        mqtt::message msg(topic, config.dump(), QOS, false);
        std::clog << "config message published - topic: " << topic << " - message: " << config.dump() << std::endl;
        client.publish(msg);

        // // Sleep for some time
        std::this_thread::sleep_for(std::chrono::milliseconds(config["config_message_interval"]));
    }
}

int main()
{
    std::string clientId = "sensor-monitor";
    mqtt::client client(BROKER_ADDRESS, clientId);
    nlohmann::json data;

    // Connect to the MQTT broker.
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    try
    {
        client.connect(connOpts);
    }
    catch (mqtt::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    std::clog << "connected to the broker" << std::endl;

    // Get the unique machine identifier, in this case, the hostname.
    char hostname[1024];
    gethostname(hostname, 1024);
    std::string machineId(hostname);

    // Get the config file
    std::ifstream f("/workspaces/machine-health-monitoring/getter_config.json");
    if (!f.is_open())
    {
        std::cout << "Failed to open the file." << std::endl;
    }
    else
    {
        std::string fileContent((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
        data = nlohmann::json::parse(fileContent);
    }

    data["machine_id"] = machineId;

    // Start the config publishing thread
    std::thread configThread(publish_config_message, data, std::ref(client));

    // Start the memory usage publishing thread
    std::thread memoryThread(publishDiskUsage, data, std::ref(client));

    // Start the CPU usage publishing thread
    std::thread cpuThread(publishCPUUsage, data, std::ref(client));

    // Wait for the threads to finish (which will be never in this case, since the loops run indefinitely)
    // memoryThread.join();
    cpuThread.join();

    return 0;
}
