#include "cli.hpp"
#include "correlation/search.hpp"
#include <fstream>

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "main [--history history.csv] [--queries queries.csv]\n"
                         "  [--backend sequential|openmp|mpi|opencl] (default: sequential)\n"
                         "  [--mode top-k|threshold] [--k 10] [--threshold 0.8] [--window 3600]\n"
                         "  [--weight-operation 1] [--weight-device 1] [--weight-account 1] [--weight-source 1] [--weight-destination 1] [--weight-numeric 1]\n"
                         "Available backend: sequential. Other backends are not yet implemented.\n"
                         "Results: CSV on stdout. Query events are not added to history.\n";
            return 0;
        }
        const auto args = cli::parse(argc, argv, {{"--history", "history.csv"}, {"--queries", "queries.csv"},
            {"--backend", "sequential"},
            {"--mode", "top-k"}, {"--k", "10"}, {"--threshold", "0.8"}, {"--window", "3600"},
            {"--weight-operation", "1"}, {"--weight-device", "1"}, {"--weight-account", "1"}, {"--weight-source", "1"}, {"--weight-destination", "1"}, {"--weight-numeric", "1"}});



        const auto search = correlation::resolve_backend(correlation::parse_backend(args.at("--backend")));
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

        std::ifstream hfile(args.at("--history")), qfile(args.at("--queries"));
        if (!hfile || !qfile) throw std::runtime_error("Cannot open input files");
        const auto history = correlation::read_events_csv(hfile);
        const auto queries = correlation::read_events_csv(qfile);
        const auto dimension = !history.empty() ? history.front().features.size() :
                               (!queries.empty() ? queries.front().features.size() : 0);
        
        correlation::validate_config(c, dimension);
        for (const auto& query : queries) correlation::validate_event(query, dimension);

        correlation::write_matches_csv(std::cout, 0, {});
        for (const auto& query : queries)
            correlation::write_matches_csv(std::cout, query.id, search(history, query, c), false);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
