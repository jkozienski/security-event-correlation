#include "cli.hpp"
#include "correlation/generator.hpp"
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "generate_events [--history history.csv] [--queries queries.csv] [--count 1000]\n"
                         "  [--query-count 10] [--features 4] [--seed 1337] [--start 1700000000] [--step 1]\n";
            return 0;
        }
        const auto args = cli::parse(argc, argv, {{"--history", "history.csv"}, {"--queries", "queries.csv"},
            {"--count", "1000"}, {"--query-count", "10"}, {"--features", "4"}, {"--seed", "1337"},
            {"--start", "1700000000"}, {"--step", "1"}});
        correlation::GeneratorConfig c;
        c.history_count = cli::number<std::size_t>(args.at("--count"));
        c.query_count = cli::number<std::size_t>(args.at("--query-count"));
        c.feature_count = cli::number<std::size_t>(args.at("--features"));
        c.seed = cli::number<std::uint32_t>(args.at("--seed"));
        c.start_timestamp = cli::number<std::uint64_t>(args.at("--start"));
        c.step_seconds = cli::number<std::uint64_t>(args.at("--step"));
        const auto hpath = std::filesystem::weakly_canonical(args.at("--history"));
        const auto qpath = std::filesystem::weakly_canonical(args.at("--queries"));
        if (hpath == qpath || (std::filesystem::exists(hpath) && std::filesystem::exists(qpath) &&
                             std::filesystem::equivalent(hpath, qpath)))
            throw std::invalid_argument("History and queries require different output files");
        const auto data = correlation::generate_dataset(c);
        std::ofstream history(hpath), queries(qpath);
        if (!history || !queries) throw std::runtime_error("Cannot open output files");
        correlation::write_events_csv(history, data.history);
        correlation::write_events_csv(queries, data.queries);
        history.close(); queries.close();
        if (!history || !queries) throw std::runtime_error("Cannot close output files");
        std::cout << "Generated " << data.history.size() << " history events and " << data.queries.size() << " queries\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
