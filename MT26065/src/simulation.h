#ifndef SIMULATION_H
#define SIMULATION_H

#include <cstdint>
#include <string>
#include <vector>

class ThreadPool;

enum class State {
    S, I, R, V, D
};

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

    std::string division = "strips";
    std::string parallelMode = "sequential";

    int snapshotEvery = 0;
    std::string snapshotDir = "snapshots";

    std::string outputFile = "results.csv";
    bool fixedDays = false;
};

struct Counts {
    uint64_t S = 0;
    uint64_t I = 0;
    uint64_t R = 0;
    uint64_t V = 0;
    uint64_t D = 0;
};

class Simulation {
private:
    Parameters params;

    std::vector<Person> current;
    std::vector<Person> next;

    double betaEff;
    bool lockdownActive = false;

    uint64_t hashRandom(
        uint64_t day,
        uint64_t cell,
        uint64_t stream
    ) const;

    double random01(
        uint64_t day,
        uint64_t cell,
        uint64_t stream
    ) const;

    void initialize();

    Counts countStates() const;
    uint64_t checksum() const;

    int infectedNeighbours(int row, int col) const;

    void updateCell(int row, int col, uint64_t day);

    Counts updateChunk(
        int chunk,
        uint64_t day
    );

    Counts updateDay(
        uint64_t day,
        ThreadPool* pool
    );

    void updateDaySequential(uint64_t day);

public:
    explicit Simulation(const Parameters& p);

    void run();
};

#endif