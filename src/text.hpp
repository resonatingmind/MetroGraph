#pragma once

// Small string helpers shared by the parsers. Internal to metrograph_core.

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace metrograph::detail {

inline std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

inline std::string_view trim(std::string_view s) {
    const auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

// Removes a leading UTF-8 byte-order mark, which Windows tools (Notepad, PowerShell pipes) like to add.
inline void strip_utf8_bom(std::string& s) {
    if (s.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        s.erase(0, 3);
    }
}

// Splits on every separator; adjacent separators yield empty fields.
inline std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            fields.push_back(s.substr(start));
            return fields;
        }
        fields.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
}

}  // namespace metrograph::detail
