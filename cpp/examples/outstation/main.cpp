#include <iostream>
#include <thread>
#include <mutex>
#include <boost/asio.hpp>

#include <openpal/logging/LogLevels.h>
#include <opendnp3/outstation/OutstationConfig.h>
#include <opendnp3/outstation/DatabaseConfigView.h>
#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/DefaultOutstationApplication.h>
#include <asiodnp3/UpdateHandlers.h>

using boost::asio::ip::tcp;

// Globals for sensor data
std::mutex data_mutex;
double latest_temp = 0.0;
double latest_pressure = 0.0;
double latest_humidity = 0.0;

// Constants
const int SENSOR_PORT = 20001;
const int DNP3_PORT = 20000;

// TCP server to receive sensor data
void StartSensorServer()
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

        double t, p, h;
        char c1, c2;
        std::stringstream ss(line);
        if (ss >> t >> c1 >> p >> c2 >> h && c1 == ',' && c2 == ',')
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            latest_temp = t;
            latest_pressure = p;
            latest_humidity = h;
            std::cout << "[RECEIVED] temp=" << t << ", pressure=" << p << ", humidity=" << h << "\n";
        }
        else
        {
            std::cerr << "[ERROR] Failed to parse: " << line << "\n";
        }
    }
}

// Custom command handler for responding to SCADA
class MyOutstationApplication : public asiodnp3::DefaultOutstationApplication
{
public:
    bool SupportsWriteAbsoluteTime() const override { return true; }

    void ProcessRequest(const opendnp3::TimeAndInterval& value, uint16_t index, opendnp3::IUpdateHandler& handler) override
    {
        // Not used
    }

    void OnReadComplete(opendnp3::IUpdateHandler& handler) override
    {
        std::lock_guard<std::mutex> lock(data_mutex);
        opendnp3::Analog a1(latest_temp, opendnp3::Flags(0x01));
        opendnp3::Analog a2(latest_pressure, opendnp3::Flags(0x01));
        opendnp3::Analog a3(latest_humidity, opendnp3::Flags(0x01));

        handler.Update(a1, 0);
        handler.Update(a2, 1);
        handler.Update(a3, 2);

        std::cout << "[RESPOND] Polled data: T=" << latest_temp << ", P=" << latest_pressure << ", H=" << latest_humidity << "\n";
    }
};

int main()
{
    // Start sensor listener thread
    std::thread sensor_thread(StartSensorServer);

    // Logging
    const auto levels = openpal::LogLevels::NORMAL | openpal::LogLevels::ALL_APP_COMMS;
    asiodnp3::DNP3Manager manager(1);
    auto log = manager.GetLogger();
    log.SetLevels(levels);

    // Setup TCP server for SCADA (ScadaBR connects to this)
    auto channel = manager.AddTCPServer(
        "outstation",
        levels,
        opendnp3::ServerAcceptMode::CloseNew,
        {opendnp3::IPEndpoint("0.0.0.0", DNP3_PORT)},
        std::chrono::seconds(5)
    );

    // Configure outstation DB
    opendnp3::DatabaseConfig dbConfig(10); // 10 analogs
    dbConfig.analogs[0].clazz = opendnp3::PointClass::Class1;
    dbConfig.analogs[1].clazz = opendnp3::PointClass::Class1;
    dbConfig.analogs[2].clazz = opendnp3::PointClass::Class1;

    asiodnp3::OutstationStackConfig stackConfig(dbConfig);
    stackConfig.outstation.params.allowUnsolicited = false;
    stackConfig.outstation.params.eventBufferConfig = opendnp3::EventBufferConfig(10);

    // Create outstation
    auto outstation = channel->AddOutstation(
        "outstation",
        asiodnp3::UpdateHandlers::Create(),
        std::make_shared<MyOutstationApplication>(),
        stackConfig
    );

    outstation->Enable();
    std::cout << "[INFO] DNP3 Outstation enabled on port " << DNP3_PORT << "\n";

    sensor_thread.join();
    return 0;
}






