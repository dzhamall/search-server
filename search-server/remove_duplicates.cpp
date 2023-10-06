#include <iostream>

#include "remove_duplicates.h"

void RemoveDuplicates(SearchServer& search_server) {
    std::map<std::vector<std::string>, int> doc_words;
    std::vector<int> duplicate_ids;

    for (const int doc_id : search_server) {
        const std::map<std::string, double> word_freqs = search_server.GetWordFrequencies(doc_id);
        std::vector<std::string> words;
        words.reserve(word_freqs.size());

        for (const auto& [key, val] : word_freqs) {
            words.push_back(key);
        }

        auto [word, emplaced] = doc_words.emplace(words, doc_id);
        if (!emplaced) {
            duplicate_ids.push_back(doc_id);
        }
    }

    for (const int doc_id : duplicate_ids) {
        using namespace std::literals;
        
        std::cout << "Found duplicate document id "s << doc_id << std::endl;
        search_server.RemoveDocument(doc_id);
    }
}
