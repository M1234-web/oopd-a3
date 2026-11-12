#include "MT26065_simulation.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace std;

void printUsage() {
    cout << R"(
Usage:
./pandemic [options]

Options:
  --N <int>                Grid size
  --days <int>             Maximum simulation days
  --seed <int>             Random seed
  --beta <double>          Transmission probability
  --d <int>                Infectious duration
  --mu <double>            Mortality probability
  --I0 <int>               Initially infected
  --v <double>             Vaccinated fraction
  --m <double>             Mask factor
  --lambda <double>        Lockdown factor
  --theta-on <double>      Lockdown activation threshold
  --theta-off <double>     Lockdown deactivation threshold
  --threads <int>          Number of threads
  --chunks <int>           Number of chunks
  --division <string>      Grid division strategy

Part C snapshot options:
  --snapshot-every <int>   Save a snapshot every K days
  --snapshot-dir <string>  Snapshot output directory
  --async-io               Enable asynchronous snapshot writing
  --max-pending <int>      Maximum pending asynchronous writes
  --io-latency <int>       Artificial write delay in milliseconds

Other options:
  --output <string>        CSV output filename
  --fixed-days             Run exactly the specified days
  --help                   Display help
)";
}

int main(int argc, char* argv[]) {

    Parameters params;

    try {

        for (int i = 1; i < argc; i++) {

            string arg = argv[i];

            if (arg == "--help") {
                printUsage();
                return 0;
            }

            if (arg == "--fixed-days") {
                params.fixedDays = true;
                continue;
            }

            if (arg == "--async-io") {
                params.asyncIO = true;
                continue;
            }

            if (i + 1 >= argc) {
                throw invalid_argument("Missing value for " + arg);
            }

            string value = argv[++i];

            if (arg == "--N")
                params.N = stoi(value);

            else if (arg == "--days")
                params.days = stoi(value);

            else if (arg == "--seed")
                params.seed = stoull(value);

            else if (arg == "--beta")
                params.beta = stod(value);

            else if (arg == "--d")
                params.d = stoi(value);

            else if (arg == "--mu")
                params.mu = stod(value);

            else if (arg == "--I0")
                params.I0 = stoi(value);

            else if (arg == "--v")
                params.v = stod(value);

            else if (arg == "--m")
                params.m = stod(value);

            else if (arg == "--lambda")
                params.lambda = stod(value);

            else if (arg == "--theta-on")
                params.thetaOn = stod(value);

            else if (arg == "--theta-off")
                params.thetaOff = stod(value);

            else if (arg == "--threads")
                params.threads = stoi(value);

            else if (arg == "--chunks")
                params.chunks = stoi(value);

            else if (arg == "--division")
                params.division = value;

            else if (arg == "--snapshot-every")
                params.snapshotEvery = stoi(value);

            else if (arg == "--snapshot-dir")
                params.snapshotDir = value;

            else if (arg == "--max-pending")
                params.maxPending = stoi(value);

            else if (arg == "--io-latency")
                params.ioLatencyMs = stoi(value);

            else if (arg == "--output")
                params.outputFile = value;

            else
                throw invalid_argument("Unknown argument: " + arg);
        }

        if (params.threads <= 0 ||
            params.chunks <= 0 ||
            params.snapshotEvery < 0 ||
            params.maxPending <= 0 ||
            params.ioLatencyMs < 0 ||
            params.outputFile.empty() ||
            params.snapshotDir.empty()) {

            throw invalid_argument("Invalid configuration");
        }

        cout << "Starting pandemic simulation\n";

        cout << "Grid size: "
             << params.N << " x " << params.N << "\n";

        cout << "Snapshot interval: "
             << params.snapshotEvery << "\n";

        cout << "I/O mode: "
             << (params.asyncIO ? "Asynchronous" : "Blocking")
             << "\n";

        if (params.asyncIO) {
            cout << "Maximum pending writes: "
                 << params.maxPending << "\n";
        }

        cout << "I/O latency: "
             << params.ioLatencyMs << " ms\n";

        Simulation simulation(params);
        simulation.run();
    }
    catch (const exception& e) {

        cerr << "Error: " << e.what() << "\n";
        printUsage();

        return 1;
    }

    return 0;
}