#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <execution>

#include "document.h"
#include "concurrent_map.h"
#include "string_processing.h"

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const double ACCURACY = 1e-6;

class SearchServer {
public:
    template<typename StringContainer>
    explicit SearchServer(const StringContainer &stop_words);

    explicit SearchServer(const std::string &stop_words_text);

    explicit SearchServer(const std::string_view stop_words_text);

    void AddDocument(int document_id, const std::string_view document, DocumentStatus status, const std::vector<int> &ratings);

    void RemoveDocument(int document_id);

    void RemoveDocument(const std::execution::sequenced_policy &, int document_id);

    void RemoveDocument(const std::execution::parallel_policy &, int document_id);

    template<typename Handler>
    std::vector<Document> FindTopDocuments(const std::string_view raw_query, Handler lambda) const;

    template<typename Handler, typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(
            ExecutionPolicy &&policy,
            std::string_view raw_query,
            Handler lambda) const;

    template<typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy &&policy, const std::string_view raw_query) const;

    std::vector<Document> FindTopDocuments(const std::string_view raw_query) const;

    template<typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy &&policy, const std::string_view raw_query, DocumentStatus needed_status) const;

    std::vector<Document> FindTopDocuments(const std::string_view raw_query, DocumentStatus needed_status) const;

    int GetDocumentCount() const;

    const std::set<int>::const_iterator begin() const;

    const std::set<int>::const_iterator end() const;

    const std::map<std::string_view, double> &GetWordFrequencies(int document_id) const;

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::string_view raw_query, int document_id) const;

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::execution::sequenced_policy &,
                                                                            const std::string_view raw_query,
                                                                            int document_id) const;

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::execution::parallel_policy &,
                                                                            const std::string_view raw_query,
                                                                            int document_id) const;

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
        std::string data;
    };

    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };

    std::set<std::string, std::less<>> stop_words_;
    std::map<std::string_view, std::map<int, double>> word_to_document_freqs_;
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> docs_id_;

    bool IsStopWord(const std::string_view word) const;

    std::vector<std::string_view> SplitIntoWordsNoStop(const std::string_view text) const;

    static int ComputeAverageRating(const std::vector<int> &ratings);

    static bool IsValidWord(const std::string_view word);

    QueryWord ParseQueryWord(std::string_view text) const;

    Query ParseQuery(const std::string_view text) const;

    double ComputeWordInverseDocumentFreq(const std::string_view word) const;

    template<typename Handler>
    std::vector<Document> FindAllDocuments(const Query &query, Handler lambda) const;

    template<typename Handler, typename ExecutionPolicy>
    std::vector<Document> FindAllDocuments(ExecutionPolicy &&policy, const Query &query, Handler lambda) const;
};

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
        : stop_words_(MakeUniqueNonEmptyStrings(stop_words))
{
    for (const auto& word: stop_words) {
        if (!IsValidWord(word)) {
            throw std::invalid_argument("init stop word - contains an invalid character");
        }
    }
}

template<typename Handler>
std::vector<Document> SearchServer::FindTopDocuments(const std::string_view raw_query, Handler lambda) const {
    const Query query = ParseQuery(raw_query);
    auto matched_documents = FindAllDocuments(query, lambda);

    sort(matched_documents.begin(), matched_documents.end(),
         [](const Document& lhs, const Document& rhs) {
             if (std::abs(lhs.relevance - rhs.relevance) < ACCURACY) {
                 return lhs.rating > rhs.rating;
             } else {
                 return lhs.relevance > rhs.relevance;
             }
         });
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}

template <typename Handler, typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(
        ExecutionPolicy&& policy,
        std::string_view raw_query,
        Handler lambda) const {
    const Query query = ParseQuery(raw_query);
    auto matched_documents = FindAllDocuments(policy, query, lambda);

    sort(policy,
         matched_documents.begin(),
         matched_documents.end(),
         [](const Document& lhs, const Document& rhs)
         {
             if (std::abs(lhs.relevance - rhs.relevance) < ACCURACY) {
                 return lhs.rating > rhs.rating;
             } else {
                 return lhs.relevance > rhs.relevance;
             }
         });
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}

template <typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, const std::string_view raw_query) const {
    return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}

template <typename ExecutionPolicy>
std::vector<Document>SearchServer::FindTopDocuments(ExecutionPolicy&& policy, const std::string_view raw_query, DocumentStatus needed_status) const {
    return FindTopDocuments(policy, raw_query, [needed_status](int _, DocumentStatus status, int rating) {
        return status == needed_status;
    });
}

template<typename Handler, typename ExecutionPolicy>
std::vector<Document> SearchServer::FindAllDocuments(ExecutionPolicy&& policy,
                                                     const Query& query,
                                                     Handler lambda) const {
    ConcurrentMap<int, double> document_to_relevance(20);

    std::for_each(policy,
                  query.plus_words.begin(),
                  query.plus_words.end(),
                  [this, &lambda, &document_to_relevance](std::string_view word)
                  {
                      if (word_to_document_freqs_.count(word)) {
                          const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
                          for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                              const DocumentData& data = documents_.at(document_id);
                              if (lambda(document_id, data.status, data.rating)) {
                                  document_to_relevance[document_id].ref_to_value += term_freq * inverse_document_freq;
                              }
                          }
                      }
                  });

    std::for_each(policy,
                  query.minus_words.begin(),
                  query.minus_words.end(),
                  [this, &document_to_relevance](std::string_view word)
                  {
                      if (word_to_document_freqs_.count(word)) {
                          for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                              document_to_relevance.Erase(document_id);
                          }
                      }
                  });

    std::map<int, double> ordinary_document_to_relevance = document_to_relevance.BuildOrdinaryMap();
    std::vector<Document> matched_documents;
    matched_documents.reserve(ordinary_document_to_relevance.size());

    for (const auto [document_id, relevance] : ordinary_document_to_relevance) {
        matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
    }
    return matched_documents;
}

template<typename Handler>
std::vector<Document> SearchServer::FindAllDocuments(const Query& query, Handler lambda) const {
    std::map<int, double> document_to_relevance;
    for (const std::string_view word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
        for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
            if (lambda(document_id, documents_.at(document_id).status, documents_.at(document_id).rating)) {
                document_to_relevance[document_id] += term_freq * inverse_document_freq;
            }
        }
    }

    for (const std::string_view word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
            document_to_relevance.erase(document_id);
        }
    }

    std::vector<Document> matched_documents;
    for (const auto [document_id, relevance] : document_to_relevance) {
        matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
    }
    return matched_documents;
}