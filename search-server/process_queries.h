//
// Created by Artemiy Dzhamaldinov on 5/20/23.
//

#pragma once

#include "document.h"
#include "search_server.h"

#include <vector>

std::vector<std::vector<Document>> ProcessQueries(
        const SearchServer& search_server,
        const std::vector<std::string>& queries);

std::vector<Document> ProcessQueriesJoined(
        const SearchServer& search_server,
        const std::vector<std::string>& queries);