

#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/PrintingSOEHandler.h>
#include <asiodnp3/PrintingChannelListener.h>
#include <asiodnp3/ConsoleLogger.h>
#include <asiodnp3/UpdateBuilder.h>
#include <asiodnp3/ChannelRetry.h>

#include <asiopal/UTCTimeSource.h>
#include <opendnp3/outstation/SimpleCommandHandler.h>
#include <opendnp3/outstation/IUpdateHandler.h>
#include <opendnp3/LogLevels.h>

#include <string>
#include <thread>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>

using namespace std;
using namespace opendnp3;
using namespace openpal;
using namespace asiopal;
using namespace asiodnp3;

void ConfigureDatabase(DatabaseConfig& config)
{
    config.analog[0].clazz = PointClass::Class2;
    config.analog[0].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[0].evariation = EventAnalogVariation::Group32Var7;

    config.analog[1].clazz = PointClass::Class2;
    config.analog[2].clazz = PointClass::Class2;
}

struct State
{
    double temperature = 0;
    double pressure = 0;
    double humidity = 0;
};

void parse_and_update_sensor_data(const std::string& data, State& state, std::shared_ptr<IOutstation> outstation)
{
    std::stringstream ss(data);
    std::string item;
    std::vector<double> values;

    while (std::getline(ss, item, ','))
    {
        try {
            values.push_back(std::stod(item));
        } catch (...) {}
    }

    if (values.size() >= 3)
    {
        state.temperature = values[0];
        state.pressure = values[1];
        state.humidity = values[2];

        UpdateBuilder builder;
        builder.Update(Analog(state.temperature), 0);
        builder.Update(Analog(state.pressure), 1);
        builder.Update(Analog(state.humidity), 2);

        outstation->Apply(builder.Build());

        std::cout << "[UPDATE] Temperature: " << state.temperature
                  << ", Pressure: " << state.pressure
                  << ", Humidity: " << state.humidity << std::endl;
    }
}

void listen_for_data(std::shared_ptr<IOutstation> outstation)
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1025] = {0};
    State state;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(20000);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 3);

    std::cout << "[LISTEN] TCP server waiting for sensor data on port 20000..." << std::endl;

    while ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)))
    {
        ssize_t bytes_read = read(new_socket, buffer, 1024);
        if (bytes_read > 0)
        {
            buffer[bytes_read] = '\0';
            std::string data(buffer);
            parse_and_update_sensor_data(data, state, outstation);
        }
        close(new_socket);
    }

    close(server_fd);
}

int main(int argc, char* argv[])
{
    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;

    DNP3Manager manager(1, ConsoleLogger::Create());

    auto channel = manager.AddTCPServer(
        "server", 
        FILTERS, 
        asiodnp3::ChannelRetry::Default(), 
        "0.0.0.0", 
        20001,  // Changed to 20001 so 20000 is free for Python
        PrintingChannelListener::Create()
    );

    OutstationStackConfig config(DatabaseSizes::AllTypes(10));
    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);
    config.outstation.params.allowUnsolicited = true;
    config.link.LocalAddr = 10;
    config.link.RemoteAddr = 1;
    config.link.KeepAliveTimeout = openpal::TimeDuration::Max();

    ConfigureDatabase(config.dbConfig);

    auto outstation = channel->AddOutstation(
        "outstation",
        SuccessCommandHandler::Create(),
        DefaultOutstationApplication::Create(),
        config
    );

    outstation->Enable();

    // Start TCP data listener thread
    std::thread listener_thread(listen_for_data, outstation);

    std::cout << "[READY] DNP3 outstation is up. Receiving sensor data..." << std::endl;
    listener_thread.join();

    return 0;
}
