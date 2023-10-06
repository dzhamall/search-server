#include "request_queue.h"

RequestQueue::RequestQueue(const SearchServer& search_server)
    : no_result_requests_(0), search_server_(search_server)
{}

std::vector<Document> RequestQueue::AddFindRequest(const std::string& raw_query, DocumentStatus status) {
    const std::vector<Document> result = search_server_.FindTopDocuments(raw_query, status);
    AddRequest(result);
    return result;
}

std::vector<Document> RequestQueue::AddFindRequest(const std::string& raw_query) {
    const std::vector<Document> result = search_server_.FindTopDocuments(raw_query);
    AddRequest(result);
    return result;
}

int RequestQueue::GetNoResultRequests() const {
    return no_result_requests_;
}

void RequestQueue::AddRequest(const std::vector<Document> docs) {
    if (requests_.size() == min_in_day_) {
        if (requests_.front().has_result == false) {
            no_result_requests_--;
        }
        requests_.pop_front();
    }

    QueryResult query_result;
    if (docs.empty()) {
        query_result.has_result = false;
        no_result_requests_++;
    }

    requests_.push_back(query_result);
}