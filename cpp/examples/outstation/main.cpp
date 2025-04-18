#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <sstream>
#include <boost/asio.hpp>

#include <openpal/logging/LogLevels.h>
#include <opendnp3/LogLevels.h>
#include <opendnp3/IPEndpoint.h>
#include <opendnp3/DatabaseConfig.h>
#include <opendnp3/MeasurementTypes.h>
#include <opendnp3/EventType.h>
#include <opendnp3/Flags.h>
#include <opendnp3/EventMode.h>

#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/DefaultOutstationApplication.h>
#include <asiodnp3/OutstationStackConfig.h>
#include <asiodnp3/IOutstation.h>
#include <asiodnp3/UpdateHandlers.h>

using boost::asio::ip::tcp;

const int TCP_PORT = 20001;

std::mutex db_mutex;
double latest_temp = 0.0;
double latest_pressure = 0.0;
double latest_humidity = 0.0;

// Thread function to receive TCP sensor data
void TCPReceiver()
{
    boost::asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), TCP_PORT));

    std::cout << "[TCP] Listening on port " << TCP_PORT << "...\n";

    while (true)
    {
        tcp::socket socket(io);
        acceptor.accept(socket);

        boost::asio::streambuf buf;
        boost::asio::read_until(socket, buf, '\n');

        std::istream input(&buf);
        std::string line;
        std::getline(input, line);

        double temp, pressure, humidity;
        char comma1, comma2;

        std::stringstream ss(line);
        if (ss >> temp >> comma1 >> pressure >> comma2 >> humidity)
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            latest_temp = temp;
            latest_pressure = pressure;
            latest_humidity = humidity;

            std::cout << "[RECEIVED] Temp: " << temp
                      << ", Pressure: " << pressure
                      << ", Humidity: " << humidity << "\n";
        }
        else
        {
            std::cerr << "[ERROR] Invalid data: " << line << "\n";
        }
    }
}

int main()
{
    const auto logLevels = openpal::LogLevels::NORMAL | openpal::LogLevels::ALL_APP_COMMS;
    asiodnp3::DNP3Manager manager(1);
    auto channel = manager.AddTCPServer("server", logLevels,
        opendnp3::ServerAcceptMode::CloseNew,
        { opendnp3::IPEndpoint("0.0.0.0", 20000) }, // SCADA listens here
        std::chrono::seconds(5)
    );

    opendnp3::DatabaseConfig db(10);
    db.analog[0].clazz = opendnp3::PointClass::Class1;
    db.analog[1].clazz = opendnp3::PointClass::Class1;
    db.analog[2].clazz = opendnp3::PointClass::Class1;

    asiodnp3::OutstationStackConfig config(db);
    config.outstation.params.allowUnsolicited = false;  // Only respond to poll
    config.outstation.params.eventBufferConfig = opendnp3::EventBufferConfig(10);

    auto outstation = channel->AddOutstation("outstation",
        asiodnp3::UpdateHandlers::Create(),
        asiodnp3::DefaultOutstationApplication::Create(),
        config);

    outstation->Enable();
    std::cout << "[DNP3] Outstation enabled\n";

    // Start TCP thread
    std::thread tcpThread(TCPReceiver);

    // Poll-safe loop to push data as Class 1 events
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        {
            std::lock_guard<std::mutex> lock(db_mutex);
            asiodnp3::UpdateBuilder builder;
            builder.Update(opendnp3::Analog(latest_temp, opendnp3::Flags(0x01)), 0);
            builder.Update(opendnp3::Analog(latest_pressure, opendnp3::Flags(0x01)), 1);
            builder.Update(opendnp3::Analog(latest_humidity, opendnp3::Flags(0x01)), 2);
            outstation->Apply(builder.Build());
        }
    }

    tcpThread.join();
    return 0;
}





