#ifndef SIMULATION_H
#define SIMULATION_H

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

class ThreadPool;

enum class State { S, I, R, V, D };

struct Person {
    State state = State::S;
    int infectedDays = 0;
};

struct Parameters {
    int N = 1000;
    int days = 200;
    uint64_t seed = 42;
    double beta = 0.08;
    int d = 7;
    double mu = 0.01;
    int I0 = 10;
    double v = 0.0;
    double m = 1.0;
    double lambda = 0.3;
    double thetaOn = 0.02;
    double thetaOff = 0.005;

    int threads = 1;
    int chunks = 1;
    std::string division = "strips"; // strips, blocks, interleaved
    std::string execution = "pool";  // pool, thread-per-day

    int snapshotEvery = 0;
    std::string snapshotDir = "snapshots";
    std::string snapshotMode = "none"; // none, blocking, async
    int ioLatencyMs = 0;
    int maxPending = 2;

    std::string outputFile = "results.csv";
    bool fixedDays = false;
};

struct Counts {
    uint64_t S = 0, I = 0, R = 0, V = 0, D = 0;
};

struct RunSummary {
    double peakInfectedFraction = 0.0;
    int peakDay = 0;
    uint64_t totalInfected = 0;
    uint64_t totalDeaths = 0;
    int daysSimulated = 0;
};

class Simulation {
private:
    Parameters params;
    std::vector<Person> current, next;
    double betaEff;
    bool lockdownActive = false;
    std::unique_ptr<ThreadPool> pool;

    uint64_t hashRandom(uint64_t day, uint64_t cell, uint64_t stream) const;
    double random01(uint64_t day, uint64_t cell, uint64_t stream) const;
    void initialize();
    Counts countStates() const;
    uint64_t checksum() const;
    int infectedNeighbours(int row, int col) const;
    void updateCell(int row, int col, uint64_t day);
    void updateRange(int task, uint64_t day);
    void updateDay(uint64_t day);
    void updateLockdown(const Counts& counts);
    void writeSnapshot(uint64_t day, int latencyMs) const;
    void runDays(bool withSnapshots, bool collectSummary, RunSummary* summary);

public:
    explicit Simulation(const Parameters& p);
    ~Simulation();
    void run();
    double benchmarkComputation();
    double benchmarkIO();
    RunSummary runSummary();
    std::size_t population() const { return current.size(); }
};

#endif
