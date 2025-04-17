#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/PrintingSOEHandler.h>
#include <asiodnp3/PrintingChannelListener.h>
#include <asiodnp3/ConsoleLogger.h>
#include <asiodnp3/UpdateBuilder.h>

#include <asiopal/UTCTimeSource.h>
#include <opendnp3/outstation/SimpleCommandHandler.h>

#include <opendnp3/outstation/IUpdateHandler.h>

#include <opendnp3/LogLevels.h>

#include <string>
#include <thread>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <sys/socket.h>
#include <arpa/inet.h>

using namespace std;
using namespace opendnp3;
using namespace openpal;
using namespace asiopal;
using namespace asiodnp3;

void ConfigureDatabase(DatabaseConfig& config)
{
    // example of configuring analog index 0 for Class2 with floating point variations by default
    config.analog[0].clazz = PointClass::Class2;
    config.analog[0].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[0].evariation = EventAnalogVariation::Group32Var7;
    config.analog[1].clazz = PointClass::Class2;
    config.analog[1].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[1].evariation = EventAnalogVariation::Group32Var7;
    config.analog[2].clazz = PointClass::Class2;
    config.analog[2].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[2].evariation = EventAnalogVariation::Group32Var7;
}

struct State
{
    double temperature = 0;
    double pressure = 0;
    double humidity = 0;
};

void parse_and_update_sensor_data(const std::string& data, State& state, Outstation& outstation)
{
    std::istringstream stream(data);
    std::string token;

    // Parse temperature, pressure, and humidity
    if (std::getline(stream, token, ','))
        state.temperature = std::stod(token);
    if (std::getline(stream, token, ','))
        state.pressure = std::stod(token);
    if (std::getline(stream, token, ','))
        state.humidity = std::stod(token);

    // Update DNP3 analog points with new sensor data
    UpdateBuilder builder;

    builder.Update(Analog(state.temperature), 0);  // Temperature to Analog 0
    builder.Update(Analog(state.pressure), 1);     // Pressure to Analog 1
    builder.Update(Analog(state.humidity), 2);     // Humidity to Analog 2

    // Apply updates to outstation
    outstation.Apply(builder.Build());

    // Print the updated values for debugging
    std::cout << "[INFO] Updated sensor data: Temperature = " << state.temperature
              << ", Pressure = " << state.pressure << ", Humidity = " << state.humidity << std::endl;
}

void listen_for_data(Outstation& outstation, State& state)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "[ERROR] Unable to create socket." << std::endl;
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(20000);  // Listening on port 20000

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[ERROR] Unable to bind socket." << std::endl;
        close(sockfd);
        return;
    }

    if (listen(sockfd, 5) < 0) {
        std::cerr << "[ERROR] Unable to listen on socket." << std::endl;
        close(sockfd);
        return;
    }

    std::cout << "[INFO] Listening for incoming data on port 20000..." << std::endl;

    while (true) {
        int client_sockfd = accept(sockfd, nullptr, nullptr);
        if (client_sockfd < 0) {
            std::cerr << "[ERROR] Unable to accept client connection." << std::endl;
            continue;
        }

        char buffer[1024];
        ssize_t bytes_received = recv(client_sockfd, buffer, sizeof(buffer), 0);
        if (bytes_received < 0) {
            std::cerr << "[ERROR] Failed to receive data." << std::endl;
            close(client_sockfd);
            continue;
        }

        buffer[bytes_received] = '\0';
        std::string received_data(buffer);
        std::cout << "[INFO] Received data: " << received_data << std::endl;

        // Parse and update sensor data
        parse_and_update_sensor_data(received_data, state, outstation);

        close(client_sockfd);
    }

    close(sockfd);
}

int main(int argc, char* argv[])
{
    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;

    DNP3Manager manager(1, ConsoleLogger::Create());

    auto channel = manager.AddTCPServer("server", FILTERS, ChannelRetry::Default(), "0.0.0.0", 20000, PrintingChannelListener::Create());

    OutstationStackConfig config(DatabaseSizes::AllTypes(10));
    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);
    config.outstation.params.allowUnsolicited = true;

    config.link.LocalAddr = 10;
    config.link.RemoteAddr = 1;
    config.link.KeepAliveTimeout = openpal::TimeDuration::Max();

    ConfigureDatabase(config.dbConfig);

    auto outstation = channel->AddOutstation("outstation", SuccessCommandHandler::Create(), DefaultOutstationApplication::Create(), config);
    outstation->Enable();

    State state;

    // Listen for incoming sensor data in a separate thread
    std::thread listener_thread(listen_for_data, std::ref(*outstation), std::ref(state));
    listener_thread.detach();

    // Keep the program running
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
