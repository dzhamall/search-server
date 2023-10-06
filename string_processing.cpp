//
// Created by Artemiy Dzhamaldinov on 6/13/23.
//

#include "string_processing.h"

std::vector<std::string_view> SplitIntoWords(std::string_view text) {
    std::vector<std::string_view> result;
    text.remove_prefix(std::min(text.find_first_not_of(' '), text.size()));
    const std::string_view::size_type pos_end = std::string_view::npos;

    while (!text.empty()) {
        std::string_view::size_type space = text.find(' ');
        result.push_back(space == pos_end ? text.substr(0, text.size()) : text.substr(0, space));
        text.remove_prefix(std::min(text.find_first_not_of(' ', space), text.size()));
    }

    return result;
}