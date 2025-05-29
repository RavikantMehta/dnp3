#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <boost/asio.hpp>
#include <openpal/logging/LogLevels.h>
#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/DefaultListenCallbacks.h>
#include <asiodnp3/UpdateBuilder.h>
#include <opendnp3/outstation/DatabaseConfig.h>
#include <opendnp3/outstation/IOutstation.h>
#include <opendnp3/outstation/OutstationConfig.h>
#include <opendnp3/outstation/OutstationStackConfig.h>
#include <opendnp3/app/MeasurementTypes.h>

using namespace boost::asio;
using namespace std;
using namespace opendnp3;
using namespace asiodnp3;

std::mutex dataMutex;
float temperature = 0.0, pressure = 0.0, humidity = 0.0;
bool binaryValue = false;

void receiveSensorData() {
    try {
        io_context io_context;
        ip::tcp::acceptor acceptor(io_context, ip::tcp::endpoint(ip::tcp::v4(), 15000));

        while (true) {
            ip::tcp::socket socket(io_context);
            acceptor.accept(socket);

            boost::asio::streambuf buffer;
            boost::asio::read_until(socket, buffer, "\n");

            istream input(&buffer);
            float temp, press, hum;
            int bin;
            input >> temp >> press >> hum >> bin;

            {
                std::lock_guard<std::mutex> lock(dataMutex);
                temperature = temp;
                pressure = press;
                humidity = hum;
                binaryValue = static_cast<bool>(bin);
            }

            socket.close();
        }
    } catch (std::exception& e) {
        cerr << "TCP Receive Error: " << e.what() << endl;
    }
}

void updateOutstation(std::shared_ptr<IOutstation> outstation) {
    float lastTemp = -9999, lastPress = -9999, lastHum = -9999;
    bool lastBinary = false;

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        float temp, press, hum;
        bool bin;

        {
            std::lock_guard<std::mutex> lock(dataMutex);
            temp = temperature;
            press = pressure;
            hum = humidity;
            bin = binaryValue;
        }

        UpdateBuilder builder;
        bool changed = false;

        if (temp != lastTemp) {
            builder.Update(Analog(temp, Flags(0x01)), 0); // Temperature
            lastTemp = temp;
            changed = true;
        }

        if (press != lastPress) {
            builder.Update(Analog(press, Flags(0x01)), 1); // Pressure
            lastPress = press;
            changed = true;
        }

        if (hum != lastHum) {
            builder.Update(Analog(hum, Flags(0x01)), 2); // Humidity
            lastHum = hum;
            changed = true;
        }

        if (bin != lastBinary) {
            builder.Update(Binary(bin, Flags(0x01)), 0); // Binary
            lastBinary = bin;
            changed = true;
        }

        if (changed) {
            outstation->Apply(builder.Build());
        }
    }
}

int main() {
    // Create DNP3 Manager
    DNP3Manager manager(1, [](const LogMessage& msg) {
        std::cout << msg.loggerid << " - " << msg.message << std::endl;
    });

    // Configure outstation
    auto channel = manager.AddTCPServer(
        "server",
        LogLevels::NORMAL,
        ServerAcceptMode::CloseNew,
        IPEndpoint("0.0.0.0", 20000),
        nullptr
    );

    // Database config: 3 analogs, 1 binary
    DatabaseConfig db;
    db.analogs.resize(3);
    db.binaries.resize(1);

    OutstationStackConfig config(db);
    config.outstation.params.allowUnsolicited = false;
    config.link.LocalAddr = 10;  // RTU2 address
    config.link.RemoteAddr = 1;  // SCADA address

    auto outstation = channel->AddOutstation(
        "outstation",
        [](IOutstation& outstation) {},
        config
    );

    outstation->Enable();

    std::thread tcpThread(receiveSensorData);
    std::thread updateThread(updateOutstation, outstation);

    tcpThread.join();
    updateThread.join();

    return 0;
}
