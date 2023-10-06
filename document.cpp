#include "document.h"

Document::Document(int id, double relevance, int rating)
    : id(id), relevance(relevance), rating(rating)
{}

std::ostream& operator<<(std::ostream& os, const Document& doc) {
    using namespace std::string_literals;
    return os << "{ document_id = "s << doc.id << ", relevance = "s << doc.relevance << ", rating = "s << doc.rating << " }"s;
}