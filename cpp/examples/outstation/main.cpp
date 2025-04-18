#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <boost/asio.hpp>

#include <opendnp3/outstation/Database.h>
#include <opendnp3/outstation/IOutstationApplication.h>
#include <opendnp3/outstation/IOutstation.h>
#include <opendnp3/outstation/OutstationConfig.h>
#include <opendnp3/outstation/OutstationStackConfig.h>
#include <opendnp3/outstation/UpdateBuilder.h>
#include <opendnp3/logging/ConsoleLogger.h>
#include <opendnp3/LogLevels.h>
#include <opendnp3/master/MasterStackConfig.h>
#include <opendnp3/outstation/DefaultOutstationApplication.h>
#include <opendnp3/outstation/DefaultOutstationCommandHandler.h>
#include <asiodnp3/DefaultListenCallbacks.h>
#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/OutstationStackConfig.h>
#include <asiodnp3/UpdateBuilder.h>

using namespace boost::asio;
using boost::asio::ip::tcp;
using namespace std;
using namespace opendnp3;
using namespace asiodnp3;

void startSensorServer(shared_ptr<IOutstation> outstation)
{
    thread([outstation]() {
        try
        {
            io_service ioService;
            tcp::acceptor acceptor(ioService, tcp::endpoint(tcp::v4(), 5000));

            while (true)
            {
                tcp::socket socket(ioService);
                acceptor.accept(socket);
                cout << "[INFO] Sensor connected." << endl;

                boost::asio::streambuf buffer;
                read_until(socket, buffer, "\n");

                istream is(&buffer);
                string dataLine;
                getline(is, dataLine);

                cout << "[DATA RECEIVED] " << dataLine << endl;

                // Parse the values: "temp,pressure,humidity"
                stringstream ss(dataLine);
                string tempStr, pressureStr, humidityStr;
                getline(ss, tempStr, ',');
                getline(ss, pressureStr, ',');
                getline(ss, humidityStr, ',');

                double temp = stod(tempStr);
                double press = stod(pressureStr);
                double humid = stod(humidityStr);

                cout << "[INFO] Sent to outstation: T=" << temp << ", P=" << press << ", H=" << humid << endl;

                // Build and apply the updates
                UpdateBuilder builder;
                builder.Update(Analog(temp, Flags(0x01), DNPTime::Now()), 0);  // AI index 0 = temperature
                builder.Update(Analog(press, Flags(0x01), DNPTime::Now()), 1); // AI index 1 = pressure
                builder.Update(Analog(humid, Flags(0x01), DNPTime::Now()), 2); // AI index 2 = humidity

                outstation->Apply(builder.Build());
            }
        }
        catch (std::exception& e)
        {
            cerr << "[ERROR] Sensor server exception: " << e.what() << endl;
        }
    }).detach();
}

int main()
{
    const uint32_t FILTERS = levels::NORMAL;

    DNP3Manager manager(1, ConsoleLogger::Create());

    // TCP server on 20000 for SCADA (ScadaBR)
    auto channel = manager.AddTCPServer(
        "server",
        FILTERS,
        ServerAcceptMode::CloseExisting,
        IPEndpoint("0.0.0.0", 20000),
        nullptr
    );

    // Outstation config
    OutstationStackConfig config;
    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);
    config.dbConfig.analog[0]; // Temperature
    config.dbConfig.analog[1]; // Pressure
    config.dbConfig.analog[2]; // Humidity
    config.link.LocalAddr = 10;
    config.link.RemoteAddr = 1;

    auto outstation = channel->AddOutstation(
        "outstation",
        DefaultOutstationCommandHandler::Create(),
        DefaultOutstationApplication::Create(),
        config
    );

    outstation->Enable();

    // Start sensor listener
    startSensorServer(outstation);

    cout << "[INFO] DNP3 Outstation running. Listening on TCP 20000 and TCP 5000 for sensor data." << endl;

    // Run forever
    this_thread::sleep_for(chrono::hours(24));
    return 0;
}

