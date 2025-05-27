// main.cpp -- RTU1 for two devices on the same sensor port

#include <openpal/logging/ConsoleLogger.h>
#include <asiopal/ASIOExecutor.h>
#include <asiopal/TCPServer.h>
#include <dnp3/outstation_stack.h>
#include <dnp3/log_levels.h>

#include <boost/asio.hpp>
#include <iostream>
#include <thread>
#include <sstream>

using namespace dnp3;
using boost::asio::ip::tcp;

// --------------------------------------------------------------------------
// Listens on TCP port 15000 for sensor lines:
//   "<temp> <pressure> <humidity> <binary> <device_id>\n"
// for BOTH devices, and updates the outstation accordingly.
// --------------------------------------------------------------------------
void startSensorListener(std::shared_ptr<OutstationStack> stack)
{
    std::thread([stack]() {
        asio::io_context io;
        tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 15000));
        std::cout << "[RTU1] Sensor listener on port 15000\n";

        while (true) {
            tcp::socket sock(io);
            acceptor.accept(sock);

            std::thread([sock = std::move(sock), stack]() mutable {
                try {
                    while (true) {
                        char buf[256];
                        size_t n = sock.read_some(asio::buffer(buf));
                        if (n == 0) break;
                        std::istringstream iss(std::string(buf, n));

                        double temp, pressure, humidity;
                        int binary, device;
                        if (!(iss >> temp >> pressure >> humidity >> binary >> device)) {
                            std::cerr << "[RTU1] Bad sensor format\n";
                            break;
                        }

                        // Build a new transaction
                        auto tx = stack->StartTx();

                        if (device == 101) {
                            tx->UpdateAnalog(0, temp);
                            tx->UpdateAnalog(1, pressure);
                            tx->UpdateAnalog(2, humidity);
                            tx->UpdateBinary(0, binary != 0);
                        }
                        else if (device == 102) {
                            tx->UpdateAnalog(3, temp);
                            tx->UpdateAnalog(4, pressure);
                            tx->UpdateAnalog(5, humidity);
                            tx->UpdateBinary(1, binary != 0);
                        }
                        else {
                            std::cerr << "[RTU1] Unknown device: " << device << "\n";
                        }

                        tx->End();  // apply
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "[RTU1] Sensor connection error: " << e.what() << "\n";
                }
            }).detach();
        }
    }).detach();
}

// --------------------------------------------------------------------------
// Entry point: configure DNP3 outstation and launch sensor listener
// --------------------------------------------------------------------------
int main()
{
    // 1) Prepare the logger & executor
    auto logger   = openpal::ConsoleLogger::Create();
    auto executor = asiopal::ASIOExecutor::Create();

    // 2) Build an outstation config for 6 analogs & 2 binaries
    auto cfg = DefaultOutstationConfig();
    cfg.link.LocalAddr = 10;   // RTU1 address
    cfg.link.RemoteAddr = 1;   // SCADA (master) address
    cfg.stack.num_analogs      = 6;
    cfg.stack.num_binaries     = 2;
    cfg.outstation.params.allowUnsolicited = false; // polling-only

    // 3) Create & enable the outstation stack, listening on port 20000
    auto stack = OutstationStack::Create(
        executor, logger, cfg,
        nullptr,           // no custom op handler
        IPEndpoint("0.0.0.0", 20000),
        LogLevels::NORMAL
    );
    stack->Enable();
    std::cout << "[RTU1] DNP3 outstation listening on port 20000\n";

    // 4) Start listening for sensor data (both devices) on port 15000
    startSensorListener(stack);

    // 5) Keep running
    std::this_thread::sleep_for(std::chrono::hours(24));
    return 0;
}
