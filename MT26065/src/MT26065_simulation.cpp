#include "MT26065_simulation.h"
#include "thread_pool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace std;
using Clock = chrono::steady_clock;

static uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

Simulation::Simulation(const Parameters& p)
    : params(p), betaEff(p.beta * p.m) {
    if (p.N <= 0 || p.days < 0 || p.d <= 0 || p.I0 < 0 ||
        p.v < 0 || p.v > 1 || p.beta < 0 || p.beta > 1 ||
        p.mu < 0 || p.mu > 1 || p.m <= 0 || p.m > 1 ||
        p.lambda < 0 || p.lambda > 1 || p.thetaOn < 0 ||
        p.thetaOn > 1 || p.thetaOff < 0 || p.thetaOff > p.thetaOn ||
        p.threads <= 0 || p.chunks <= 0 || p.snapshotEvery < 0 ||
        p.ioLatencyMs < 0 || p.maxPending <= 0) {
        throw invalid_argument("Invalid simulation parameters");
    }
    if (p.division != "strips" && p.division != "blocks" &&
        p.division != "interleaved") {
        throw invalid_argument("division must be strips, blocks, or interleaved");
    }
    if (p.execution != "pool" && p.execution != "thread-per-day") {
        throw invalid_argument("execution must be pool or thread-per-day");
    }
    if (p.snapshotMode != "none" && p.snapshotMode != "blocking" &&
        p.snapshotMode != "async") {
        throw invalid_argument("snapshot mode must be none, blocking, or async");
    }

    uint64_t total = static_cast<uint64_t>(p.N) * static_cast<uint64_t>(p.N);
    if (total > numeric_limits<size_t>::max()) throw invalid_argument("Grid too large");
    if (static_cast<uint64_t>(p.I0) > total) throw invalid_argument("I0 exceeds population");
    if (static_cast<uint64_t>(p.I0) + static_cast<uint64_t>(p.v * total) > total) {
        throw invalid_argument("Vaccinated population leaves too few people for I0");
    }

    current.resize(static_cast<size_t>(total));
    next.resize(static_cast<size_t>(total));
    initialize();

    if (params.threads > 1 && params.execution == "pool") {
        pool = make_unique<ThreadPool>(params.threads);
    }
}

Simulation::~Simulation() = default;

uint64_t Simulation::hashRandom(uint64_t day, uint64_t cell, uint64_t stream) const {
    uint64_t x = splitmix64(params.seed);
    x = splitmix64(x ^ day);
    x = splitmix64(x ^ cell);
    return splitmix64(x ^ stream);
}

double Simulation::random01(uint64_t day, uint64_t cell, uint64_t stream) const {
    return static_cast<double>(hashRandom(day, cell, stream) >> 11) *
           (1.0 / 9007199254740992.0);
}

void Simulation::initialize() {
    const uint64_t total = current.size();
    for (auto& person : current) person = Person{};

    // Retains the deterministic selection rule used by the original Part A code.
    vector<pair<uint64_t, uint64_t>> order;
    order.reserve(static_cast<size_t>(total));
    for (uint64_t i = 0; i < total; ++i) order.emplace_back(hashRandom(0, i, 2), i);
    sort(order.begin(), order.end());

    const uint64_t vaccinated = static_cast<uint64_t>(params.v * total);
    for (uint64_t i = 0; i < vaccinated; ++i) current[order[i].second].state = State::V;

    uint64_t infected = 0;
    for (const auto& item : order) {
        if (infected == static_cast<uint64_t>(params.I0)) break;
        Person& person = current[item.second];
        if (person.state == State::S) {
            person.state = State::I;
            person.infectedDays = 0;
            ++infected;
        }
    }
    if (infected != static_cast<uint64_t>(params.I0))
        throw invalid_argument("Not enough susceptible people for initial infections");
    next = current;
}

Counts Simulation::countStates() const {
    Counts c;
    for (const auto& p : current) {
        switch (p.state) {
            case State::S: ++c.S; break;
            case State::I: ++c.I; break;
            case State::R: ++c.R; break;
            case State::V: ++c.V; break;
            case State::D: ++c.D; break;
        }
    }
    return c;
}

uint64_t Simulation::checksum() const {
    uint64_t hash = 0;
    for (uint64_t i = 0; i < current.size(); ++i) {
        uint64_t value = static_cast<uint64_t>(current[i].state);
        value = splitmix64(value ^ (static_cast<uint64_t>(current[i].infectedDays) << 8));
        hash ^= splitmix64(value ^ i);
    }
    return hash;
}

int Simulation::infectedNeighbours(int row, int col) const {
    int count = 0;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            const int nr = row + dr, nc = col + dc;
            if (nr < 0 || nr >= params.N || nc < 0 || nc >= params.N) continue;
            const uint64_t id = static_cast<uint64_t>(nr) * params.N + nc;
            if (current[id].state == State::I) ++count;
        }
    }
    return count;
}

void Simulation::updateCell(int row, int col, uint64_t day) {
    const uint64_t id = static_cast<uint64_t>(row) * params.N + col;
    const Person& person = current[id];
    Person& result = next[id];

    if (person.state == State::S) {
        const int k = infectedNeighbours(row, col);
        const double probability = 1.0 - pow(1.0 - betaEff, k);
        if (random01(day, id, 0) < probability) {
            result.state = State::I;
            result.infectedDays = 0;
        } else result = person;
    } else if (person.state == State::I) {
        const int newCounter = person.infectedDays + 1;
        if (newCounter == params.d) {
            result.state = (random01(day, id, 1) < params.mu) ? State::D : State::R;
            result.infectedDays = 0;
        } else {
            result.state = State::I;
            result.infectedDays = newCounter;
        }
    } else result = person;
}

void Simulation::updateRange(int task, uint64_t day) {
    const uint64_t total = current.size();
    const int tasks = max(1, params.chunks);
    if (params.division == "interleaved") {
        for (uint64_t id = static_cast<uint64_t>(task); id < total; id += static_cast<uint64_t>(tasks)) {
            updateCell(static_cast<int>(id / params.N), static_cast<int>(id % params.N), day);
        }
    } else if (params.division == "blocks") {
        // Factor chunks into a rectangular tile grid so every cell is covered exactly once.
        int tileRows = max(1, static_cast<int>(sqrt(static_cast<double>(tasks))));
        while (tileRows > 1 && tasks % tileRows != 0) --tileRows;
        const int tileCols = tasks / tileRows;
        const int tileRow = task / tileCols;
        const int tileCol = task % tileCols;
        const int r0 = params.N * tileRow / tileRows;
        const int r1 = params.N * (tileRow + 1) / tileRows;
        const int c0 = params.N * tileCol / tileCols;
        const int c1 = params.N * (tileCol + 1) / tileCols;
        for (int r = r0; r < r1; ++r)
            for (int c = c0; c < c1; ++c)
                updateCell(r, c, day);
    } else {
        const uint64_t begin = total * static_cast<uint64_t>(task) / static_cast<uint64_t>(tasks);
        const uint64_t end = total * static_cast<uint64_t>(task + 1) / static_cast<uint64_t>(tasks);
        for (uint64_t id = begin; id < end; ++id)
            updateCell(static_cast<int>(id / params.N), static_cast<int>(id % params.N), day);
    }
}

void Simulation::updateDay(uint64_t day) {
    if (params.threads <= 1) {
        for (int r = 0; r < params.N; ++r)
            for (int c = 0; c < params.N; ++c) updateCell(r, c, day);
    } else if (params.execution == "thread-per-day") {
        vector<thread> workers;
        workers.reserve(static_cast<size_t>(params.threads));
        const int taskCount = params.chunks;
        atomic<int> nextTask{0};
        const int workerCount = params.threads;
        for (int w = 0; w < workerCount; ++w) {
            workers.emplace_back([&, day]() {
                for (;;) {
                    int task = nextTask.fetch_add(1);
                    if (task >= taskCount) break;
                    updateRange(task, day);
                }
            });
        }
        for (auto& t : workers) t.join();
    } else {
        pool->parallelFor(params.chunks, [this, day](int task) { updateRange(task, day); });
    }
    current.swap(next);
}

void Simulation::updateLockdown(const Counts& counts) {
    const double fraction = static_cast<double>(counts.I) / current.size();
    if (!lockdownActive) {
        if (fraction > params.thetaOn) {
            betaEff *= params.lambda;
            lockdownActive = true;
        }
    } else if (fraction < params.thetaOff) {
        betaEff = params.beta * params.m;
        lockdownActive = false;
    }
}

void Simulation::writeSnapshot(uint64_t day, int latencyMs) const {
    filesystem::create_directories(params.snapshotDir);
    const string filename = params.snapshotDir + "/day_" + to_string(day) + ".ppm";
    ofstream out(filename);
    if (!out) throw runtime_error("Cannot write snapshot: " + filename);
    out << "P3\n" << params.N << " " << params.N << "\n255\n";
    for (int r = 0; r < params.N; ++r) {
        for (int c = 0; c < params.N; ++c) {
            const State s = current[static_cast<size_t>(r) * params.N + c].state;
            int red = 255, green = 255, blue = 255;
            if (s == State::I) { red = 220; green = 30; blue = 30; }
            else if (s == State::R) { red = 30; green = 160; blue = 50; }
            else if (s == State::V) { red = 30; green = 80; blue = 220; }
            else if (s == State::D) { red = 20; green = 20; blue = 20; }
            out << red << " " << green << " " << blue << " ";
        }
        out << "\n";
    }
    out.close();
    if (latencyMs > 0) this_thread::sleep_for(chrono::milliseconds(latencyMs));
}

void Simulation::runDays(bool withSnapshots, bool collectSummary, RunSummary* summary) {
    Counts counts = countStates();
    if (summary) {
        summary->peakInfectedFraction = static_cast<double>(counts.I) / current.size();
        summary->peakDay = 0;
        summary->totalInfected = static_cast<uint64_t>(params.I0);
        summary->totalDeaths = counts.D;
        summary->daysSimulated = 0;
    }

    vector<future<void>> pending;
    auto drainOne = [&]() {
        if (!pending.empty()) {
            pending.front().get();
            pending.erase(pending.begin());
        }
    };

    for (int day = 1; day <= params.days; ++day) {
        updateDay(static_cast<uint64_t>(day));
        counts = countStates();

        if (summary) {
            const double fraction = static_cast<double>(counts.I) / current.size();
            if (fraction > summary->peakInfectedFraction) {
                summary->peakInfectedFraction = fraction;
                summary->peakDay = day;
            }
            const uint64_t initialSusceptible = current.size() -
                static_cast<uint64_t>(params.I0) -
                static_cast<uint64_t>(params.v * current.size());
            const uint64_t currentSusceptible = counts.S;
            summary->totalInfected = static_cast<uint64_t>(params.I0) +
                (initialSusceptible >= currentSusceptible ? initialSusceptible - currentSusceptible : 0);
            summary->totalDeaths = counts.D;
            summary->daysSimulated = day;
        }

        if (withSnapshots && params.snapshotEvery > 0 &&
            day % params.snapshotEvery == 0 && params.snapshotMode != "none") {
            if (params.snapshotMode == "blocking") {
                writeSnapshot(day, params.ioLatencyMs);
            } else {
                // Copy is required: the simulation swaps and overwrites its buffers.
                vector<Person> image = current;
                const int N = params.N, latency = params.ioLatencyMs;
                const string dir = params.snapshotDir;
                filesystem::create_directories(dir);
                pending.emplace_back(async(launch::async,
                    [image = move(image), N, day, latency, dir]() {
                        const string path = dir + "/day_" + to_string(day) + ".ppm";
                        ofstream out(path);
                        if (!out) throw runtime_error("Cannot write async snapshot: " + path);
                        out << "P3\n" << N << " " << N << "\n255\n";
                        for (int r = 0; r < N; ++r) {
                            for (int c = 0; c < N; ++c) {
                                State s = image[static_cast<size_t>(r) * N + c].state;
                                int rr=255, gg=255, bb=255;
                                if (s == State::I) { rr=220; gg=30; bb=30; }
                                else if (s == State::R) { rr=30; gg=160; bb=50; }
                                else if (s == State::V) { rr=30; gg=80; bb=220; }
                                else if (s == State::D) { rr=20; gg=20; bb=20; }
                                out << rr << " " << gg << " " << bb << " ";
                            }
                            out << "\n";
                        }
                        out.close();
                        if (latency > 0) this_thread::sleep_for(chrono::milliseconds(latency));
                    }));
                if (static_cast<int>(pending.size()) >= params.maxPending) drainOne();
            }
        }

        updateLockdown(counts);
        if (counts.I == 0 && !params.fixedDays) break;
    }
    while (!pending.empty()) drainOne();
}

void Simulation::run() {
    ofstream csv(params.outputFile);
    if (!csv) throw runtime_error("Cannot open output CSV: " + params.outputFile);
    csv << "day,S,I,R,V,D,beta_eff,checksum\n";
    Counts c = countStates();
    csv << "0," << c.S << "," << c.I << "," << c.R << "," << c.V << ","
        << c.D << "," << betaEff << "," << checksum() << "\n";

    vector<future<void>> pending;
    auto drainOne = [&]() {
        if (!pending.empty()) {
            pending.front().get();
            pending.erase(pending.begin());
        }
    };

    for (int day = 1; day <= params.days; ++day) {
        updateDay(static_cast<uint64_t>(day));
        c = countStates();
        csv << day << "," << c.S << "," << c.I << "," << c.R << "," << c.V
            << "," << c.D << "," << betaEff << "," << checksum() << "\n";

        if (params.snapshotEvery > 0 && day % params.snapshotEvery == 0 &&
            params.snapshotMode != "none") {
            if (params.snapshotMode == "blocking") {
                writeSnapshot(static_cast<uint64_t>(day), params.ioLatencyMs);
            } else {
                vector<Person> image = current;
                const int N = params.N, latency = params.ioLatencyMs;
                const string dir = params.snapshotDir;
                filesystem::create_directories(dir);
                pending.emplace_back(async(launch::async,
                    [image = move(image), N, day, latency, dir]() {
                        const string path = dir + "/day_" + to_string(day) + ".ppm";
                        ofstream out(path);
                        if (!out) throw runtime_error("Cannot write async snapshot: " + path);
                        out << "P3\n" << N << " " << N << "\n255\n";
                        for (int r = 0; r < N; ++r) {
                            for (int col = 0; col < N; ++col) {
                                State state = image[static_cast<size_t>(r) * N + col].state;
                                int rr=255, gg=255, bb=255;
                                if (state == State::I) { rr=220; gg=30; bb=30; }
                                else if (state == State::R) { rr=30; gg=160; bb=50; }
                                else if (state == State::V) { rr=30; gg=80; bb=220; }
                                else if (state == State::D) { rr=20; gg=20; bb=20; }
                                out << rr << " " << gg << " " << bb << " ";
                            }
                            out << "\n";
                        }
                        out.close();
                        if (latency > 0) this_thread::sleep_for(chrono::milliseconds(latency));
                    }));
                if (static_cast<int>(pending.size()) >= params.maxPending) drainOne();
            }
        }

        updateLockdown(c);
        if (c.I == 0 && !params.fixedDays) break;
    }
    while (!pending.empty()) drainOne();
    cout << "Simulation completed. CSV saved to: " << params.outputFile << "\n";
}

double Simulation::benchmarkComputation() {
    const auto start = Clock::now();
    runDays(false, false, nullptr);
    const auto stop = Clock::now();
    return chrono::duration<double>(stop - start).count();
}

double Simulation::benchmarkIO() {
    const auto start = Clock::now();
    runDays(true, false, nullptr);
    const auto stop = Clock::now();
    return chrono::duration<double>(stop - start).count();
}

RunSummary Simulation::runSummary() {
    RunSummary s;
    runDays(false, true, &s);
    return s;
}
