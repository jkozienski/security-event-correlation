#include "correlation/common.hpp"
#include <charconv>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace correlation {
namespace {
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> cells;
    std::size_t begin = 0;
    do {
        const auto end = line.find(',', begin);
        cells.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (true);
    return cells;
}
template<class T> T integer(const std::string& text) {
    T value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid unsigned integer: " + text);
    return value;
}
double real(const std::string& text) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double value;
    if (!(stream >> std::noskipws >> value) || stream.peek() != std::char_traits<char>::eof())
        throw std::invalid_argument("Invalid feature: " + text);
    return value;
}
}
std::vector<Event> read_events_csv(std::istream& input) {
    std::string line;
    if (!std::getline(input, line)) throw std::invalid_argument("Missing CSV header");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto header = split(line);
    const std::vector<std::string> base{"id", "timestamp", "operation_id", "device_id", "account_id", "source_id", "destination_id"};
    if (header.size() < base.size()) throw std::invalid_argument("Invalid CSV header");
    for (std::size_t i = 0; i < header.size(); ++i) {
        const auto expected = i < base.size() ? base[i] : "f" + std::to_string(i - base.size());
        if (header[i] != expected) throw std::invalid_argument("Expected CSV column: " + expected);
    }
    std::vector<Event> events;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        try {
            const auto cells = split(line);
            if (cells.size() != header.size()) throw std::invalid_argument("Wrong column count");
            Event e;
            e.id = integer<std::uint64_t>(cells[0]);
            e.timestamp = integer<std::uint64_t>(cells[1]);
            e.operation_id = integer<std::uint32_t>(cells[2]);
            e.device_id = integer<std::uint32_t>(cells[3]);
            e.account_id = integer<std::uint32_t>(cells[4]);
            e.source_id = integer<std::uint32_t>(cells[5]);
            e.destination_id = integer<std::uint32_t>(cells[6]);
            for (std::size_t i = base.size(); i < cells.size(); ++i) e.features.push_back(real(cells[i]));
            events.push_back(std::move(e));
        } catch (const std::exception& error) {
            throw std::invalid_argument("CSV line " + std::to_string(line_number) + ": " + error.what());
        }
    }
    if (input.bad() || (!input.eof() && input.fail())) throw std::runtime_error("Failed reading CSV");
    validate_events(events);
    return events;
}
void write_events_csv(std::ostream& output, const std::vector<Event>& events) {
    validate_events(events);
    std::ostringstream buffer;
    buffer.imbue(std::locale::classic());
    buffer << std::setprecision(std::numeric_limits<double>::max_digits10);
    buffer << "id,timestamp,operation_id,device_id,account_id,source_id,destination_id";
    const auto count = events.empty() ? 0 : events.front().features.size();
    for (std::size_t i = 0; i < count; ++i) buffer << ",f" << i;
    buffer << '\n';
    for (const auto& e : events) {
        buffer << e.id << ',' << e.timestamp << ',' << e.operation_id << ',' << e.device_id << ',' << e.account_id
               << ',' << e.source_id << ',' << e.destination_id;
        for (double v : e.features) buffer << ',' << v;
        buffer << '\n';
    }
    output << buffer.str();
    if (!output) throw std::runtime_error("Failed writing events CSV");
}
void write_matches_csv(std::ostream& output, std::uint64_t query_id,
                       const std::vector<Match>& matches, bool header) {
    std::ostringstream buffer;
    buffer.imbue(std::locale::classic());
    buffer << std::setprecision(std::numeric_limits<double>::max_digits10);
    if (header) buffer << "query_id,event_id,timestamp,score\n";
    for (const auto& m : matches)
        buffer << query_id << ',' << m.event_id << ',' << m.timestamp << ',' << m.score << '\n';
    output << buffer.str();
    if (!output) throw std::runtime_error("Failed writing matches CSV");
}
} // namespace correlation
