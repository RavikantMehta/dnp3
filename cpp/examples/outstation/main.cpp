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
    for (int i = 0; i < 6; ++i)
    {
        config.analog[i].clazz = PointClass::Class1;
        config.analog[i].svariation = StaticAnalogVariation::Group30Var5;
        config.analog[i].evariation = EventAnalogVariation::Group32Var7;
    }

    for (int i = 0; i < 2; ++i)
    {
        config.binary[i].clazz = PointClass::Class1;
    }
}

void ReceiveSensorData(std::shared_ptr<IOutstation> outstation)
{
    try {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 20001));
        std::cout << "[INFO] Listening for sensor data on port 20001 (RTU)" << std::endl;

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
            std::string deviceStr, tempStr, pressStr, humidStr, binaryStr;

            if (std::getline(iss, deviceStr, ',') &&
                std::getline(iss, tempStr, ',') &&
                std::getline(iss, pressStr, ',') &&
                std::getline(iss, humidStr, ',') &&
                std::getline(iss, binaryStr, ','))
            {
                int device_id = std::stoi(deviceStr);
                float temperature = std::stof(tempStr);
                float pressure = std::stof(pressStr);
                float humidity = std::stof(humidStr);
                bool binaryValue = (binaryStr == "1");

                int analogBaseIndex, binaryIndex;

                if (device_id == 103) {
                    analogBaseIndex = 0;
                    binaryIndex = 0;
                } else if (device_id == 104) {
                    analogBaseIndex = 3;
                    binaryIndex = 1;
                } else {
                    std::cerr << "[ERROR] Unknown device ID: " << device_id << std::endl;
                    continue;
                }

                UpdateBuilder builder;
                builder.Update(Analog(temperature), analogBaseIndex);
                builder.Update(Analog(pressure), analogBaseIndex + 1);
                builder.Update(Analog(humidity), analogBaseIndex + 2);
                builder.Update(Binary(binaryValue), binaryIndex);
                outstation->Apply(builder.Build());

                std::cout << "[INFO] Device " << device_id << " | Sent to outstation: T=" << temperature
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
        20002,
        PrintingChannelListener::Create()
    );

    OutstationStackConfig config(DatabaseSizes::AllTypes(10));
    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);
    config.outstation.params.allowUnsolicited = true;
    config.link.LocalAddr = 11;
    config.link.RemoteAddr = 2;
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
