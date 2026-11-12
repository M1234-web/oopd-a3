#include "MT26065_simulation.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <limits>

#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <memory>
#include <utility>

using namespace std;


// ============================================================
// THREAD POOL
// ============================================================

class ThreadPool {

private:

    vector<thread> workers;

    queue<function<void()>> tasks;

    mutex queueMutex;
    condition_variable condition;

    bool stopping = false;

public:

    explicit ThreadPool(int T) {

        for (int i = 0; i < T; i++) {

            workers.emplace_back([this]() {

                while (true) {

                    function<void()> task;

                    {
                        unique_lock<mutex> lock(queueMutex);

                        condition.wait(lock, [this]() {
                            return stopping || !tasks.empty();
                        });

                        if (stopping && tasks.empty())
                            return;

                        task = move(tasks.front());
                        tasks.pop();
                    }

                    task();
                }
            });
        }
    }


    template<typename F>
    auto submit(F&& f) {

        using ReturnType =
            decltype(f());

        auto task = make_shared<
            packaged_task<ReturnType()>
        >(forward<F>(f));

        future<ReturnType> result =
            task->get_future();

        {
            lock_guard<mutex> lock(queueMutex);

            if (stopping)
                throw runtime_error("Thread pool stopped");

            tasks.emplace([task]() {
                (*task)();
            });
        }

        condition.notify_one();

        return result;
    }


    ~ThreadPool() {

        {
            lock_guard<mutex> lock(queueMutex);
            stopping = true;
        }

        condition.notify_all();

        for (auto& worker : workers) {

            if (worker.joinable())
                worker.join();
        }
    }
};


// ============================================================
// BLOCK REPRESENTATION
// ============================================================

struct Block {

    int r0, r1;
    int c0, c1;

};


// Split the grid into C approximately square blocks.

static vector<Block> createBlocks(int N, int C) {

    vector<Block> blocks;

    blocks.push_back({0, N, 0, N});

    while (static_cast<int>(blocks.size()) < C) {

        int best = -1;
        long long largestArea = -1;

        for (int i = 0; i < static_cast<int>(blocks.size()); i++) {

            int height =
                blocks[i].r1 - blocks[i].r0;

            int width =
                blocks[i].c1 - blocks[i].c0;

            long long area =
                static_cast<long long>(height) * width;

            bool canSplit =
                (height > 1 || width > 1);

            if (canSplit && area > largestArea) {

                largestArea = area;
                best = i;
            }
        }

        // If there are more tasks than cells,
        // create empty blocks for the remaining tasks.

        if (best == -1) {

            blocks.push_back({0, 0, 0, 0});
            continue;
        }

        Block b = blocks[best];

        int height = b.r1 - b.r0;
        int width = b.c1 - b.c0;

        Block first, second;

        if (height >= width && height > 1) {

            int mid = b.r0 + height / 2;

            first = {
                b.r0, mid,
                b.c0, b.c1
            };

            second = {
                mid, b.r1,
                b.c0, b.c1
            };

        } else {

            int mid = b.c0 + width / 2;

            first = {
                b.r0, b.r1,
                b.c0, mid
            };

            second = {
                b.r0, b.r1,
                mid, b.c1
            };
        }

        blocks[best] = first;
        blocks.push_back(second);
    }

    return blocks;
}


// ============================================================
// SPLITMIX64 DETERMINISTIC HASH
// ============================================================

static uint64_t splitmix64(uint64_t x) {

    x += 0x9e3779b97f4a7c15ULL;

    x = (x ^ (x >> 30))
        * 0xbf58476d1ce4e5b9ULL;

    x = (x ^ (x >> 27))
        * 0x94d049bb133111ebULL;

    return x ^ (x >> 31);
}


// ============================================================
// CONSTRUCTOR
// ============================================================

Simulation::Simulation(const Parameters& p)
    : params(p),
      betaEff(p.beta * p.m) {

    if (p.N <= 0 || p.days < 0 || p.d <= 0 ||
        p.I0 < 0 || p.v < 0 || p.v > 1 ||
        p.beta < 0 || p.beta > 1 ||
        p.mu < 0 || p.mu > 1 ||
        p.m <= 0 || p.m > 1 ||
        p.lambda < 0 || p.lambda > 1 ||
        p.thetaOn < 0 || p.thetaOn > 1 ||
        p.thetaOff < 0 || p.thetaOff > p.thetaOn) {

        throw invalid_argument("Invalid simulation parameters");
    }

    if (p.threads <= 0 || p.chunks <= 0) {
        throw invalid_argument("Threads and chunks must be positive");
    }

    if (p.division == "interleaved" && p.chunks < p.threads) {

        throw invalid_argument(
            "Interleaved division requires chunks >= threads"
        );
    }

    if (p.division != "strips" &&
        p.division != "blocks" &&
        p.division != "interleaved") {

        throw invalid_argument("Invalid division strategy");
    }

    if (p.parallelMode != "sequential" &&
        p.parallelMode != "thread-per-day" &&
        p.parallelMode != "pool") {

        throw invalid_argument("Invalid parallel mode");
    }

    uint64_t total =
        static_cast<uint64_t>(p.N) * p.N;

    if (total > numeric_limits<size_t>::max()) {
        throw invalid_argument("Grid size is too large");
    }

    if (static_cast<uint64_t>(p.I0) > total) {
        throw invalid_argument("I0 exceeds population size");
    }

    current.resize(static_cast<size_t>(total));
    next.resize(static_cast<size_t>(total));

    initialize();
}


// ============================================================
// RANDOM NUMBER GENERATION
// ============================================================

uint64_t Simulation::hashRandom(
    uint64_t day,
    uint64_t cell,
    uint64_t stream
) const {

    uint64_t x = splitmix64(params.seed);

    x = splitmix64(x ^ day);
    x = splitmix64(x ^ cell);
    x = splitmix64(x ^ stream);

    return x;
}


double Simulation::random01(
    uint64_t day,
    uint64_t cell,
    uint64_t stream
) const {

    return static_cast<double>(
        hashRandom(day, cell, stream) >> 11
    ) * (1.0 / 9007199254740992.0);
}


// ============================================================
// INITIALIZATION
// ============================================================

void Simulation::initialize() {

    uint64_t total = current.size();

    for (auto& person : current) {

        person.state = State::S;
        person.infectedDays = 0;
    }

    vector<pair<uint64_t, uint64_t>> order;

    order.reserve(total);

    for (uint64_t i = 0; i < total; i++) {

        order.push_back({
            hashRandom(0, i, 2),
            i
        });
    }

    sort(order.begin(), order.end());

    uint64_t vaccinated =
        static_cast<uint64_t>(params.v * total);

    for (uint64_t i = 0; i < vaccinated; i++) {

        current[order[i].second].state = State::V;
    }

    uint64_t infected = 0;

    for (const auto& item : order) {

        if (infected == static_cast<uint64_t>(params.I0))
            break;

        uint64_t id = item.second;

        if (current[id].state == State::S) {

            current[id].state = State::I;
            current[id].infectedDays = 0;

            infected++;
        }
    }

    if (infected != static_cast<uint64_t>(params.I0)) {

        throw invalid_argument(
            "Not enough susceptible people for initial infections"
        );
    }

    next = current;
}


// ============================================================
// COUNT STATES
// ============================================================

Counts Simulation::countStates() const {

    Counts counts;

    for (const auto& person : current) {

        switch (person.state) {

            case State::S: counts.S++; break;
            case State::I: counts.I++; break;
            case State::R: counts.R++; break;
            case State::V: counts.V++; break;
            case State::D: counts.D++; break;
        }
    }

    return counts;
}


// ============================================================
// CHECKSUM
// ============================================================

uint64_t Simulation::checksum() const {

    uint64_t hash = 0;

    for (uint64_t i = 0; i < current.size(); i++) {

        uint64_t value =
            static_cast<uint64_t>(current[i].state);

        value = splitmix64(
            value ^
            (static_cast<uint64_t>(
                current[i].infectedDays) << 8)
        );

        hash ^= splitmix64(value ^ i);
    }

    return hash;
}


// ============================================================
// INFECTED NEIGHBOURS
// ============================================================

int Simulation::infectedNeighbours(
    int row,
    int col
) const {

    int count = 0;

    for (int dr = -1; dr <= 1; dr++) {

        for (int dc = -1; dc <= 1; dc++) {

            if (dr == 0 && dc == 0)
                continue;

            int nr = row + dr;
            int nc = col + dc;

            if (nr < 0 || nr >= params.N ||
                nc < 0 || nc >= params.N)
                continue;

            uint64_t id =
                static_cast<uint64_t>(nr) * params.N + nc;

            if (current[id].state == State::I)
                count++;
        }
    }

    return count;
}


// ============================================================
// UPDATE ONE CELL
// ============================================================

void Simulation::updateCell(
    int row,
    int col,
    uint64_t day
) {

    uint64_t id =
        static_cast<uint64_t>(row) * params.N + col;

    const Person& person = current[id];

    Person& result = next[id];

    if (person.state == State::S) {

        int k = infectedNeighbours(row, col);

        double probability =
            1.0 - pow(1.0 - betaEff, k);

        if (random01(day, id, 0) < probability) {

            result.state = State::I;
            result.infectedDays = 0;

        } else {

            result = person;
        }
    }

    else if (person.state == State::I) {

        int newCounter = person.infectedDays + 1;

        if (newCounter == params.d) {

            if (random01(day, id, 1) < params.mu) {

                result.state = State::D;

            } else {

                result.state = State::R;
            }

            result.infectedDays = 0;

        } else {

            result.state = State::I;
            result.infectedDays = newCounter;
        }
    }

    else {

        result = person;
    }
}


// ============================================================
// UPDATE A CHUNK AND RETURN LOCAL COUNTS
// ============================================================

Counts Simulation::updateChunk(
    int chunk,
    uint64_t day
) {

    int N = params.N;
    int C = params.chunks;

    int r0 = 0;
    int r1 = 0;

    int c0 = 0;
    int c1 = N;

    vector<Block> blocks;

    if (params.division == "strips") {

        r0 = (N * chunk) / C;
        r1 = (N * (chunk + 1)) / C;
    }

    else if (params.division == "blocks") {

        blocks = createBlocks(N, C);

        r0 = blocks[chunk].r0;
        r1 = blocks[chunk].r1;

        c0 = blocks[chunk].c0;
        c1 = blocks[chunk].c1;
    }

    Counts local;


    // Interleaved rows:

    if (params.division == "interleaved") {

    int T = params.threads;

    // The first T tasks each own one interleaved
    // sequence of rows. Extra tasks are empty.

    if (chunk >= T)
        return local;

    for (int row = chunk; row < N; row += T) {

        for (int col = 0; col < N; col++) {

            updateCell(row, col, day);

            const Person& p =
                next[static_cast<uint64_t>(row) * N + col];

            switch (p.state) {

                case State::S: local.S++; break;
                case State::I: local.I++; break;
                case State::R: local.R++; break;
                case State::V: local.V++; break;
                case State::D: local.D++; break;
            }
        }
    }

    return local;
}


    // Strips or square blocks

    for (int row = r0; row < r1; row++) {

        for (int col = c0; col < c1; col++) {

            updateCell(row, col, day);

            const Person& p =
                next[static_cast<uint64_t>(row) * N + col];

            switch (p.state) {

                case State::S: local.S++; break;
                case State::I: local.I++; break;
                case State::R: local.R++; break;
                case State::V: local.V++; break;
                case State::D: local.D++; break;
            }
        }
    }

    return local;
}


// ============================================================
// SEQUENTIAL UPDATE
// ============================================================

void Simulation::updateDaySequential(uint64_t day) {

    for (int i = 0; i < params.N; i++) {

        for (int j = 0; j < params.N; j++) {

            updateCell(i, j, day);
        }
    }

    current.swap(next);
}


// ============================================================
// UPDATE ONE DAY
// ============================================================

Counts Simulation::updateDay(
    uint64_t day,
    ThreadPool* pool
) {

    if (params.parallelMode == "sequential") {

        updateDaySequential(day);

        return countStates();
    }


    int C = params.chunks;

    vector<Counts> results(C);


    // --------------------------------------------------------
    // MODE 1: THREAD PER DAY
    // --------------------------------------------------------

    if (params.parallelMode == "thread-per-day") {

        vector<thread> workers;

        workers.reserve(C);

        for (int i = 0; i < C; i++) {

            workers.emplace_back([this, i, day, &results]() {

                results[i] = updateChunk(i, day);
            });
        }

        // Join all threads before swapping grids.

        for (auto& worker : workers) {

            if (worker.joinable())
                worker.join();
        }
    }


    // --------------------------------------------------------
    // MODE 2: THREAD POOL
    // --------------------------------------------------------

    else if (params.parallelMode == "pool") {

        if (pool == nullptr) {

            throw runtime_error("Thread pool not initialized");
        }

        vector<future<Counts>> futures;

        futures.reserve(C);

        // Submit C tasks to the existing T-thread pool.

        for (int i = 0; i < C; i++) {

            futures.push_back(
                pool->submit([this, i, day]() {

                    return updateChunk(i, day);
                })
            );
        }

        // Wait for every task and collect local counts.

        for (int i = 0; i < C; i++) {

            results[i] = futures[i].get();
        }
    }


    // --------------------------------------------------------
    // ADD LOCAL COUNTS
    // --------------------------------------------------------

    Counts total;

    for (const auto& c : results) {

        total.S += c.S;
        total.I += c.I;
        total.R += c.R;
        total.V += c.V;
        total.D += c.D;
    }


    // Swap only after all tasks have completed.

    current.swap(next);

    return total;
}


// ============================================================
// MAIN SIMULATION
// ============================================================

void Simulation::run() {

    ofstream csv(params.outputFile);

    if (!csv.is_open()) {

        throw runtime_error("Cannot open output CSV file");
    }

    csv << "day,S,I,R,V,D,beta_eff,checksum\n";


    // Create the pool only once for pool mode.

    unique_ptr<ThreadPool> pool;

    if (params.parallelMode == "pool") {

        pool = make_unique<ThreadPool>(params.threads);
    }


    cout << "Execution mode: "
         << params.parallelMode << "\n";

    cout << "Division: "
         << params.division << "\n";

    cout << "Threads: "
         << params.threads << "\n";

    cout << "Chunks: "
         << params.chunks << "\n";


    // Day 0

    Counts counts = countStates();

    csv << 0 << ","
        << counts.S << ","
        << counts.I << ","
        << counts.R << ","
        << counts.V << ","
        << counts.D << ","
        << betaEff << ","
        << checksum() << "\n";

    cout << "Day 0: I = " << counts.I << "\n";


    // Remaining days

    for (int day = 1; day <= params.days; day++) {

        counts = updateDay(
            day,
            pool.get()
        );


        csv << day << ","
            << counts.S << ","
            << counts.I << ","
            << counts.R << ","
            << counts.V << ","
            << counts.D << ","
            << betaEff << ","
            << checksum() << "\n";


        cout << "Day " << day
             << ": S = " << counts.S
             << ", I = " << counts.I
             << ", R = " << counts.R
             << ", V = " << counts.V
             << ", D = " << counts.D
             << "\n";


        // Lockdown decision for the next day.

        double infectedFraction =
            static_cast<double>(counts.I) / current.size();

        if (!lockdownActive) {

            if (infectedFraction > params.thetaOn) {

                betaEff *= params.lambda;

                lockdownActive = true;
            }

        } else {

            if (infectedFraction < params.thetaOff) {

                betaEff = params.beta * params.m;

                lockdownActive = false;
            }
        }


        if (counts.I == 0 && !params.fixedDays) {
            break;
        }
    }


    csv.close();

    // Pool is destroyed here and its workers are joined.

    pool.reset();

    cout << "\nSimulation completed.\n";

    cout << "CSV saved to: "
         << params.outputFile << "\n";
}