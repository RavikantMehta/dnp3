#include <iostream>
#include <sstream>
#include <thread>
#include <string>
#include <mutex>

#include <boost/asio.hpp>

#include <openpal/logging/LogLevels.h>
#include <asiopal/UTCTimeSource.h>

#include <opendnp3/LogLevels.h>
#include <opendnp3/outstation/IUpdateHandler.h>
#include <opendnp3/outstation/SimpleCommandHandler.h>

#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/ConsoleLogger.h>
#include <asiodnp3/PrintingChannelListener.h>
#include <asiodnp3/UpdateBuilder.h>

using namespace std;
using namespace boost::asio::ip;
using namespace openpal;
using namespace asiopal;
using namespace opendnp3;
using namespace asiodnp3;

void ConfigureDatabase(DatabaseConfig& config)
{
    // Temperature - Analog[0]
    config.analog[0].clazz = PointClass::Class1;
    config.analog[0].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[0].evariation = EventAnalogVariation::Group32Var7;

    // Pressure - Analog[1]
    config.analog[1].clazz = PointClass::Class1;
    config.analog[1].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[1].evariation = EventAnalogVariation::Group32Var7;

    // Humidity - Analog[2]
    config.analog[2].clazz = PointClass::Class1;
    config.analog[2].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[2].evariation = EventAnalogVariation::Group32Var7;

    // Binary input - Binary[0]
    config.binary[0].clazz = PointClass::Class1;
}

void ReceiveSensorData(std::shared_ptr<IOutstation> outstation)
{
    try {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 20001));
        std::cout << "[INFO] Listening for sensor data on port 20001..." << std::endl;

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
            std::string tempStr, pressStr, humidStr, binaryStr;

            if (std::getline(iss, tempStr, ',') &&
                std::getline(iss, pressStr, ',') &&
                std::getline(iss, humidStr, ',') &&
                std::getline(iss, binaryStr, ','))
            {
                float temperature = std::stof(tempStr);
                float pressure = std::stof(pressStr);
                float humidity = std::stof(humidStr);
                bool binaryValue = (binaryStr == "1");

                UpdateBuilder builder;
                builder.Update(Analog(temperature), 0);
                builder.Update(Analog(pressure), 1);
                builder.Update(Analog(humidity), 2);
                builder.Update(Binary(binaryValue), 0);
                outstation->Apply(builder.Build());

                std::cout << "[INFO] Sent to outstation: T=" << temperature
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

int main(int argc, char* argv[])
{
    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;
    DNP3Manager manager(1, ConsoleLogger::Create());

    auto channel = manager.AddTCPServer(
        "server",
        FILTERS,
        ChannelRetry::Default(),
        "0.0.0.0",
        20000,
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

    std::thread sensorThread(ReceiveSensorData, outstation);
    sensorThread.join();

    return 0;
}
