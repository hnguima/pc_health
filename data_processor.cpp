#include <iostream>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <bson.h>
#include <mongoc.h>
#include <map>

#include "json.hpp"
#include "mqtt/client.h"

#define QOS 1
#define BROKER_ADDRESS "tcp://localhost:1883"

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

void insert_document(mongoc_collection_t *collection, std::string machine_id, std::string timestamp_str, double value)
{
    bson_error_t error;
    bson_oid_t oid;
    bson_t *doc;

    std::tm tm{};
    std::istringstream ss{timestamp_str};
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    std::time_t time_t_timestamp = std::mktime(&tm);

    doc = bson_new();
    BSON_APPEND_UTF8(doc, "machine_id", machine_id.c_str());
    BSON_APPEND_TIME_T(doc, "timestamp", time_t_timestamp);
    BSON_APPEND_DOUBLE(doc, "value", value);

    if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error))
    {
        std::cerr << "Failed to insert doc: " << error.message << std::endl;
    }

    bson_destroy(doc);
}

void insert_alarm(mongoc_collection_t *collection, nlohmann::json data)
{
    bson_error_t error;
    bson_oid_t oid;
    bson_t *doc;

    std::tm tm{};
    std::string timestamp_str = data["timestamp"];
    std::istringstream ss{timestamp_str};
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    std::time_t time_t_timestamp = std::mktime(&tm);

    doc = bson_new();

    std::string machine_id = data["machine_id"];
    BSON_APPEND_UTF8(doc, "machine_id", machine_id.c_str());
    std::string sensor_id = data["sensor_id"];
    BSON_APPEND_UTF8(doc, "sensor_id", sensor_id.c_str());
    BSON_APPEND_TIME_T(doc, "timestamp", time_t_timestamp);
    std::string value = data["value"];
    BSON_APPEND_UTF8(doc, "value", value.c_str());

    if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error))
    {
        std::cerr << "Failed to insert doc: " << error.message << std::endl;
    }

    bson_destroy(doc);
}

std::vector<std::string> split(const std::string &str, char delim)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delim))
    {
        tokens.push_back(token);
    }
    return tokens;
}

class RingBuffer
{
private:
    std::list<double> list;
    int size;

public:
    RingBuffer(int size) : size(size){};

    void push(double val)
    {
        list.push_front(val);

        if (list.size() > size)
        {
            list.pop_back();
        }
    };

    double mean()
    {

        double mean = 0;
        for (auto const &value : list)
        {
            mean += value;
        }

        return mean / list.size();
    };
};

typedef struct
{

    std::thread th;
    int interval;
    int count;
    bool offline;
    double value;

    RingBuffer *rb;

    std::string machine_id;
    std::string sensor_id;

} sensor_data_t;

std::map<std::string, sensor_data_t> sensors;

void sensor_thread(mongoc_database_t *db, std::string key, nlohmann::json sensor_config, mqtt::client &client)
{
    sensors[key].interval = sensor_config["data_interval"];

    while (1)
    {

        if (sensors[key].offline == false && sensors[key].count++ >= 10)
        {
            sensors[key].offline = true;
            sensors[key].count = 0;
            std::cout << key << " did not respond for 10 cycles" << std::endl;

            nlohmann::json j;
            j["timestamp"] = now();
            j["sensor_id"] = sensors[key].sensor_id;
            j["machine_id"] = sensors[key].machine_id;
            j["value"] = "Sensor inativo por mais de 10 intervalos de tempo";

            // Get collection and persist the document.
            mongoc_collection_t *collection = mongoc_database_get_collection(db, "alarm");
            insert_alarm(collection, j);
            mongoc_collection_destroy(collection);
        }

        if (sensors[key].offline == false)
        {

            if (sensors[key].value > sensors[key].rb->mean() * 1.1)
            {
                nlohmann::json j;
                j["timestamp"] = now();
                j["sensor_id"] = sensors[key].sensor_id;
                j["machine_id"] = sensors[key].machine_id;
                j["value"] = "Valor mais que 10%% acima da média: " + std::to_string(sensors[key].value);

                // Get collection and persist the document.
                mongoc_collection_t *collection = mongoc_database_get_collection(db, "alarm");
                insert_alarm(collection, j);
                mongoc_collection_destroy(collection);
            }

            else if (sensors[key].value < sensors[key].rb->mean() * 0.9)
            {
                nlohmann::json j;
                j["timestamp"] = now();
                j["sensor_id"] = sensors[key].sensor_id;
                j["machine_id"] = sensors[key].machine_id;
                j["value"] = "Valor mais que 10%% abaixo da média: " + std::to_string(sensors[key].value);

                // Get collection and persist the document.
                mongoc_collection_t *collection = mongoc_database_get_collection(db, "alarm");
                insert_alarm(collection, j);
                mongoc_collection_destroy(collection);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sensors[key].interval));
    }
}

int main(int argc, char *argv[])
{
    std::string clientId = "data-processor";
    mqtt::client client(BROKER_ADDRESS, clientId);

    // Initialize MongoDB client and connect to the database.
    mongoc_init();
    mongoc_client_t *mongodb_client = mongoc_client_new("mongodb://localhost:27017");
    mongoc_database_t *database = mongoc_client_get_database(mongodb_client, "sensors_data"); // replace with your database name

    // Create an MQTT callback.
    class callback : public virtual mqtt::callback
    {
        mongoc_database_t *db;
        mqtt::client &cli_;

    public:
        callback(mongoc_database_t *db, mqtt::client &cli) : db(db), cli_(cli) {}

        void message_arrived(mqtt::const_message_ptr msg) override
        {
            auto j = nlohmann::json::parse(msg->get_payload());

            std::string topic = msg->get_topic();
            auto topic_parts = split(topic, '/');

            if (topic_parts[1] == "sensor_monitors")
            {

                for (auto it = j["sensors"].rbegin(); it != j["sensors"].rend(); ++it)
                {
                    std::string machine_id = j["machine_id"];
                    std::string sensor_id = it.value()["sensor_id"];
                    std::string key = machine_id + "_" + sensor_id;
                    if (sensors.count(key) != 1)
                    {
                        std::cout << "thread does not exist yet: " << key << std::endl;

                        nlohmann::json data = nlohmann::json::parse(it.value().dump());

                        std::thread th(sensor_thread, db, key, data, std::ref(cli_));
                        sensors[key].th = std::move(th);
                        sensors[key].count = 0;
                        sensors[key].interval = 0;
                        sensors[key].offline = false;
                        sensors[key].machine_id = machine_id;
                        sensors[key].sensor_id = sensor_id;
                        sensors[key].rb = new RingBuffer(32);
                    }
                    // threads[config["machine_id"] + "_" it.value()["sensor_id"]]();
                }
            }

            else if (topic_parts[1] == "sensors")
            {

                std::string machine_id = topic_parts[2];
                std::string sensor_id = topic_parts[3];
                std::string key = machine_id + "_" + sensor_id;

                std::string timestamp = j["timestamp"];
                double value = j["value"];

                if (sensors.count(key) == 1)
                {
                    sensors[key].offline = false;
                    sensors[key].count = 0;
                    sensors[key].value = value;

                    sensors[key].rb->push(value);
                }

                // Get collection and persist the document.
                mongoc_collection_t *collection = mongoc_database_get_collection(db, sensor_id.c_str());
                insert_document(collection, machine_id, timestamp, value);
                mongoc_collection_destroy(collection);
            }
        }
    };

    callback cb(database, std::ref(client));
    client.set_callback(cb);

    // Connect to the MQTT broker.
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    try
    {
        client.connect(connOpts);
        client.subscribe("/sensors/#", QOS);
        client.subscribe("/sensor_monitors/#", QOS);
    }
    catch (mqtt::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Cleanup MongoDB resources
    mongoc_database_destroy(database);
    mongoc_client_destroy(mongodb_client);
    mongoc_cleanup();

    return EXIT_SUCCESS;
}