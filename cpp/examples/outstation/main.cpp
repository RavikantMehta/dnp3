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

#include <nlohmann/json.hpp> // JSON library

using namespace std;
using namespace boost::asio::ip;
using namespace openpal;
using namespace asiopal;
using namespace opendnp3;
using namespace asiodnp3;
using json = nlohmann::json;

struct State {
    uint32_t count = 0;
    double value = 0;
    bool binary = false;
    DoubleBit dbit = DoubleBit::DETERMINED_OFF;
};

// configure database points
void ConfigureDatabase(DatabaseConfig& config)
{
    // Analog points
    config.analog[0].clazz = PointClass::Class1; // temperature
    config.analog[0].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[0].evariation = EventAnalogVariation::Group32Var7;

    config.analog[1].clazz = PointClass::Class1; // humidity
    config.analog[2].clazz = PointClass::Class1; // power_usage
    config.analog[3].clazz = PointClass::Class1; // energy_kwh

    // Binary points
    config.binary[0].clazz = PointClass::Class1; // door_open
    config.binary[1].clazz = PointClass::Class1; // smoke_detected
    config.binary[2].clazz = PointClass::Class1; // ups_status
}

// helper for manual updates from console
void AddUpdates(UpdateBuilder& builder, State& state, const std::string& arguments)
{
    for (const char& c : arguments)
    {
        switch (c)
        {
            case 'c':
                builder.Update(Counter(state.count), 0);
                ++state.count;
                break;
            case 'a':
                builder.Update(Analog(state.value), 0);
                state.value += 1;
                break;
            case 'b':
                builder.Update(Binary(state.binary), 0);
                state.binary = !state.binary;
                break;
            case 'd':
                builder.Update(DoubleBitBinary(state.dbit), 0);
                state.dbit = (state.dbit == DoubleBit::DETERMINED_OFF) ? DoubleBit::DETERMINED_ON : DoubleBit::DETERMINED_OFF;
                break;
            default:
                break;
        }
    }
}

// receive sensor data via TCP and apply to DNP3 outstation
void ReceiveSensorData(std::shared_ptr<IOutstation> outstation)
{
    try {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 15000));
        std::cout << "[INFO] Listening for sensor data on port 15000... for RTU#1" << std::endl;

        while (true)
        {
            tcp::socket socket(io_context);
            acceptor.accept(socket);
            std::cout << "[INFO] Sensor connected." << std::endl;

            char buffer[2048];
            size_t length = socket.read_some(boost::asio::buffer(buffer));
            buffer[length] = '\0';

            std::string data(buffer);
            std::cout << "[DATA RECEIVED] " << data << std::endl;

            try {
                json j = json::parse(data);

                // extract telemetry
                int rack_id = j.value("rack_id", 0);
                float temperature = j.value("temperature", 0.0);
                float humidity = j.value("humidity", 0.0);
                float power_usage = j.value("power_usage", 0.0);
                float energy = j.value("energy_kwh", 0.0);

                bool door = j.value("door_open", false);
                bool smoke = j.value("smoke_detected", false);
                bool ups = j.value("ups_status", false);

                std::string location = j.value("location", "");
                std::string city = j.value("city", "");
                std::string line_id = j.value("manufacturing_line_id", "");
                std::string line = j.value("manufacturing_line", "");
                std::string machine_id = j.value("machine_id", "");

                UpdateBuilder builder;
                builder.Update(Analog(temperature), 0);
                builder.Update(Analog(humidity), 1);
                builder.Update(Analog(power_usage), 2);
                builder.Update(Analog(energy), 3);
                builder.Update(Binary(door), 0);
                builder.Update(Binary(smoke), 1);
                builder.Update(Binary(ups), 2);

                outstation->Apply(builder.Build());

                std::cout << "[INFO] Sent to outstation: "
                          << "Rack=" << rack_id
                          << ", Temp=" << temperature
                          << ", Hum=" << humidity
                          << ", Power=" << power_usage
                          << ", Energy=" << energy
                          << ", Door=" << door
                          << ", Smoke=" << smoke
                          << ", UPS=" << ups
                          << ", Loc=" << location
                          << ", City=" << city
                          << ", LineID=" << line_id
                          << ", Line=" << line
                          << ", Machine=" << machine_id
                          << std::endl;

            } catch (std::exception& ex) {
                std::cerr << "[ERROR] JSON parse failed: " << ex.what() << std::endl;
            }

            socket.close();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] Exception in ReceiveSensorData: " << e.what() << std::endl;
    }
}

// manual user input thread (optional)
void HandleUserInput(std::shared_ptr<IOutstation> outstation)
{
    string input;
    State state;

    while (true)
    {
        std::cout << "Enter one or more measurement changes then press <enter>" << std::endl;
        std::cout << "c = counter, b = binary, d = doublebit, a = analog, 'quit' = exit" << std::endl;
        std::cin >> input;

        if (input == "quit")
            exit(0);

        UpdateBuilder builder;
        AddUpdates(builder, state, input);
        outstation->Apply(builder.Build());
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

    // 🔹 INITIAL NON-ZERO VALUES so master sees live data immediately
    {
        UpdateBuilder builder;
        builder.Update(Analog(25.5), 0);   // temperature
        builder.Update(Analog(60.2), 1);   // humidity
        builder.Update(Analog(120.0), 2);  // power_usage
        builder.Update(Analog(4500.0), 3); // energy_kwh
        builder.Update(Binary(true), 0);   // door_open
        builder.Update(Binary(false), 1);  // smoke_detected
        builder.Update(Binary(true), 2);   // ups_status
        outstation->Apply(builder.Build());
        std::cout << "[INFO] Initialized outstation with non-zero values." << std::endl;
    }

    std::thread sensorThread(ReceiveSensorData, outstation);
    std::thread inputThread(HandleUserInput, outstation);

    sensorThread.join();
    inputThread.join();

    return 0;
}
