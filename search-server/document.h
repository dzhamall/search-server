#pragma once

#include <iostream>
#include <string>

struct Document {
    Document() = default;

    Document(int id, double relevance, int rating);

    int id = 0;
    double relevance = 0.0;
    int rating = 0;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

template <typename Document>
void PrintDocument(const Document& document) {
    using namespace std::string_literals;
    std::cout << "{ document_id = "s << document.id << ", " << "relevance = "s << document.relevance << ", " << "rating = "s << document.rating << " }"s << std::endl;
}

std::ostream& operator<<(std::ostream& os, const Document& doc);