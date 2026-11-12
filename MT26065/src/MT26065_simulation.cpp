
#include "MT26065_simulation.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <limits>

using namespace std;

// SplitMix64 deterministic hash
static uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Constructor
Simulation::Simulation(const Parameters& p)
    : params(p), betaEff(p.beta * p.m) {

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

    uint64_t total = static_cast<uint64_t>(p.N) * p.N;

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

// Deterministic random hash
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

// Convert hash to [0,1)
double Simulation::random01(
    uint64_t day,
    uint64_t cell,
    uint64_t stream
) const {

    return static_cast<double>(hashRandom(day, cell, stream) >> 11)
           * (1.0 / 9007199254740992.0);
}

// Initialize population
void Simulation::initialize() {

    uint64_t total = current.size();

    for (auto& person : current) {
        person.state = State::S;
        person.infectedDays = 0;
    }

    // Generate deterministic random ordering
    vector<pair<uint64_t, uint64_t>> order;
    order.reserve(total);

    for (uint64_t i = 0; i < total; i++) {
        order.push_back({
            hashRandom(0, i, 2),
            i
        });
    }

    sort(order.begin(), order.end());

    // Vaccinate a fraction v of the population
    uint64_t vaccinated =
        static_cast<uint64_t>(params.v * total);

    for (uint64_t i = 0; i < vaccinated; i++) {
        current[order[i].second].state = State::V;
    }

    // Infect I0 randomly selected susceptible people
    uint64_t infected = 0;

    for (const auto& item : order) {

        if (infected == static_cast<uint64_t>(params.I0)) {
            break;
        }

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

// Count states
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

// Calculate grid checksum
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

// Count infected neighbours
int Simulation::infectedNeighbours(int row, int col) const {

    int count = 0;

    for (int dr = -1; dr <= 1; dr++) {

        for (int dc = -1; dc <= 1; dc++) {

            if (dr == 0 && dc == 0)
                continue;

            int nr = row + dr;
            int nc = col + dc;

            // Grid boundaries
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

// Update one cell
void Simulation::updateCell(
    int row,
    int col,
    uint64_t day
) {

    uint64_t id =
        static_cast<uint64_t>(row) * params.N + col;

    const Person& person = current[id];
    Person& result = next[id];

    // Susceptible
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

    // Infected
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

    // Recovered, vaccinated or dead
    else {
        result = person;
    }
}

// Update entire grid for one day
void Simulation::updateDay(uint64_t day) {

    for (int i = 0; i < params.N; i++) {

        for (int j = 0; j < params.N; j++) {
            updateCell(i, j, day);
        }
    }

    // Swap only after all cells have been updated
    current.swap(next);
}

// Main simulation
void Simulation::run() {

    ofstream csv(params.outputFile);

    if (!csv.is_open()) {
        throw runtime_error("Cannot open output CSV file");
    }

    csv << "day,S,I,R,V,D,beta_eff,checksum\n";

    Counts counts = countStates();

    // Day 0
    csv << 0 << ","
        << counts.S << ","
        << counts.I << ","
        << counts.R << ","
        << counts.V << ","
        << counts.D << ","
        << betaEff << ","
        << checksum() << "\n";

    cout << "Day 0: I = " << counts.I << "\n";

    for (int day = 1; day <= params.days; day++) {

        updateDay(day);

        counts = countStates();

        // Record the beta used for this day
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

        // Calculate infected fraction
        double infectedFraction =
            static_cast<double>(counts.I) / current.size();

        // Lockdown decision applies from next day
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

        // Stop if epidemic ends
        if (counts.I == 0 && !params.fixedDays) {
            break;
        }
    }

    csv.close();

    cout << "\nSimulation completed.\n";
    cout << "CSV saved to: " << params.outputFile << "\n";
}

