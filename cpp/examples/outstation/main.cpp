#include <iostream>
#include <thread>
#include <mutex>
#include <boost/asio.hpp>
#include <openpal/logging/LogLevels.h>
#include <opendnp3/outstation/IOutstation.h>
#include <opendnp3/outstation/OutstationStackConfig.h>
#include <opendnp3/outstation/OutstationConfig.h>
#include <opendnp3/outstation/IOutstationApplication.h>
#include <opendnp3/outstation/DatabaseConfig.h>
#include <opendnp3/LogLevels.h>
#include <opendnp3/AnalogOutput.h>
#include <opendnp3/Flags.h>
#include <opendnp3/EventMode.h>
#include <asiodnp3/DefaultOutstationApplication.h>
#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/OutstationStackConfig.h>
#include <asiodnp3/UpdateHandlers.h>

using boost::asio::ip::tcp;

const int SENSOR_PORT = 20001;
const int SCADA_PORT = 20000;

std::mutex update_mutex;

void UpdateAnalog(asiodnp3::IOutstation* outstation, double value, uint16_t index)
{
    std::lock_guard<std::mutex> lock(update_mutex);
    opendnp3::Analog analog(value, opendnp3::Flags(0x01)); // ONLINE
    outstation->Update(analog, index, opendnp3::EventMode::Detect);
    std::cout << "[OUTSTATION] Analog[" << index << "] = " << value << std::endl;
}

void StartSensorTCP(asiodnp3::IOutstation* outstation)
{
    boost::asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), SENSOR_PORT));
    std::cout << "[TCP] Listening for sensor data on port " << SENSOR_PORT << "...\n";

    while (true)
    {
        tcp::socket socket(io);
        acceptor.accept(socket);

        boost::asio::streambuf buffer;
        boost::asio::read_until(socket, buffer, '\n');

        std::istream input(&buffer);
        std::string line;
        std::getline(input, line);

        std::stringstream ss(line);
        double temp, pressure, humidity;
        char comma;

        if (ss >> temp >> comma >> pressure >> comma >> humidity)
        {
            std::cout << "[RECEIVED] " << temp << ", " << pressure << ", " << humidity << std::endl;
            UpdateAnalog(outstation, temp, 0);
            UpdateAnalog(outstation, pressure, 1);
            UpdateAnalog(outstation, humidity, 2);
        }
        else
        {
            std::cerr << "[ERROR] Could not parse: " << line << std::endl;
        }
    }
}

int main()
{
    const auto logLevels = openpal::LogLevels::NORMAL | openpal::LogLevels::ALL_APP_COMMS;

    asiodnp3::DNP3Manager manager(1);
    auto log = manager.GetLogger();
    log.SetLevels(logLevels);

    auto channel = manager.AddTCPServer(
        "dnp3_server",
        logLevels,
        opendnp3::ServerAcceptMode::CloseNew,
        { opendnp3::IPEndpoint("0.0.0.0", SCADA_PORT) },
        std::chrono::seconds(5)
    );

    opendnp3::DatabaseConfig dbConfig(10);
    dbConfig.analog[0].clazz = opendnp3::PointClass::Class1;
    dbConfig.analog[1].clazz = opendnp3::PointClass::Class1;
    dbConfig.analog[2].clazz = opendnp3::PointClass::Class1;

    asiodnp3::OutstationStackConfig stackConfig(dbConfig);
    stackConfig.outstation.params.allowUnsolicited = false;
    stackConfig.outstation.params.eventBufferConfig = opendnp3::EventBufferConfig(10);

    auto outstation = channel->AddOutstation(
        "outstation",
        asiodnp3::UpdateHandlers::Create(),
        asiodnp3::DefaultOutstationApplication::Create(),
        stackConfig
    );

    outstation->Enable();
    std::cout << "[INFO] DNP3 Outstation running on port " << SCADA_PORT << "\n";

    std::thread sensorThread(StartSensorTCP, outstation.get());
    sensorThread.join();

    return 0;
}




