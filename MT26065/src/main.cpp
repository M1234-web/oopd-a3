#include "simulation.h"
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace std;

static void usage() {
    cout << "Usage: ./pandemic [options]\n"
         << "  --N N --days D --seed S --beta B --d D --mu M --I0 I --v V\n"
         << "  --m M --lambda L --theta-on X --theta-off X\n"
         << "  --threads T --chunks C --division strips|blocks|interleaved\n"
         << "  --execution pool|thread-per-day\n"
         << "  --snapshot-every K --snapshot-dir DIR\n"
         << "  --snapshot-mode none|blocking|async --io-latency MS --max-pending Q\n"
         << "  --output FILE --fixed-days --help\n";
}

int main(int argc, char** argv) {
    Parameters p;
    try {
        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            if (a == "--help") { usage(); return 0; }
            if (a == "--fixed-days") { p.fixedDays = true; continue; }
            if (i + 1 >= argc) throw invalid_argument("Missing value for " + a);
            string v = argv[++i];
            if (a == "--N") p.N = stoi(v);
            else if (a == "--days") p.days = stoi(v);
            else if (a == "--seed") p.seed = stoull(v);
            else if (a == "--beta") p.beta = stod(v);
            else if (a == "--d") p.d = stoi(v);
            else if (a == "--mu") p.mu = stod(v);
            else if (a == "--I0") p.I0 = stoi(v);
            else if (a == "--v") p.v = stod(v);
            else if (a == "--m") p.m = stod(v);
            else if (a == "--lambda") p.lambda = stod(v);
            else if (a == "--theta-on") p.thetaOn = stod(v);
            else if (a == "--theta-off") p.thetaOff = stod(v);
            else if (a == "--threads") p.threads = stoi(v);
            else if (a == "--chunks") p.chunks = stoi(v);
            else if (a == "--division") p.division = v;
            else if (a == "--execution") p.execution = v;
            else if (a == "--snapshot-every") p.snapshotEvery = stoi(v);
            else if (a == "--snapshot-dir") p.snapshotDir = v;
            else if (a == "--snapshot-mode") p.snapshotMode = v;
            else if (a == "--io-latency") p.ioLatencyMs = stoi(v);
            else if (a == "--max-pending") p.maxPending = stoi(v);
            else if (a == "--output") p.outputFile = v;
            else throw invalid_argument("Unknown option: " + a);
        }
        Simulation sim(p);
        sim.run();
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        usage();
        return 1;
    }
    return 0;
}
