#include <iostream>
#include <sstream>
#include <thread>
#include <string>

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
#include <asiodnp3/OutstationStackConfig.h>

using namespace std;
using boost::asio::ip::tcp;
using namespace openpal;
using namespace asiopal;
using namespace opendnp3;
using namespace asiodnp3;

// Configure 6 analogs (3 per device) and 2 binaries
void ConfigureDatabase(DatabaseConfig& config)
{
    for (int i = 0; i < 6; ++i)
    {
        config.analog[i].clazz       = PointClass::Class1;
        config.analog[i].svariation  = StaticAnalogVariation::Group30Var5;
        config.analog[i].evariation  = EventAnalogVariation::Group32Var7;
    }
    for (int i = 0; i < 2; ++i)
    {
        config.binary[i].clazz       = PointClass::Class1;
    }
}

// Listen on 20001 for BOTH devices sending:
//   <deviceID>,<temp>,<press>,<humid>,<binary>
void ReceiveSensorData(std::shared_ptr<IOutstation> outstation)
{
    try {
        asio::io_context io;
        tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 20001));
        cout << "[RTU2] Listening sensor port 20001 for both devices\n";

        while (true)
        {
            tcp::socket sock(io);
            acceptor.accept(sock);

            // Handle each connection in its own thread
            thread([sock = std::move(sock), outstation]() mutable {
                try {
                    char buf[1024];
                    while (true)
                    {
                        size_t n = sock.read_some(boost::asio::buffer(buf));
                        if (n == 0) break;
                        string data(buf, n);
                        std::istringstream iss(data);

                        string devStr, tStr, pStr, hStr, bStr;
                        if (getline(iss, devStr, ',') &&
                            getline(iss, tStr,   ',') &&
                            getline(iss, pStr,   ',') &&
                            getline(iss, hStr,   ',') &&
                            getline(iss, bStr,   ','))
                        {
                            int    deviceID    = stoi(devStr);
                            float  temp        = stof(tStr);
                            float  press       = stof(pStr);
                            float  humid       = stof(hStr);
                            bool   binState    = (bStr == "1");

                            // Determine point indexes
                            int analogBase = (deviceID == 101 ? 0 : 3);
                            int binaryIdx  = (deviceID == 101 ? 0 : 1);

                            UpdateBuilder builder;
                            builder.Update(Analog(temp), analogBase);
                            builder.Update(Analog(press), analogBase + 1);
                            builder.Update(Analog(humid), analogBase + 2);
                            builder.Update(Binary(binState), binaryIdx);

                            outstation->Apply(builder.Build());

                            cout << "[RTU2] Dev=" << deviceID
                                 << " T=" << temp
                                 << " P=" << press
                                 << " H=" << humid
                                 << " B=" << binState << "\n";
                        }
                        else
                        {
                            cerr << "[RTU2] Invalid payload: " << data << "\n";
                        }
                    }
                }
                catch (const exception& e) {
                    cerr << "[RTU2] Sensor thread error: " << e.what() << "\n";
                }
            }).detach();
        }
    }
    catch (const exception& e) {
        cerr << "[RTU2] ReceiveSensorData error: " << e.what() << "\n";
    }
}

int main()
{
    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;
    DNP3Manager manager(1, ConsoleLogger::Create());

    // 1) DNP3 TCP server for ScadaBR polls on port 20002
    auto channel = manager.AddTCPServer(
        "server",
        FILTERS,
        ChannelRetry::Default(),
        "0.0.0.0",
        20002,
        PrintingChannelListener::Create()
    );

    // 2) Outstation config: 6 analogs, 2 binaries
    OutstationStackConfig config(DatabaseSizes(6, 0, 2, 0));
    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);
    config.outstation.params.allowUnsolicited = true;
    config.link.LocalAddr = 11;   // RTU2 address
    config.link.RemoteAddr = 2;   // ScadaBR (master) address
    config.link.KeepAliveTimeout = TimeDuration::Max();

    ConfigureDatabase(config.dbConfig);

    auto outstation = channel->AddOutstation(
        "outstation",
        SuccessCommandHandler::Create(),
        DefaultOutstationApplication::Create(),
        config
    );
    outstation->Enable();

    // 3) Start sensor listener for both devices
    ReceiveSensorData(outstation);

    // 4) Keep running
    std::this_thread::sleep_for(std::chrono::hours(24));
    return 0;
}

