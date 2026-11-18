#include "cli.hpp"
#include "correlation/search.hpp"
#include "correlation/sequential.hpp"
#ifdef CORRELATION_HAS_MPI
#include "correlation/mpi.hpp"
#include <optional>
#endif
#include <chrono>
#include <fstream>

int main(int argc, char** argv) {
    int rank = 0;
    int processes = 1;
#ifdef CORRELATION_HAS_MPI
    std::optional<correlation::MpiSession> mpi;
#endif
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "main [--history history.csv] [--queries queries.csv]\n"
                         "  [--backend sequential|openmp|mpi|opencl] (default: sequential)\n"
                         "  [--threads 0] (openmp: 0 = OpenMP default, e.g. OMP_NUM_THREADS)\n"
                         "  [--mode top-k|threshold] [--k 10] [--threshold 0.8] [--window 3600]\n"
                         "  [--weight-operation 1] [--weight-device 1] [--weight-account 1] [--weight-source 1] [--weight-destination 1] [--weight-numeric 1]\n"
                         "  [--timing 0|1] (1: print core and total time to stderr)\n"
                         "OpenMP and MPI require build support. OpenCL is planned.\n"
                         "MPI example: mpiexec -n 4 main --backend mpi [options]\n"
                         "Results: CSV on stdout, in query order. Query events are not added to history.\n";
            return 0;
        }
        const auto args = cli::parse(argc, argv, {{"--history", "history.csv"}, {"--queries", "queries.csv"},
            {"--backend", "sequential"}, {"--threads", "0"}, {"--timing", "0"},
            {"--mode", "top-k"}, {"--k", "10"}, {"--threshold", "0.8"}, {"--window", "3600"},
            {"--weight-operation", "1"}, {"--weight-device", "1"}, {"--weight-account", "1"}, {"--weight-source", "1"}, {"--weight-destination", "1"}, {"--weight-numeric", "1"}});

        using Clock = std::chrono::steady_clock;
        const auto total_start = Clock::now();
        const auto backend = correlation::parse_backend(args.at("--backend"));
        const auto search = correlation::resolve_backend(backend);
#ifdef CORRELATION_HAS_MPI
        if (backend == correlation::Backend::MPI) {
            mpi.emplace(argc, argv);
            rank = mpi->rank();
            processes = mpi->size();
        }
#endif
        const auto on_root = [&](auto action) {
#ifdef CORRELATION_HAS_MPI
            if (mpi) {
                mpi->on_root(action);
                return;
            }
#endif
            action();
        };
        correlation::SearchConfig c;
        const auto mode = args.at("--mode");
        if (mode != "top-k" && mode != "threshold") throw std::invalid_argument("Mode must be top-k or threshold");
        c.mode = mode == "top-k" ? correlation::SearchMode::TopK : correlation::SearchMode::Threshold;
        c.k = cli::number<std::size_t>(args.at("--k"));
        c.threshold = cli::real(args.at("--threshold"));
        c.window_seconds = cli::number<std::uint64_t>(args.at("--window"));
        c.weights = {cli::real(args.at("--weight-operation")), cli::real(args.at("--weight-device")),
                     cli::real(args.at("--weight-account")), cli::real(args.at("--weight-source")),
                     cli::real(args.at("--weight-destination")), cli::real(args.at("--weight-numeric"))};
        correlation::RunOptions options;
        options.threads = cli::number<int>(args.at("--threads"));
        if (options.threads < 0) throw std::invalid_argument("--threads must not be negative");
        const auto timing = args.at("--timing");
        if (timing != "0" && timing != "1") throw std::invalid_argument("--timing must be 0 or 1");

        std::vector<correlation::Event> history, queries;
        on_root([&] {
            std::ifstream hfile(args.at("--history")), qfile(args.at("--queries"));
            if (!hfile || !qfile) throw std::runtime_error("Cannot open input files");
            history = correlation::read_events_csv(hfile);
            queries = correlation::read_events_csv(qfile);
            if (backend != correlation::Backend::MPI)
                correlation::validate_batch(history, queries, c);
        });

        const auto core_start = Clock::now();
        const auto results = search(history, queries, c, options);
        const auto core_end = Clock::now();

        on_root([&] {
            correlation::write_matches_csv(std::cout, 0, {});
            for (std::size_t i = 0; i < queries.size(); ++i)
                correlation::write_matches_csv(std::cout, queries[i].id, results[i], false);
            std::cout.flush();
            if (!std::cout) throw std::runtime_error("Failed writing results");
        });
        if (rank == 0 && timing == "1") {
            const auto seconds = [](auto a, auto b) { return std::chrono::duration<double>(b - a).count(); };
            std::cerr << "backend=" << args.at("--backend") << " threads_requested=" << options.threads;
            if (backend == correlation::Backend::MPI) std::cerr << " processes=" << processes;
            std::cerr << " queries=" << queries.size() << " history=" << history.size()
                      << " core_seconds=" << seconds(core_start, core_end)
                      << " total_seconds=" << seconds(total_start, Clock::now()) << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        if (rank == 0) std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
