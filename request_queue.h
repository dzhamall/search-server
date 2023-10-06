#pragma once 

#include <deque>
#include <vector>

#include "search_server.h"
#include "document.h"

class RequestQueue {
public:
    explicit RequestQueue(const SearchServer& search_server);

    template <typename DocumentPredicate>
    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentPredicate document_predicate);
    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentStatus status);
    std::vector<Document> AddFindRequest(const std::string& raw_query);

    int GetNoResultRequests() const;

private:
    struct QueryResult {
        bool has_result = true;
    };

    std::deque<QueryResult> requests_;
    int no_result_requests_;

    const static int min_in_day_ = 1440;
    const SearchServer& search_server_;

    void AddRequest(const std::vector<Document> docs);
};

template <typename DocumentPredicate>
std::vector<Document> RequestQueue::AddFindRequest(const std::string& raw_query, DocumentPredicate document_predicate) {
    const std::vector<Document> result = search_server_.FindTopDocuments(raw_query, document_predicate);
    AddRequest(result);
    return result;
}