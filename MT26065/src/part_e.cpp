#include "simulation.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

static const int N = 500;
static const int DAYS = 5000;
static const int SEEDS = 10;
static const int INITIAL_INFECTED = 10;

struct Setting {
    string experiment;
    string name;
    double beta;
    double vaccination;
    double maskMultiplier;
    double lockdownMultiplier;
};

static void runOne(
    const Setting& setting,
    int seed,
    ofstream& raw
) {
    Parameters p;

    p.N = N;
    p.days = DAYS;
    p.seed = seed;

    p.beta = setting.beta;
    p.d = 7;
    p.mu = 0.01;
    p.I0 = INITIAL_INFECTED;

    p.v = setting.vaccination;
    p.m = setting.maskMultiplier;
    p.lambda = setting.lockdownMultiplier;

    p.thetaOn = 0.02;
    p.thetaOff = 0.005;

    // Part E experiments are run sequentially.
    p.threads = 1;
    p.chunks = 1;

    // No snapshots are needed for epidemiological analysis.
    p.snapshotEvery = 0;
    p.snapshotMode = "none";

    // Stop when the epidemic ends.
    p.fixedDays = false;

    string tempFile =
        "results/partE/temp_" +
        setting.experiment + "_" +
        setting.name + "_" +
        to_string(seed) + ".csv";

    p.outputFile = tempFile;

    cout << "Running "
         << setting.experiment << " / "
         << setting.name << " / seed "
         << seed << endl;

    Simulation simulation(p);
    simulation.run();

    ifstream input(tempFile);

    if (!input) {
        throw runtime_error("Cannot read temporary output: " + tempFile);
    }

    string line;

    // Skip the individual simulation CSV header.
    getline(input, line);

    while (getline(input, line)) {
        if (line.empty()) continue;

        raw << setting.experiment << ","
            << setting.name << ","
            << seed << ","
            << line << "\n";
    }

    input.close();
    fs::remove(tempFile);
}

static void runSetting(
    const Setting& setting,
    ofstream& raw
) {
    for (int i = 0; i < SEEDS; ++i) {
        // Use the same seed set across settings for fair comparisons.
        int seed = 1001 + i;
        runOne(setting, seed, raw);
    }
}

int main() {
    try {
        fs::create_directories("results/partE");

        // Start with a fresh combined output file.
        const string rawFile = "results/partE/raw_timeseries.csv";
        ofstream raw(rawFile);

        if (!raw) {
            throw runtime_error("Cannot create raw_timeseries.csv");
        }

        raw << "experiment,setting,seed,day,S,I,R,V,D,beta_eff,checksum\n";

        cout << "\n====================================\n";
        cout << "       PART E: EPIDEMIOLOGY\n";
        cout << "====================================\n";
        cout << "Grid size       : " << N << " x " << N << "\n";
        cout << "Seeds/setting   : " << SEEDS << "\n";
        cout << "Maximum days    : " << DAYS << "\n";
        cout << "Initial infected: " << INITIAL_INFECTED << "\n";
        cout << "====================================\n\n";

        // -------------------------------------------------
        // E1. Default epidemic curves
        // -------------------------------------------------

        cout << "\nE1: DEFAULT PARAMETERS\n";

        Setting defaultSetting = {
            "E1", "default",
            0.08, 0.0, 1.0, 1.0
        };

        runSetting(defaultSetting, raw);

        // -------------------------------------------------
        // E2. Transmission probability beta sweep
        // -------------------------------------------------

        cout << "\nE2: BETA SWEEP\n";

        vector<double> betaValues = {
            0.01, 0.02, 0.03, 0.04, 0.05,
            0.06, 0.08, 0.10, 0.12, 0.16
        };

        for (double beta : betaValues) {
            ostringstream name;
            name << fixed << setprecision(3) << "beta_" << beta;

            Setting setting = {
                "E2", name.str(),
                beta, 0.0, 1.0, 1.0
            };

            runSetting(setting, raw);
        }

        // -------------------------------------------------
        // E3. Vaccination sweep
        // -------------------------------------------------

        cout << "\nE3: VACCINATION SWEEP\n";

        vector<double> vaccinationValues = {
            0.0, 0.1, 0.2, 0.3, 0.4,
            0.5, 0.6, 0.7, 0.8, 0.9
        };

        for (double v : vaccinationValues) {
            ostringstream name;
            name << fixed << setprecision(3) << "v_" << v;

            Setting setting = {
                "E3", name.str(),
                0.08, v, 1.0, 1.0
            };

            runSetting(setting, raw);
        }

        // -------------------------------------------------
        // E4. Masks and lockdown
        // -------------------------------------------------

        cout << "\nE4: MASKS AND LOCKDOWN\n";

        vector<Setting> interventionSettings = {
            {
                "E4", "baseline",
                0.08, 0.0, 1.0, 1.0
            },
            {
                "E4", "masks",
                0.08, 0.0, 0.5, 1.0
            },
            {
                "E4", "lockdown",
                0.08, 0.0, 1.0, 0.3
            },
            {
                "E4", "masks_lockdown",
                0.08, 0.0, 0.5, 0.3
            }
        };

        for (const auto& setting : interventionSettings) {
            runSetting(setting, raw);
        }

        raw.close();

        cout << "\n====================================\n";
        cout << "PART E SIMULATIONS COMPLETED\n";
        cout << "Output: " << rawFile << "\n";
        cout << "====================================\n";

    } catch (const exception& e) {
        cerr << "Part E error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
