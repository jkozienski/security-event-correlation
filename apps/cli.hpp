#pragma once
#include <charconv>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace cli {
using Options = std::map<std::string, std::string>;
inline Options parse(int argc, char** argv, const Options& defaults) {
    auto result = defaults;
    for (int i = 1; i < argc; i += 2) {
        const std::string key = argv[i];
        if (!defaults.count(key)) throw std::invalid_argument("Unknown option: " + key);
        if (i + 1 >= argc) throw std::invalid_argument("Missing value: " + key);
        result[key] = argv[i + 1];
    }
    return result;
}
template<class T> T number(const std::string& text) {
    T value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid unsigned integer: " + text);
    return value;
}
inline double real(const std::string& text) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double value;
    if (!(stream >> std::noskipws >> value) || stream.peek() != std::char_traits<char>::eof())
        throw std::invalid_argument("Invalid number: " + text);
    return value;
}
}
