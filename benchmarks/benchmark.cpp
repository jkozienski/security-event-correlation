// Benchmark of the batch search backends (sequential reference and OpenMP).
// Output: one CSV row per measurement (raw data) and a metadata file with the setup.
#include "cli.hpp"
#include "correlation/generator.hpp"
#include "correlation/search.hpp"
#include "correlation/sequential.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
using Results = std::vector<std::vector<correlation::Match>>;

double seconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

std::vector<int> parse_threads(const std::string& text) {
    std::vector<int> threads;
    std::istringstream stream(text);
    for (std::string part; std::getline(stream, part, ',');) {
        const int value = cli::number<int>(part);
        if (value < 1) throw std::invalid_argument("--threads values must be >= 1");
        threads.push_back(value);
    }
    if (threads.empty()) throw std::invalid_argument("--threads must not be empty");
    return threads;
}

// Exact comparison: the backends are expected to be bitwise identical to the reference.
bool same_results(const Results& a, const Results& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size()) return false;
        for (std::size_t j = 0; j < a[i].size(); ++j)
            if (a[i][j].event_id != b[i][j].event_id || a[i][j].timestamp != b[i][j].timestamp ||
                a[i][j].score != b[i][j].score) return false;
    }
    return true;
}

struct Config {
    std::string backend;
    int threads;
    correlation::BatchSearchFunction search;
};

struct Row {
    double read_s = 0, validate_s = 0, core_s = 0, write_s = 0;
    bool equal = true;
    std::size_t match_count = 0;
};

// One measurement. Timed parts (same process, in-memory streams, so the numbers do not
// depend on disk speed):
//   read_s     - parsing history and queries from CSV text
//   validate_s - validate_batch alone (constant O(N) cost, also run inside the core call)
//   core_s     - one backend call over the whole query set (includes its own validation)
//   write_s    - writing all matches as CSV
// total_s = read_s + core_s + write_s (validate_s is already inside core_s).
Row measure(const Config& config, const std::string& history_csv, const std::string& queries_csv,
            const correlation::SearchConfig& search_config, const Results* reference,
            Results* keep) {
    Row row;
    std::istringstream history_in(history_csv), queries_in(queries_csv);
    const auto t0 = Clock::now();
    const auto history = correlation::read_events_csv(history_in);
    const auto queries = correlation::read_events_csv(queries_in);
    const auto t1 = Clock::now();
    row.read_s = seconds(t0, t1);

    correlation::validate_batch(history, queries, search_config);
    const auto t2 = Clock::now();
    row.validate_s = seconds(t1, t2);

    correlation::RunOptions options;
    options.threads = config.threads;
    auto results = config.search(history, queries, search_config, options);
    const auto t3 = Clock::now();
    row.core_s = seconds(t2, t3);

    std::ostringstream out;
    correlation::write_matches_csv(out, 0, {});
    for (std::size_t i = 0; i < queries.size(); ++i)
        correlation::write_matches_csv(out, queries[i].id, results[i], false);
    row.write_s = seconds(t3, Clock::now());

    for (const auto& r : results) row.match_count += r.size();
    row.equal = reference ? same_results(results, *reference) : true;
    if (keep) *keep = std::move(results);
    return row;
}

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "(unset)";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "benchmark [--history 100000] [--queries 1000] [--features 4] [--seed 1337]\n"
                         "  [--mode top-k|threshold] [--k 10] [--threshold 0.8] [--window 1000000000]\n"
                         "  [--threads 1,2,4,8] (OpenMP thread counts) [--repeats 7] [--warmup 1]\n"
                         "  [--output benchmark_results.csv] [--note \"CPU, system, ...\"]\n"
                         "Measures the sequential reference and the OpenMP backend (if built) on the same\n"
                         "generated data. Writes raw rows to --output and the setup to <output>.meta.txt.\n";
            return 0;
        }
        const auto args = cli::parse(argc, argv, {{"--history", "100000"}, {"--queries", "1000"},
            {"--features", "4"}, {"--seed", "1337"}, {"--mode", "top-k"}, {"--k", "10"},
            {"--threshold", "0.8"}, {"--window", "1000000000"}, {"--threads", "1,2,4,8"},
            {"--repeats", "7"}, {"--warmup", "1"}, {"--output", "benchmark_results.csv"},
            {"--note", ""}});

        correlation::GeneratorConfig generator;
        generator.history_count = cli::number<std::size_t>(args.at("--history"));
        generator.query_count = cli::number<std::size_t>(args.at("--queries"));
        generator.feature_count = cli::number<std::size_t>(args.at("--features"));
        generator.seed = cli::number<std::uint32_t>(args.at("--seed"));
        correlation::SearchConfig search_config;
        const auto mode = args.at("--mode");
        if (mode != "top-k" && mode != "threshold") throw std::invalid_argument("Mode must be top-k or threshold");
        search_config.mode = mode == "top-k" ? correlation::SearchMode::TopK : correlation::SearchMode::Threshold;
        search_config.k = cli::number<std::size_t>(args.at("--k"));
        search_config.threshold = cli::real(args.at("--threshold"));
        search_config.window_seconds = cli::number<std::uint64_t>(args.at("--window"));
        const auto repeats = cli::number<int>(args.at("--repeats"));
        const auto warmup = cli::number<int>(args.at("--warmup"));
        if (repeats < 5) throw std::invalid_argument("--repeats must be at least 5");
        if (warmup < 1) throw std::invalid_argument("--warmup must be at least 1");
        const auto thread_list = parse_threads(args.at("--threads"));

        std::vector<Config> configs;
        configs.push_back({"sequential", 1, correlation::resolve_backend(correlation::Backend::Sequential)});
        try {
            const auto openmp = correlation::resolve_backend(correlation::Backend::OpenMP);
            for (int t : thread_list) configs.push_back({"openmp", t, openmp});
        } catch (const std::exception& e) {
            std::cerr << "OpenMP backend skipped: " << e.what() << '\n';
        }

        // Data is generated once and serialized once; every measurement parses it again.
        const auto data = correlation::generate_dataset(generator);
        std::ostringstream history_out, queries_out;
        correlation::write_events_csv(history_out, data.history);
        correlation::write_events_csv(queries_out, data.queries);
        const auto history_csv = history_out.str(), queries_csv = queries_out.str();

        Results reference;
        measure(configs.front(), history_csv, queries_csv, search_config, nullptr, &reference);
        for (const auto& c : configs)  // warm-up: caches, thread pool, page faults
            for (int w = 0; w < warmup; ++w)
                measure(c, history_csv, queries_csv, search_config, &reference, nullptr);

        std::ofstream csv(args.at("--output"));
        if (!csv) throw std::runtime_error("Cannot open output file");
        csv.precision(9);
        csv << "backend,threads,repeat,history,queries,features,seed,mode,k,threshold,window,"
               "read_s,validate_s,core_s,write_s,total_s,matches,equal_to_reference\n";
        bool all_equal = true;
        // Interleaved order (repeat outer, configuration inner) spreads drift (thermal, other
        // load) over all configurations instead of biasing the one measured last.
        for (int r = 1; r <= repeats; ++r) {
            for (const auto& c : configs) {
                const auto row = measure(c, history_csv, queries_csv, search_config, &reference, nullptr);
                all_equal = all_equal && row.equal;
                csv << c.backend << ',' << c.threads << ',' << r << ',' << data.history.size() << ','
                    << data.queries.size() << ',' << generator.feature_count << ',' << generator.seed << ','
                    << mode << ',' << search_config.k << ',' << search_config.threshold << ','
                    << search_config.window_seconds << ',' << row.read_s << ',' << row.validate_s << ','
                    << row.core_s << ',' << row.write_s << ',' << row.read_s + row.core_s + row.write_s << ','
                    << row.match_count << ',' << (row.equal ? 1 : 0) << '\n';
                std::cerr << c.backend << " threads=" << c.threads << " repeat=" << r
                          << " core_s=" << row.core_s << (row.equal ? "" : "  MISMATCH") << '\n';
            }
        }
        csv.flush();
        if (!csv) throw std::runtime_error("Failed writing results");

        std::ofstream meta(args.at("--output") + ".meta.txt");
        meta << "compiler=";
#if defined(__VERSION__)
        meta << __VERSION__;
#elif defined(_MSC_VER)
        meta << "MSVC " << _MSC_VER;
#else
        meta << "unknown";
#endif
        meta << "\nbuild_type=" << BENCH_BUILD_TYPE;
#ifdef _OPENMP
        meta << "\nopenmp_version_macro=" << _OPENMP;
#else
        meta << "\nopenmp_version_macro=(not compiled with OpenMP in this file)";
#endif
#if defined(_WIN32)
        meta << "\nos=Windows";
#elif defined(__linux__)
        meta << "\nos=Linux";
#else
        meta << "\nos=other";
#endif
        meta << "\nhardware_concurrency=" << std::thread::hardware_concurrency()
             << "\nOMP_NUM_THREADS=" << env("OMP_NUM_THREADS") << "\nOMP_PROC_BIND=" << env("OMP_PROC_BIND")
             << "\nOMP_PLACES=" << env("OMP_PLACES") << "\nOMP_SCHEDULE=" << env("OMP_SCHEDULE")
             << "\nhistory=" << data.history.size() << "\nqueries=" << data.queries.size()
             << "\nfeatures=" << generator.feature_count << "\nseed=" << generator.seed
             << "\nmode=" << mode << "\nk=" << search_config.k << "\nthreshold=" << search_config.threshold
             << "\nwindow_seconds=" << search_config.window_seconds << "\nrepeats=" << repeats
             << "\nwarmup=" << warmup << "\nthreads=" << args.at("--threads")
             << "\nnote=" << args.at("--note") << '\n'
             << "# Times use std::chrono::steady_clock and in-memory CSV streams.\n"
             << "# core_s = one backend call for the whole query set; it includes the backend's own\n"
             << "#   validation of the history (constant O(N) cost, the same for every backend).\n"
             << "# validate_s = validate_batch alone, measured separately (already inside core_s).\n"
             << "# total_s = read_s + core_s + write_s.\n";
        std::cerr << (all_equal ? "All results identical to the sequential reference.\n"
                                : "WARNING: some results differ from the sequential reference!\n");
        return all_equal ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
