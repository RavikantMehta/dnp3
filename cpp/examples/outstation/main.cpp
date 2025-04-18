#include <iostream>
#include <thread>
#include <boost/asio.hpp>
#include <openpal/logging/LogLevels.h>
#include <opendnp3/outstation/DatabaseConfig.h>
#include <opendnp3/outstation/UpdateBuilder.h>
#include <opendnp3/outstation/OutstationStackConfig.h>
#include <opendnp3/outstation/IOutstation.h>
#include <opendnp3/outstation/OutstationConfig.h>
#include <opendnp3/outstation/IOutstationApplication.h>
#include <asiodnp3/DefaultOutstationApplication.h>
#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/OutstationStackConfig.h>
#include <asiodnp3/UpdateHandlers.h>

using boost::asio::ip::tcp;

// Constants
const int PORT = 20000;  // Port for TCP server

// Helper function to update analogs
void UpdateAnalog(asiodnp3::IOutstation* outstation, double value, uint16_t index)
{
    opendnp3::Analog analog(value, opendnp3::Flags(0x01)); // ONLINE flag
    outstation->Update(analog, index, opendnp3::EventMode::Force);
    std::cout << "[OUTSTATION] Updated Analog[" << index << "] = " << value << std::endl;
}

// TCP server to receive sensor data
void StartTCPServer(asiodnp3::IOutstation* outstation)
{
    boost::asio::io_context io_context;
    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), PORT));

    std::cout << "[TCP] Listening on port " << PORT << "...\n";

    while (true)
    {
        tcp::socket socket(io_context);
        acceptor.accept(socket);

        boost::asio::streambuf buffer;
        boost::asio::read_until(socket, buffer, '\n');

        std::istream input(&buffer);
        std::string line;
        std::getline(input, line);

        std::stringstream ss(line);
        double temp, pressure, vibration;
        char comma;

        if (ss >> temp >> comma >> pressure >> comma >> vibration)
        {
            std::cout << "[RECEIVED] Sensor data: " << temp << ", " << pressure << ", " << vibration << "\n";
            UpdateAnalog(outstation, temp, 0);
            UpdateAnalog(outstation, pressure, 1);
            UpdateAnalog(outstation, vibration, 2);
        }
        else
        {
            std::cerr << "[ERROR] Failed to parse line: " << line << "\n";
        }
    }
}

int main()
{
    // Logger levels
    const auto levels = openpal::LogLevels::NORMAL | openpal::LogLevels::ALL_APP_COMMS;

    // DNP3 Manager with 1 thread
    asiodnp3::DNP3Manager manager(1);
    auto log = manager.GetLogger();
    log.SetLevels(levels);

    // Create channel
    auto channel = manager.AddTCPServer(
        "server",
        levels,
        opendnp3::ServerAcceptMode::CloseNew,
        { opendnp3::IPEndpoint("0.0.0.0", 20001) },
        std::chrono::seconds(5)
    );

    // Database configuration with 10 analogs
    opendnp3::DatabaseConfig dbConfig(10);

    // Assign Class 1 so SCADA can poll these points
    dbConfig.analog[0].clazz = opendnp3::PointClass::Class1;
    dbConfig.analog[1].clazz = opendnp3::PointClass::Class1;
    dbConfig.analog[2].clazz = opendnp3::PointClass::Class1;

    asiodnp3::OutstationStackConfig stackConfig(dbConfig);
    stackConfig.outstation.params.allowUnsolicited = true;
    stackConfig.outstation.params.eventBufferConfig = opendnp3::EventBufferConfig(10);

    // Create outstation
    auto outstation = channel->AddOutstation(
        "outstation",
        asiodnp3::UpdateHandlers::Create(),
        asiodnp3::DefaultOutstationApplication::Create(),
        stackConfig
    );

    outstation->Enable();
    std::cout << "[INFO] DNP3 Outstation Enabled.\n";

    // Start TCP listener in another thread
    std::thread serverThread(StartTCPServer, outstation.get());
    serverThread.join();

    return 0;
}


