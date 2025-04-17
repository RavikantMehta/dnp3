#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/PrintingSOEHandler.h>
#include <asiodnp3/PrintingChannelListener.h>
#include <asiodnp3/ConsoleLogger.h>
#include <asiodnp3/UpdateBuilder.h>

#include <asiopal/UTCTimeSource.h>
#include <opendnp3/outstation/SimpleCommandHandler.h>
#include <opendnp3/outstation/IUpdateHandler.h>
#include <opendnp3/LogLevels.h>

#include <boost/asio.hpp>

#include <string>
#include <thread>
#include <iostream>
#include <sstream>

using namespace std;
using namespace opendnp3;
using namespace asiopal;
using namespace asiodnp3;
using boost::asio::ip::tcp;

std::shared_ptr<IOutstation> global_outstation;

void ConfigureDatabase(DatabaseConfig& config)
{
    config.analog[0].clazz = PointClass::Class2;
    config.analog[0].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[0].evariation = EventAnalogVariation::Group32Var7;
    config.analog[1] = config.analog[0];
    config.analog[2] = config.analog[0];
}

void start_tcp_receiver()
{
    try
    {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 20001));  // Avoid 20000 conflict

        std::cout << "[TCP SERVER] Listening on port 20001..." << std::endl;

        while (true)
        {
            tcp::socket socket(io_context);
            acceptor.accept(socket);
            std::cout << "[TCP SERVER] Client connected!" << std::endl;

            char data[1024] = {0};
            size_t length = socket.read_some(boost::asio::buffer(data));
            std::string received(data, length);
            std::cout << "[TCP SERVER] Received: " << received << std::endl;

            std::istringstream ss(received);
            std::string token;
            std::vector<double> values;
            while (std::getline(ss, token, ','))
            {
                values.push_back(std::stod(token));
            }

            if (global_outstation && values.size() >= 3)
            {
                UpdateBuilder builder;
                builder.Update(Analog(values[0]), 0);  // Temperature
                builder.Update(Analog(values[1]), 1);  // Pressure
                builder.Update(Analog(values[2]), 2);  // Humidity

                global_outstation->Apply(builder.Build());
                std::cout << "[DNP3] Updated analog points!" << std::endl;
            }

            socket.close();
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "[ERROR] TCP Server exception: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[])
{
    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;
    DNP3Manager manager(1, ConsoleLogger::Create());

    // FIX: Replaced ChannelRetry::Default() with just 'nullptr' for channel listener
    auto channel = manager.AddTCPServer("server", FILTERS, nullptr, "0.0.0.0", 20000, PrintingChannelListener::Create());

    OutstationStackConfig config(DatabaseSizes::AllTypes(10));
    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);
    config.outstation.params.allowUnsolicited = true;
    config.link.LocalAddr = 10;
    config.link.RemoteAddr = 1;

    ConfigureDatabase(config.dbConfig);

    global_outstation = channel->AddOutstation(
        "outstation",
        SuccessCommandHandler::Create(),
        DefaultOutstationApplication::Create(),
        config
    );

    global_outstation->Enable();

    // Start TCP listener in a new thread
    std::thread tcp_thread(start_tcp_receiver);
    tcp_thread.join();

    return 0;
}

