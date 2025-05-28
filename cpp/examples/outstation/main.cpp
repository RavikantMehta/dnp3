#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <boost/asio.hpp>

#include <opendnp3/outstation/OutstationConfig.h>
#include <opendnp3/outstation/IOutstation.h>
#include <opendnp3/outstation/UpdateBuilder.h>
#include <opendnp3/outstation/DatabaseConfig.h>
#include <opendnp3/outstation/OutstationStackConfig.h>
#include <opendnp3/logging/ConsoleLogger.h>
#include <opendnp3/outstation/DefaultOutstationApplication.h>
#include <opendnp3/outstation/DefaultOutstationEventBufferConfig.h>
#include <opendnp3/outstation/OutstationContext.h>
#include <opendnp3/channel/ChannelRetry.h>
#include <asiodnp3/DefaultListenCallbacks.h>
#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/OutstationStackConfig.h>

using namespace opendnp3;
using namespace asiodnp3;
using boost::asio::ip::tcp;

void ConfigureDatabase(DatabaseConfig& config)
{
    // Device 101 (analog: 0,1,2; binary: 0)
    config.analog[0].clazz = PointClass::Class1;
    config.analog[1].clazz = PointClass::Class1;
    config.analog[2].clazz = PointClass::Class1;
    config.binary[0].clazz = PointClass::Class1;

    // Device 102 (analog: 3,4,5; binary: 1)
    config.analog[3].clazz = PointClass::Class1;
    config.analog[4].clazz = PointClass::Class1;
    config.analog[5].clazz = PointClass::Class1;
    config.binary[1].clazz = PointClass::Class1;
}

void ReceiveSensorData(std::shared_ptr<IOutstation> outstation)
{
    try {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 20001));
        std::cout << "[INFO] Listening for sensor data on port 20001...for RTU2" << std::endl;

        while (true)
        {
            tcp::socket socket(io_context);
            acceptor.accept(socket);
            std::cout << "[INFO] Sensor connected." << std::endl;

            char buffer[1024];
            size_t length = socket.read_some(boost::asio::buffer(buffer));
            buffer[length] = '\0';

            std::string data(buffer);
            std::cout << "[DATA RECEIVED] " << data << std::endl;

            std::istringstream iss(data);
            std::string devIdStr, tempStr, pressStr, humidStr, binaryStr;

            if (std::getline(iss, devIdStr, ',') &&
                std::getline(iss, tempStr, ',') &&
                std::getline(iss, pressStr, ',') &&
                std::getline(iss, humidStr, ',') &&
                std::getline(iss, binaryStr, ','))
            {
                int device_id = std::stoi(devIdStr);
                float temperature = std::stof(tempStr);
                float pressure = std::stof(pressStr);
                float humidity = std::stof(humidStr);
                bool binaryValue = (binaryStr == "1");

                int analogBaseIndex = (device_id == 101) ? 0 : 3;
                int binaryIndex = (device_id == 101) ? 0 : 1;

                UpdateBuilder builder;
                builder.Update(Analog(temperature), analogBaseIndex);
                builder.Update(Analog(pressure), analogBaseIndex + 1);
                builder.Update(Analog(humidity), analogBaseIndex + 2);
                builder.Update(Binary(binaryValue), binaryIndex);
                outstation->Apply(builder.Build());

                std::cout << "[INFO] Device " << device_id << ": T=" << temperature
                          << ", P=" << pressure << ", H=" << humidity
                          << ", Binary=" << binaryValue << std::endl;
            }
            else
            {
                std::cerr << "[ERROR] Invalid format: " << data << std::endl;
            }

            socket.close();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] Exception in ReceiveSensorData: " << e.what() << std::endl;
    }
}

int main()
{
    const uint32_t FILTERS = levels::NORMAL;
    DNP3Manager manager(1, ConsoleLogger::Create());

    auto channel = manager.AddTCPServer(
        "server",
        FILTERS,
        ServerAcceptMode::CloseExisting,
        IPEndpoint("0.0.0.0", 20002),
        ChannelRetry::Default()
    );

    DatabaseConfig dbConfig(6, 0, 2, 0); // 6 analogs, 2 binary
    ConfigureDatabase(dbConfig);

    OutstationConfig config(dbConfig);
    config.link.LocalAddr = 11;
    config.link.RemoteAddr = 2;
    config.eventBufferConfig = DefaultOutstationEventBufferConfig();

    auto outstation = channel->AddOutstation(
        "outstation",
        SuccessCommandHandler::Create(),
        DefaultOutstationApplication::Create(),
        config
    );

    outstation->Enable();

    std::thread sensorThread(ReceiveSensorData, outstation);
    sensorThread.join();

    return 0;
}
