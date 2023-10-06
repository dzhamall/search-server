//
// Created by Artemiy Dzhamaldinov on 5/20/23.
//

#include "process_queries.h"

#include <execution>

std::vector<std::vector<Document>> ProcessQueries(
        const SearchServer& search_server,
        const std::vector<std::string>& queries) {
    std::vector<std::vector<Document>> documents(queries.size());

    std::transform(std::execution::par,
                   queries.cbegin(),
                   queries.cend(),
                   documents.begin(),
                   [&search_server](const std::string& q) {
        return search_server.FindTopDocuments(q);
    });
    return documents;
}

std::vector<Document> ProcessQueriesJoined(
        const SearchServer& search_server,
        const std::vector<std::string>& queries) {
    std::vector<Document> documents;

    for (const auto& document : ProcessQueries(search_server, queries)) {
        documents.insert(documents.cend(), document.cbegin(), document.cend());
    }

    return documents;
}
