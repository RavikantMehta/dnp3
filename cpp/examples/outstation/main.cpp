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
#include <vector>

using namespace std;
using namespace opendnp3;
using namespace openpal;
using namespace asiopal;
using namespace asiodnp3;
using boost::asio::ip::tcp;

shared_ptr<IOutstation> outstation_global;

void ConfigureDatabase(DatabaseConfig& config)
{
    // ✅ Class 0 for static polling, plus optional Class 1/2 for events
    config.analog[0].clazz = PointClass::Class0 | PointClass::Class1;
    config.analog[0].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[0].evariation = EventAnalogVariation::Group32Var7;

    config.analog[1] = config.analog[0]; // pressure
    config.analog[2] = config.analog[0]; // humidity
}

vector<string> split(const string& s, char delimiter)
{
    vector<string> tokens;
    string token;
    istringstream tokenStream(s);
    while (getline(tokenStream, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}

void start_tcp_sensor_listener()
{
    try
    {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 20001));

        cout << "[INFO] Listening for sensor data on port 20001..." << endl;

        while (true)
        {
            tcp::socket socket(io_context);
            acceptor.accept(socket);

            char data[1024] = {0};
            size_t length = socket.read_some(boost::asio::buffer(data));
            string received(data, length);

            cout << "[RECEIVED] Sensor data: " << received << endl;

            vector<string> values = split(received, ',');
            if (values.size() >= 3 && outstation_global)
            {
                UpdateBuilder builder;
                // ✅ Use "online" flag (0x01) for each analog point
                builder.Update(Analog(stod(values[0]), 0x01), 0); // temp
                builder.Update(Analog(stod(values[1]), 0x01), 1); // pressure
                builder.Update(Analog(stod(values[2]), 0x01), 2); // humidity
                outstation_global->Apply(builder.Build());

                cout << "[UPDATED] Analog values updated in DNP3 outstation." << endl;
            }

            socket.close();
        }
    }
    catch (exception& e)
    {
        cerr << "[ERROR] TCP Server: " << e.what() << endl;
    }
}

int main(int argc, char* argv[])
{
    // ✅ Use all logs for debugging DNP3 communication
    const uint32_t FILTERS = levels::ALL;
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
    config.outstation.params.allowUnsolicited = true; // Optional; ScadaBR polls instead
    config.link.LocalAddr = 10;  // Matches Slave Address in ScadaBR
    config.link.RemoteAddr = 1;  // Matches Source Address in ScadaBR
    config.link.KeepAliveTimeout = TimeDuration::Max();

    ConfigureDatabase(config.dbConfig);

    outstation_global = channel->AddOutstation(
        "outstation",
        SuccessCommandHandler::Create(),
        DefaultOutstationApplication::Create(),
        config
    );

    outstation_global->Enable();

    cout << "[INFO] DNP3 Outstation started. Listening on port 20000." << endl;

    // ✅ Start TCP listener to receive sensor values from Python script
    thread tcp_thread(start_tcp_sensor_listener);
    tcp_thread.join();

    return 0;
}
