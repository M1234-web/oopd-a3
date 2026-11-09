
#include "simulation.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std;
using Clock = chrono::steady_clock;

// One independent simulation task.
struct SweepTask {
    int id;
    Parameters params;
};

// Result returned by a completed simulation.
struct SweepResult {
    int id;
    Parameters params;
    RunSummary summary;
    bool success = true;
    string error;
};

int main(int argc, char* argv[]) {
    try {
        // Command-line arguments:
        // ./sweep [time_limit_seconds] [worker_count]
        int timeLimitSeconds = 10;
        int workerCount = 4;

        if (argc >= 2) {
            timeLimitSeconds = stoi(argv[1]);
        }

        if (argc >= 3) {
            workerCount = stoi(argv[2]);
        }

        if (timeLimitSeconds <= 0 || workerCount <= 0) {
            throw invalid_argument(
                "Time limit and worker count must be positive."
            );
        }

        // Create output directory.
        filesystem::create_directories("results/partC2");

        // Parameter values for the sweep.
        const vector<double> betaValues = {0.2, 0.3, 0.4};
        const vector<double> vaccinationValues = {0.0, 0.2, 0.4};
        const vector<uint64_t> seeds = {42, 100, 200};

        // Generate all independent tasks in submission order.
        deque<SweepTask> taskQueue;

        int nextId = 1;

        for (double beta : betaValues) {
            for (double v : vaccinationValues) {
                for (uint64_t seed : seeds) {
                    Parameters p;

                    p.beta = beta;
                    p.v = v;
                    p.seed = seed;

                    // Disable snapshot generation for parameter sweeps.
                    p.snapshotEvery = 0;
                    p.asyncIO = false;

                    taskQueue.push_back({nextId++, p});
                }
            }
        }

        const int totalRuns = static_cast<int>(taskQueue.size());

        // Output CSV files.
        ofstream output("results/partC2/sweep.csv");
        ofstream missingOutput("results/partC2/missing_runs.csv");

        if (!output.is_open() || !missingOutput.is_open()) {
            throw runtime_error("Could not open Part C2 output files.");
        }

        output
            << "id,beta,v,seed,"
            << "peak_infected_fraction,peak_day,"
            << "total_infected,total_deaths,days_simulated,status\n";

        missingOutput << "id,beta,v,seed\n";

        output << fixed << setprecision(6);

        // Shared data structures.
        deque<SweepResult> completedQueue;

        mutex mtx;
        condition_variable cv;

        bool stopStartingTasks = false;
        int workersAlive = workerCount;

        // The deadline is shared with all worker threads.
        const auto deadline =
            Clock::now() + chrono::seconds(timeLimitSeconds);

        // Record the IDs in submission order.
        vector<int> submissionOrder;
        for (const auto& task : taskQueue) {
            submissionOrder.push_back(task.id);
        }

        // Record the IDs in completion order.
        vector<int> completionOrder;

        cout << "======================================\n";
        cout << " PART C2: ASYNCHRONOUS PARAMETER SWEEP\n";
        cout << "======================================\n";
        cout << "Total simulations : " << totalRuns << '\n';
        cout << "Worker threads    : " << workerCount << '\n';
        cout << "Time limit        : " << timeLimitSeconds << " seconds\n";
        cout << "======================================\n";

        // Worker function.
        auto workerFunction = [&]() {
            while (true) {
                SweepTask task;

                {
                    unique_lock<mutex> lock(mtx);

                    cv.wait(lock, [&]() {
                        return stopStartingTasks || !taskQueue.empty();
                    });

                    // Do not start another task after the deadline.
                    if (stopStartingTasks || Clock::now() >= deadline) {
                        stopStartingTasks = true;
                        cv.notify_all();
                        break;
                    }

                    task = taskQueue.front();
                    taskQueue.pop_front();
                }

                // Run the simulation outside the mutex.
                SweepResult result;
                result.id = task.id;
                result.params = task.params;

                try {
                    Simulation simulation(task.params);
                    result.summary = simulation.runSummary();
                    result.success = true;
                }
                catch (const exception& e) {
                    result.success = false;
                    result.error = e.what();
                }
                catch (...) {
                    result.success = false;
                    result.error = "Unknown simulation error";
                }

                // Publish the result as soon as this simulation finishes.
                {
                    lock_guard<mutex> lock(mtx);
                    completedQueue.push_back(std::move(result));
                }

                cv.notify_all();
            }

            // This worker is now finished.
            {
                lock_guard<mutex> lock(mtx);
                workersAlive--;
            }

            cv.notify_all();
        };

        // Start worker threads.
        vector<thread> workers;

        for (int i = 0; i < workerCount; i++) {
            workers.emplace_back(workerFunction);
        }

        int completed = 0;
        bool deadlineReported = false;

        // Main thread consumes results in completion order.
        while (true) {
            SweepResult result;
            bool hasResult = false;
            vector<SweepTask> missingTasks;

            {
                unique_lock<mutex> lock(mtx);

                if (!stopStartingTasks) {
                    // Wait for a result or until the deadline.
                    cv.wait_until(lock, deadline, [&]() {
                        return !completedQueue.empty()
                            || stopStartingTasks;
                    });

                    // Time limit reached: stop accepting new tasks.
                    if (!stopStartingTasks && Clock::now() >= deadline) {
                        stopStartingTasks = true;
                    }

                    // Save all tasks that have not started.
                    if (stopStartingTasks && !deadlineReported) {
                        while (!taskQueue.empty()) {
                            missingTasks.push_back(taskQueue.front());
                            taskQueue.pop_front();
                        }

                        deadlineReported = true;
                    }
                }
                else {
                    // After the deadline, wait efficiently for running tasks.
                    cv.wait(lock, [&]() {
                        return !completedQueue.empty()
                            || workersAlive == 0;
                    });
                }

                if (!completedQueue.empty()) {
                    result = std::move(completedQueue.front());
                    completedQueue.pop_front();
                    hasResult = true;
                }

                // All workers have exited and no results remain.
                if (!hasResult &&
                    workersAlive == 0 &&
                    completedQueue.empty()) {
                    break;
                }
            }

            // Wake workers if the deadline caused them to stop.
            if (deadlineReported) {
                cv.notify_all();
            }

            // Write tasks that never started.
            for (const auto& task : missingTasks) {
                missingOutput
                    << task.id << ','
                    << task.params.beta << ','
                    << task.params.v << ','
                    << task.params.seed << '\n';

                cout << "[Not started] Run " << task.id
                     << " (deadline reached)\n";
            }

            // Process a completed simulation.
            if (hasResult) {
                completed++;
                completionOrder.push_back(result.id);

                if (result.success) {
                    const RunSummary& s = result.summary;

                    output
                        << result.id << ','
                        << result.params.beta << ','
                        << result.params.v << ','
                        << result.params.seed << ','
                        << s.peakInfectedFraction << ','
                        << s.peakDay << ','
                        << s.totalInfected << ','
                        << s.totalDeaths << ','
                        << s.daysSimulated << ','
                        << "completed\n";

                    cout << "[Completed] Run " << result.id
                         << " | beta=" << result.params.beta
                         << " | v=" << result.params.v
                         << " | seed=" << result.params.seed
                         << " | peak=" << s.peakInfectedFraction
                         << " | peak day=" << s.peakDay
                         << " | total infected=" << s.totalInfected
                         << " | deaths=" << s.totalDeaths
                         << " | days=" << s.daysSimulated
                         << '\n';
                }
                else {
                    output
                        << result.id << ','
                        << result.params.beta << ','
                        << result.params.v << ','
                        << result.params.seed
                        << ",,,,,error\n";

                    cerr << "[Failed] Run " << result.id
                         << ": " << result.error << '\n';
                }

                cout << "Progress: " << completed
                     << "/" << totalRuns << " completed\n";
            }
        }

        // Wait for every worker thread to exit.
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        output.close();
        missingOutput.close();

        const int missingCount = totalRuns - completed;

        // Compare submission order and completion order.
        bool sameOrder = (completionOrder == submissionOrder);

        cout << "\n======================================\n";
        cout << " PARAMETER SWEEP SUMMARY\n";
        cout << "======================================\n";

        cout << "Total simulations : " << totalRuns << '\n';
        cout << "Completed         : " << completed << '\n';
        cout << "Not started       : " << missingCount << '\n';

        cout << "\nSubmission order:\n";
        for (int id : submissionOrder) {
            cout << id << ' ';
        }

        cout << "\n\nCompletion order:\n";
        if (completionOrder.empty()) {
            cout << "No simulations completed.";
        }
        else {
            for (int id : completionOrder) {
                cout << id << ' ';
            }
        }

        cout << "\n\n";

        if (sameOrder) {
            cout << "Completion order matches submission order.\n";
        }
        else {
            cout << "Completion order differs from submission order.\n";
        }

        cout << "\nResults saved to:\n";
        cout << "  results/partC2/sweep.csv\n";
        cout << "  results/partC2/missing_runs.csv\n";

        cout << "======================================\n";

        return 0;
    }
    catch (const exception& e) {
        cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
