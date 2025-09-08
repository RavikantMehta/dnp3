#include <nlohmann/json.hpp> // JSON library
using json = nlohmann::json;

void ConfigureDatabase(DatabaseConfig& config)
{
    // Define analog points
    config.analog[0].clazz = PointClass::Class1; // temperature
    config.analog[1].clazz = PointClass::Class1; // humidity
    config.analog[2].clazz = PointClass::Class1; // power_usage
    config.analog[3].clazz = PointClass::Class1; // energy_kwh

    // Define binary points
    config.binary[0].clazz = PointClass::Class1; // door_open
    config.binary[1].clazz = PointClass::Class1; // smoke_detected
    config.binary[2].clazz = PointClass::Class1; // ups_status
}

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

                float temperature = j.value("temperature_c", 0.0);
                float humidity = j.value("humidity_pct", 0.0);
                float power_usage = j.value("power_usage_w", 0.0);
                float energy = j.value("energy_kwh", 0.0);
                bool door = j.value("door_open", false);
                bool smoke = j.value("smoke_detected", false);
                bool ups = j.value("ups_status", false);

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
                          << "Temp=" << temperature
                          << ", Hum=" << humidity
                          << ", Power=" << power_usage
                          << ", Energy=" << energy
                          << ", Door=" << door
                          << ", Smoke=" << smoke
                          << ", UPS=" << ups
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
