#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/PrintingChannelListener.h>
#include <asiodnp3/ConsoleLogger.h>
#include <asiodnp3/UpdateBuilder.h>
#include <asiopal/UTCTimeSource.h>
#include <opendnp3/outstation/SimpleCommandHandler.h>
#include <opendnp3/LogLevels.h>

#include <iostream>
#include <string>
#include <thread>
#include <sstream>
#include <vector>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>

using namespace std;
using namespace opendnp3;
using namespace asiodnp3;

// === DATABASE CONFIGURATION ===

void ConfigureDatabase(DatabaseConfig& config)
{
    config.analog[0].clazz = PointClass::Class1;
    config.analog[1].clazz = PointClass::Class1;
    config.analog[2].clazz = PointClass::Class1;
}

// === SENSOR DATA PARSER ===

void parse_and_update_sensor_data(const std::string& data, std::shared_ptr<IOutstation> outstation)
{
    std::stringstream ss(data);
    std::string item;
    std::vector<double> values;

    while (std::getline(ss, item, ','))
    {
        try {
            values.push_back(std::stod(item));
        } catch (...) {
            std::cerr << "[ERROR] Failed to convert sensor value: " << item << std::endl;
            return;
        }
    }

    if (values.size() >= 3)
    {
        UpdateBuilder builder;
        builder.Update(Analog(values[0]), 0); // Temperature
        builder.Update(Analog(values[1]), 1); // Pressure
        builder.Update(Analog(values[2]), 2); // Humidity

        outstation->Apply(builder.Build());

        std::cout << "[INFO] Updated DNP3 Analog Points - Temp: " << values[0]
                  << ", Pressure: " << values[1] << ", Humidity: " << values[2] << std::endl;
    }
    else
    {
        std::cerr << "[ERROR] Not enough values received. Expected 3, got " << values.size() << std::endl;
    }
}

// === SOCKET LISTENER THREAD ===

void listen_for_data(std::shared_ptr<IOutstation> outstation)
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(20000);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 3);

    std::cout << "[INFO] Listening for sensor data on port 20000..." << std::endl;

    while (true)
    {
        new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        memset(buffer, 0, sizeof(buffer));
        read(new_socket, buffer, 1024);
        std::string data(buffer);
        std::cout << "[RECV] Sensor Data: " << data << std::endl;
        parse_and_update_sensor_data(data, outstation);
        close(new_socket);
    }
}

// === MAIN ===

int main(int argc, char* argv[])
{
    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;
    DNP3Manager manager(1, ConsoleLogger::Create());

    auto channel = manager.AddTCPServer("server", FILTERS, ChannelRetry::Default(),
                                        "0.0.0.0", 20001, PrintingChannelListener::Create());

    OutstationStackConfig config(DatabaseSizes::AllTypes(10));
    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);
    config.outstation.params.allowUnsolicited = true;
    config.link.LocalAddr = 10;
    config.link.RemoteAddr = 1;

    ConfigureDatabase(config.dbConfig);

    auto outstation = channel->AddOutstation("outstation",
                                             SuccessCommandHandler::Create(),
                                             Default

