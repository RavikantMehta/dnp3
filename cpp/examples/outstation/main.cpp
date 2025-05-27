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
void ConfigureDatabase(DatabaseConfig& db)
{
    // 6 analog inputs (Class1, static+event variations)
    for (uint16_t i = 0; i < 6; ++i)
    {
        db.analog[i].clazz           = PointClass::Class1;
        db.analog[i].staticVariation = StaticAnalogVariation::Group30Var5;
        db.analog[i].eventVariation  = EventAnalogVariation::Group32Var7;
    }
    // 2 binary inputs (Class1, static+event variations)
    for (uint16_t i = 0; i < 2; ++i)
    {
        db.binary[i].clazz           = PointClass::Class1;
        db.binary[i].staticVariation = StaticBinaryVariation::Group1Var2;
        db.binary[i].eventVariation  = EventBinaryVariation::Group2Var2;
    }
}

// Listen on TCP port 20001 for both devices:
// payload: <deviceID>,<temp>,<press>,<humid>,<binary>
void ReceiveSensorData(shared_ptr<IOutstation> outstation)
{
    try {
        boost::asio::io_context io;
        tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 20001));
        cout << "[RTU2] Listening on sensor port 20001\n";

        while (true)
        {
            tcp::socket sock(io);
            acceptor.accept(sock);

            thread([sock = move(sock), outstation]() mutable {
                try {
                    char buf[1024];
                    while (true)
                    {
                        size_t n = sock.read_some(boost::asio::buffer(buf));
                        if (n == 0) break;
                        string data(buf, n);
                        istringstream iss(data);

                        string dev, ts, ps, hs, bs;
                        if (getline(iss, dev, ',') &&
                            getline(iss, ts,  ',') &&
                            getline(iss, ps,  ',') &&
                            getline(iss, hs,  ',') &&
                            getline(iss, bs,  ','))
                        {
                            int    deviceID = stoi(dev);
                            float  temp     = stof(ts);
                            float  press    = stof(ps);
                            float  humid    = stof(hs);
                            bool   binary   = (bs == "1");

                            int analogBase = (deviceID == 101) ? 0 : 3;
                            int binaryIdx  = (deviceID == 101) ? 0 : 1;

                            UpdateBuilder builder;
                            builder.Update(Analog(temp),    analogBase);
                            builder.Update(Analog(press),   analogBase + 1);
                            builder.Update(Analog(humid),   analogBase + 2);
                            builder.Update(Binary(binary), binaryIdx);

                            outstation->Apply(builder.Build());

                            cout << "[RTU2] Dev=" << deviceID
                                 << " T=" << temp
                                 << " P=" << press
                                 << " H=" << humid
                                 << " B=" << binary << "\n";
                        }
                        else
                        {
                            cerr << "[RTU2] Bad payload: " << data << "\n";
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
        cerr << "[RTU2] Listener error: " << e.what() << "\n";
    }
}

int main()
{
    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;
    DNP3Manager manager(1, ConsoleLogger::Create());

    // 1) DNP3 TCP server on port 20002
    auto channel = manager.AddTCPServer(
        "server",
        FILTERS,
        ChannelRetry::Default(),
        "0.0.0.0",
        20002,
        PrintingChannelListener::Create()
    );

    // 2) Outstation config: use AllTypes(10) to cover 6 analog + 2 binary
    OutstationStackConfig config(DatabaseSizes::AllTypes(10));
    config.outstation.eventBufferConfig       = EventBufferConfig::AllTypes(10);
    config.outstation.params.allowUnsolicited = false;    // Poll-only
    config.link.LocalAddr     = 11;   // RTU2 address
    config.link.RemoteAddr    = 2;    // ScadaBR master address
    config.link.KeepAliveTimeout = TimeDuration::Max();

    ConfigureDatabase(config.dbConfig);

    // 3) Add and enable outstation
    auto outstation = channel->AddOutstation(
        "outstation",
        SuccessCommandHandler::Create(),
        DefaultOutstationApplication::Create(),
        config
    );
    outstation->Enable();

    // 4) Start sensor listener for both device IDs
    ReceiveSensorData(outstation);

    // 5) Keep running
    this_thread::sleep_for(chrono::hours(24));
    return 0;
}



