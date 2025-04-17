#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/PrintingSOEHandler.h>
#include <asiodnp3/PrintingChannelListener.h>
#include <asiodnp3/ConsoleLogger.h>
#include <asiodnp3/UpdateBuilder.h>

#include <asiopal/UTCTimeSource.h>
#include <opendnp3/outstation/SimpleCommandHandler.h>

#include <opendnp3/outstation/IUpdateHandler.h>

#include <opendnp3/LogLevels.h>

#include <string>
#include <thread>
#include <iostream>

using namespace std;
using namespace opendnp3;
using namespace openpal;
using namespace asiopal;
using namespace asiodnp3;

void ConfigureDatabase(DatabaseConfig& config)
{
    // example of configuring analog index 0 for Class2 with floating point variations by default
    config.analog[0].clazz = PointClass::Class2;
    config.analog[0].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[0].evariation = EventAnalogVariation::Group32Var7;
}

struct State
{
    uint32_t count = 0;
    double value = 0;
    bool binary = false;
    DoubleBit dbit = DoubleBit::DETERMINED_OFF;
};

void AddUpdates(UpdateBuilder& builder, State& state, const std::string& arguments)
{
    // Print the state before updates
    std::cout << "Before Updates: " << std::endl;
    std::cout << "Counter: " << state.count << ", Analog: " << state.value 
              << ", Binary: " << (state.binary ? "True" : "False") 
              << ", DoubleBit: " << (state.dbit == DoubleBit::DETERMINED_ON ? "ON" : "OFF") << std::endl;

    for (const char& c : arguments)
    {
        switch (c)
        {
        case('c'):
            {
                builder.Update(Counter(state.count), 0);
                ++state.count;
                break;
            }
        case('a'):
            {
                builder.Update(Analog(state.value), 0);
                state.value += 1;
                break;
            }
        case('b'):
            {
                builder.Update(Binary(state.binary), 0);
                state.binary = !state.binary;
                break;
            }
        case('d'):
            {
                builder.Update(DoubleBitBinary(state.dbit), 0);
                state.dbit = (state.dbit == DoubleBit::DETERMINED_OFF) ? DoubleBit::DETERMINED_ON : DoubleBit::DETERMINED_OFF;
                break;
            }
        default:
            break;
        }
    }

    // Print the state after updates
    std::cout << "After Updates: " << std::endl;
    std::cout << "Counter: " << state.count << ", Analog: " << state.value 
              << ", Binary: " << (state.binary ? "True" : "False") 
              << ", DoubleBit: " << (state.dbit == DoubleBit::DETERMINED_ON ? "ON" : "OFF") << std::endl;
}

int main(int argc, char* argv[])
{
    // Specify what log levels to use. NORMAL is warning and above
    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;

    // This is the main point of interaction with the stack
    DNP3Manager manager(1, ConsoleLogger::Create());

    // Create a TCP server (listener)
    auto channel = manager.AddTCPServer("server", FILTERS, ChannelRetry::Default(), "0.0.0.0", 20000, PrintingChannelListener::Create());

    // The main object for an outstation
    OutstationStackConfig config(DatabaseSizes::AllTypes(10));

    // Specify the maximum size of the event buffers
    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);

    // Enable unsolicited reporting
    config.outstation.params.allowUnsolicited = true;

    // Link layer settings
    config.link.LocalAddr = 10;
    config.link.RemoteAddr = 1;
    config.link.KeepAliveTimeout = openpal::TimeDuration::Max();

    ConfigureDatabase(config.dbConfig);

    // Create a new outstation with the configuration
    auto outstation = channel->AddOutstation("outstation", SuccessCommandHandler::Create(), DefaultOutstationApplication::Create(), config);

    // Enable the outstation and start communication
    outstation->Enable();

    // Variables used in the example loop
    string input;
    State state;

    while (true)
    {
        std::cout << "Enter one or more measurement changes then press <enter>" << std::endl;
        std::cout << "c = counter, b = binary, d = doublebit, a = analog, 'quit' = exit" << std::endl;
        std::cin >> input;

        if (input == "quit") return 0;

        // Update measurement values based on input string
        UpdateBuilder builder;
        AddUpdates(builder, state, input);

        // Print current state to confirm changes
        std::cout << "Current State: " << std::endl;
        std::cout << "Counter: " << state.count << ", Analog: " << state.value 
                  << ", Binary: " << (state.binary ? "True" : "False") 
                  << ", DoubleBit: " << (state.dbit == DoubleBit::DETERMINED_ON ? "ON" : "OFF") << std::endl;

        // Apply the update to outstation
        outstation->Apply(builder.Build());
    }

    return 0;
}
