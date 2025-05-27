#include <opendnp3/outstation/IOutstation.h>
#include <opendnp3/outstation/UpdateBuilder.h>
#include <opendnp3/outstation/DatabaseConfig.h>
#include <opendnp3/outstation/OutstationStackConfig.h>
#include <opendnp3/outstation/DefaultOutstationApplication.h>
#include <opendnp3/outstation/DefaultListenCallbacks.h>

#include <opendnp3/logging/ConsoleLogger.h>
#include <opendnp3/master/PrintingSOEHandler.h>
#include <opendnp3/channel/DefaultChannelListener.h>
#include <opendnp3/channel/TCPServer.h>

#include <asiopal/ASIOExecutor.h>
#include <asiopal/IO.h>
#include <asiopal/TCPServer.h>

#include <openpal/logging/LogLevels.h>
#include <openpal/util/ToHex.h>

#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <string>
#include <memory>

#include <boost/asio.hpp>

using namespace std;
using namespace opendnp3;
using namespace openpal;
using namespace asiopal;
using namespace boost::asio;

void start_sensor_listener(shared_ptr<IOutstation> outstation)
{
    thread([outstation]() {
        io_context io;
        ip::tcp::acceptor acceptor(io, ip::tcp::endpoint(ip::address::from_string("127.0.0.1"), 15000));

        function<void()> do_accept;
        do_accept = [&]() {
            acceptor.async_accept([&](boost::system::error_code ec, ip::tcp::socket socket) {
                if (!ec) {
                    thread([sock = move(socket), outstation]() mutable {
                        try {
                            while (true) {
                                char buffer[1024];
                                size_t len = sock.read_some(buffer::mutable_buffers_1(buffer, sizeof(buffer)));
                                if (len > 0) {
                                    string data(buffer, len);
                                    istringstream iss(data);
                                    int temp, pressure, humidity, binary, device_id;
                                    iss >> temp >> pressure >> humidity >> binary >> device_id;

                                    UpdateBuilder builder;
                                    if (device_id == 101) {
                                        builder.Update(Analog(temp, Flags(0x01)), 0);
                                        builder.Update(Analog(pressure, Flags(0x01)), 1);
                                        builder.Update(Analog(humidity, Flags(0x01)), 2);
                                        builder.Update(Binary(binary != 0, Flags(0x01)), 0);
                                    } else if (device_id == 102) {
                                        builder.Update(Analog(temp, Flags(0x01)), 3);
                                        builder.Update(Analog(pressure, Flags(0x01)), 4);
                                        builder.Update(Analog(humidity, Flags(0x01)), 5);
                                        builder.Update(Binary(binary != 0, Flags(0x01)), 1);
                                    } else {
                                        cerr << "Unknown device_id: " << device_id << endl;
                                    }

                                    outstation->Apply(builder.Build());
                                }
                            }
                        } catch (exception& e) {
                            cerr << "Sensor connection failed: " << e.what() << endl;
                        }
                    }).detach();
                }
                do_accept(); // Continue accepting
            });
        };

        do_accept();
        io.run();
    }).detach();
}

int main()
{
    const uint16_t port = 20000;
    const string listenAddr = "0.0.0.0";

    const auto FILTERS = levels::NORMAL;

    auto logger = ConsoleLogger::Create();
    auto executor = IO::Create();

    const uint16_t localAddr = 10;
    const uint16_t remoteAddr = 1;

    // Outstation config with 6 analogs, 2 binaries
    OutstationStackConfig config(DatabaseConfig::AllTypes(6, 2));
    config.link.LocalAddr = localAddr;
    config.link.RemoteAddr = remoteAddr;

    auto manager = DNP3Manager::Create();
    auto channel = manager->AddTCPServer(
        "tcpserver",
        FILTERS,
        ChannelRetry::Default(),
        listenAddr,
        port,
        PrintingChannelListener::Create()
    );

    auto outstation = channel->AddOutstation(
        "outstation",
        [&](IOutstation& outstation) {
            return make_shared<DefaultOutstationApplication>();
        },
        [&](IOutstation& outstation) {
            return make_shared<DefaultListenCallbacks>();
        },
        config
    );

    outstation->Enable();

    // Start sensor input listener
    start_sensor_listener(outstation);

    cout << "RTU 1 is running. Listening on port 20000 for SCADA, port 15000 for sensors." << endl;

    // Keep main thread alive
    this_thread::sleep_for(chrono::hours(24));
    return 0;
}
