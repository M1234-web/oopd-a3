#include "MT26065_simulation.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using Clock = chrono::steady_clock;

static int physicalCores() {
    // portable C++ has no physical-core query; allow override with PARTD_PHYSICAL_CORES.
    const char* env = getenv("PARTD_PHYSICAL_CORES");
    if (env) return max(1, atoi(env));
    unsigned n = thread::hardware_concurrency();
    return max(1, static_cast<int>(n ? n : 2));
}

static double median(vector<double> x) {
    sort(x.begin(), x.end());
    const size_t n = x.size();
    return n % 2 ? x[n/2] : (x[n/2-1] + x[n/2]) / 2.0;
}

static void ensureOutput() { filesystem::create_directories("results/partD"); }

static Parameters baseParams(int N, int days, int threads) {
    Parameters p;
    p.N = N; p.days = days; p.threads = threads; p.chunks = max(1, 4 * threads);
    p.seed = 42; p.beta = 0.08; p.I0 = 10; p.v = 0.0;
    p.fixedDays = true; p.snapshotMode = "none"; p.snapshotEvery = 0;
    p.outputFile = "/tmp/partd_unused.csv";
    return p;
}

static vector<int> threadCounts(int maximum) {
    vector<int> v;
    for (int t = 1; t <= maximum; t *= 2) {
        v.push_back(t);
        if (t > maximum / 2) break;
    }
    if (v.empty() || v.back() != maximum) v.push_back(maximum);
    return v;
}

static void d1(int argc, char** argv) {
    ensureOutput();
    int N = argc > 2 ? stoi(argv[2]) : 4000;
    int days = argc > 3 ? stoi(argv[3]) : 200;
    int repeats = argc > 4 ? stoi(argv[4]) : 5;
    int maxT = argc > 5 ? stoi(argv[5]) : 2 * physicalCores();
    ofstream out("results/partD/d1_strong_scaling.csv");
    out << "threads,repeat,seconds,N,days,chunks\n";
    for (int t : threadCounts(maxT)) {
        for (int r = 1; r <= repeats; ++r) {
            Parameters p = baseParams(N, days, t);
            p.chunks = max(t, 4 * t);
            Simulation sim(p); // initialization and pool construction are outside timer
            double sec = sim.benchmarkComputation();
            out << t << "," << r << "," << sec << "," << N << "," << days << "," << p.chunks << "\n";
            out.flush();
            cerr << "D1 T=" << t << " repeat=" << r << " time=" << sec << " s\n";
        }
    }
}

static void d2(int argc, char** argv) {
    ensureOutput();
    int cellsPerThread = argc > 2 ? stoi(argv[2]) : 1000000;
    int days = argc > 3 ? stoi(argv[3]) : 200;
    int repeats = argc > 4 ? stoi(argv[4]) : 5;
    int maxT = argc > 5 ? stoi(argv[5]) : physicalCores();
    ofstream out("results/partD/d2_weak_scaling.csv");
    out << "threads,N,repeat,seconds,cells_per_thread,days\n";
    for (int t : threadCounts(maxT)) {
        const int N = static_cast<int>(ceil(sqrt(static_cast<double>(cellsPerThread) * t)));
        for (int r = 1; r <= repeats; ++r) {
            Parameters p = baseParams(N, days, t);
            p.chunks = max(t, 4 * t);
            Simulation sim(p);
            double sec = sim.benchmarkComputation();
            out << t << "," << N << "," << r << "," << sec << "," << cellsPerThread << "," << days << "\n";
            out.flush();
            cerr << "D2 T=" << t << " N=" << N << " repeat=" << r << " time=" << sec << " s\n";
        }
    }
}

static void d3(int argc, char** argv) {
    ensureOutput();
    int N = argc > 2 ? stoi(argv[2]) : 1200;
    int days = argc > 3 ? stoi(argv[3]) : 100;
    int repeats = argc > 4 ? stoi(argv[4]) : 5;
    int T = argc > 5 ? stoi(argv[5]) : physicalCores();
    ofstream out("results/partD/d3_work_units.csv");
    out << "experiment,threads,chunks,division,execution,N,repeat,seconds\n";

    for (int chunks : {T, 2*T, 4*T, 8*T, 16*T, 32*T, 64*T}) {
        for (int r = 1; r <= repeats; ++r) {
            Parameters p = baseParams(N, days, T);
            p.chunks = max(1, chunks);
            p.division = "strips";
            Simulation sim(p);
            double sec = sim.benchmarkComputation();
            out << "chunk_sweep," << T << "," << chunks << ",strips,pool," << N << "," << r << "," << sec << "\n";
        }
    }

    for (const string division : {"strips", "blocks", "interleaved"}) {
        for (int r = 1; r <= repeats; ++r) {
            Parameters p = baseParams(N, days, T);
            p.chunks = 8 * T;
            p.division = division;
            Simulation sim(p);
            double sec = sim.benchmarkComputation();
            out << "division," << T << "," << p.chunks << "," << division << ",pool," << N << "," << r << "," << sec << "\n";
        }
    }

    for (int size : {50, 100, 200, 500, 1000}) {
        for (const string mode : {"sequential", "thread-per-day", "pool"}) {
            for (int r = 1; r <= repeats; ++r) {
                Parameters p = baseParams(size, days, mode == "sequential" ? 1 : T);
                p.chunks = 8 * max(1, p.threads);
                p.execution = (mode == "thread-per-day") ? "thread-per-day" : "pool";
                Simulation sim(p);
                double sec = sim.benchmarkComputation();
                out << "small_grid_" << mode << "," << p.threads << "," << p.chunks
                    << ",strips," << mode << "," << size << "," << r << "," << sec << "\n";
            }
        }
    }
    out.close();
}

static void d4(int argc, char** argv) {
    ensureOutput();
    int N = argc > 2 ? stoi(argv[2]) : 1000;
    int days = argc > 3 ? stoi(argv[3]) : 200;
    int repeats = argc > 4 ? stoi(argv[4]) : 5;
    int P = argc > 5 ? stoi(argv[5]) : physicalCores();
    ofstream out("results/partD/d4_run_parallelism.csv");
    out << "runs_at_once,threads_per_run,total_threads,repeat,total_seconds,runs_per_second\n";

    // The 32 independent parameter/seed combinations are created before timing.
    vector<Parameters> all;
    for (int i = 0; i < 32; ++i) {
        Parameters p = baseParams(N, days, 1);
        p.seed = 42 + static_cast<uint64_t>(i);
        p.beta = 0.05 + 0.005 * (i % 5);
        p.v = 0.05 * (i % 3);
        all.push_back(p);
    }

    for (int R : {1, 2, 4, 8}) {
        if (R > P || P % R != 0) continue;
        int T = P / R;
        for (int rep = 1; rep <= repeats; ++rep) {
            const auto start = Clock::now();
            size_t nextRun = 0;
            while (nextRun < all.size()) {
                vector<thread> group;
                const int count = min(R, static_cast<int>(all.size() - nextRun));
                for (int j = 0; j < count; ++j) {
                    Parameters p = all[nextRun + j];
                    p.threads = T;
                    p.chunks = 4 * T;
                    group.emplace_back([p]() {
                        Simulation sim(p);
                        (void)sim.benchmarkComputation();
                    });
                }
                for (auto& th : group) th.join();
                nextRun += static_cast<size_t>(count);
            }
            const double sec = chrono::duration<double>(Clock::now() - start).count();
            out << R << "," << T << "," << R*T << "," << rep << "," << sec << ","
                << (32.0 / sec) << "\n";
            out.flush();
            cerr << "D4 R=" << R << " T=" << T << " repeat=" << rep << " time=" << sec << " s\n";
        }
    }
}


static void d5(int argc, char** argv) {
    ensureOutput();

    int N = argc > 2 ? stoi(argv[2]) : 250;
    int days = argc > 3 ? stoi(argv[3]) : 200;
    int repeats = argc > 4 ? stoi(argv[4]) : 5;

    ofstream out("results/partD/d5_io_sweep.csv");
    out << "sweep,mode,threads,N,days,K,latency_ms,Q,snapshots,"
           "estimated_queue_copy_bytes,repeat,seconds\n";

    const uint64_t cells = static_cast<uint64_t>(N) * N;
    const uint64_t bytesPerSnapshot = cells * sizeof(Person);

    auto runCase = [&](const string& sweep, const string& mode,
                       int latency, int K, int Q) {

        for (int rep = 1; rep <= repeats; ++rep) {
            Parameters p = baseParams(N, days, max(1, physicalCores()));

            p.chunks = 4 * p.threads;
            p.snapshotMode = mode;
            p.snapshotEvery = (mode == "none") ? 0 : K;
            p.ioLatencyMs = latency;
            p.maxPending = Q;

            p.snapshotDir = "results/partD/d5_snapshots/" +
                sweep + "_" + mode +
                "_L" + to_string(latency) +
                "_K" + to_string(K) +
                "_Q" + to_string(Q);

            Simulation sim(p);
            double sec = sim.benchmarkIO();

            int snapshotCount =
                (mode == "none") ? 0 : days / K;

            uint64_t estimatedMemory =
                (mode == "async")
                ? static_cast<uint64_t>(Q) * bytesPerSnapshot
                : 0;

            out << sweep << "," << mode << "," << p.threads << ","
                << N << "," << days << "," << p.snapshotEvery << ","
                << latency << "," << Q << "," << snapshotCount << ","
                << estimatedMemory << "," << rep << "," << sec << "\n";

            out.flush();

            cerr << "D5 " << sweep << " mode=" << mode
                 << " L=" << latency << " K=" << K
                 << " Q=" << Q << " repeat=" << rep
                 << " time=" << sec << " s\n";
        }
    };

    // Baseline: no snapshots.
    runCase("baseline", "none", 0, 0, 1);

    // Experiment 1: vary artificial I/O latency.
    for (int latency : {0, 5, 10, 20, 50}) {
        runCase("latency", "blocking", latency, 10, 2);
        runCase("latency", "async", latency, 10, 2);
    }

    // Experiment 2: vary snapshot frequency.
    for (int K : {5, 10, 20, 40}) {
        runCase("frequency", "blocking", 20, K, 2);
        runCase("frequency", "async", 20, K, 2);
    }

    // Experiment 3: vary asynchronous queue capacity.
    for (int Q : {1, 2, 4, 8}) {
        runCase("queue", "async", 20, 10, Q);
    }

    out.close();
    cerr << "D5 sweep completed.\n";
}


int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            cerr << "Usage: ./benchmarks d1|d2|d3|d4|d5 [arguments]\n";
            return 1;
        }
        const string which = argv[1];
        if (which == "d1") d1(argc, argv);
        else if (which == "d2") d2(argc, argv);
        else if (which == "d3") d3(argc, argv);
        else if (which == "d4") d4(argc, argv);
        else if (which == "d5") d5(argc, argv);
        else throw invalid_argument("Unknown benchmark: " + which);
    } catch (const exception& e) {
        cerr << "Benchmark error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
