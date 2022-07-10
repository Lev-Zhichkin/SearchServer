#pragma once

#include <vector>
#include <set>
#include <string>

std::vector<std::string_view> SplitIntoWords(std::string_view text);

template <typename StringContainer>
std::set<std::string, std::less<>> MakeUniqueNonEmptyStrings(const StringContainer& strings) {
    std::set<std::string, std::less<>> non_empty_strings;
    for (const std::string_view& str : strings) {
        std::string s_str{ str };
        if (!s_str.empty()) {
            non_empty_strings.insert(s_str);
        }
    }
    return non_empty_strings;
}