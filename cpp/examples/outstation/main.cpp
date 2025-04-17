
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
using namespace openpal;
using namespace asiopal;
using namespace asiodnp3;
using boost::asio::ip::tcp;

// -- Shared State --
struct State {
    double temperature = 0.0;
    double pressure = 0.0;
    double humidity = 0.0;
};

// -- Update Function --
void UpdateFromSensor(const std::string& data, State& state, std::shared_ptr<IOutstation> outstation)
{
    std::stringstream ss(data);
    std::string token;
    std::getline(ss, token, ','); state.temperature = std::stod(token);
    std::getline(ss, token, ','); state.pressure = std::stod(token);
    std::getline(ss, token, ','); state.humidity = std::stod(token);

    UpdateBuilder builder;
    builder.Update(Analog(state.temperature), 0); // index 0 = temp
    builder.Update(Analog(state.pressure), 1);    // index 1 = pressure
    builder.Update(Analog(state.humidity), 2);    // index 2 = humidity

    outstation->Apply(builder.Build());

    std::cout << "[DNP3] Updated: Temp=" << state.temperature 
              << " | Pressure=" << state.pressure 
              << " | Humidity=" << state.humidity << std::endl;
}

// -- Boost TCP Receiver Thread --
void TCPReceiver(std::shared_ptr<IOutstation> outstation, State& state)
{
    try {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 20000));
        std::cout << "[TCP] Listening on port 20000..." << std::endl;

        while (true) {
            tcp::socket socket(io_context);
            acceptor.accept(socket);

            char data[1024] = {0};
            size_t len = socket.read_some(boost::asio::buffer(data));
            std::string received(data, len);
            std::cout << "[TCP] Received: " << received << std::endl;

            UpdateFromSensor(received, state, outstation);
            socket.close();
        }
    } catch (std::exception& e) {
        std::cerr << "[ERROR] TCP Receiver Exception: " << e.what() << std::endl;
    }
}

void ConfigureDatabase(DatabaseConfig& config)
{
    for (uint16_t i = 0; i < 3; ++i) {
        config.analog[i].clazz = PointClass::Class1;
        config.analog[i].svariation = StaticAnalogVariation::Group30Var5;
        config.analog[i].evariation = EventAnalogVariation::Group32Var7;
    }
}

int main(int argc, char* argv[])
{
    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;
    DNP3Manager manager(1, ConsoleLogger::Create());

    auto channel = manager.AddTCPServer("server", FILTERS, IChannelListener::Default(), "0.0.0.0", 20001, PrintingChannelListener::Create());

    OutstationStackConfig config(DatabaseSizes::AllTypes(10));
    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);
    config.outstation.params.allowUnsolicited = true;
    config.link.LocalAddr = 10;
    config.link.RemoteAddr = 1;

    ConfigureDatabase(config.dbConfig);

    auto outstation = channel->AddOutstation("outstation", SuccessCommandHandler::Create(), DefaultOutstationApplication::Create(), config);
    outstation->Enable();

    State state;
    std::thread tcp_thread(TCPReceiver, outstation, std::ref(state));
    tcp_thread.join();

    return 0;
}
