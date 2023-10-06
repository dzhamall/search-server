#include "search_server.h"

#include <numeric>

SearchServer::SearchServer(const std::string& stop_words_text)
    : SearchServer(SplitIntoWords(std::string_view(stop_words_text)))
{}

SearchServer::SearchServer(const std::string_view stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))
{}

void SearchServer::AddDocument(int document_id,
                               const std::string_view document,
                               DocumentStatus status,
                               const std::vector<int>& ratings) {
    if (document_id < 0)
        throw std::invalid_argument("document id : " + std::to_string(document_id) + " < 0");
    if (documents_.count(document_id) > 0)
        throw std::invalid_argument("document id - " + std::to_string(document_id) + " already exists");

    // Save string
    const std::string current_document_string = std::string(document);
    documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status, current_document_string});

    // Use saved string through string_view
    const std::vector<std::string_view> words = SplitIntoWordsNoStop(documents_.at(document_id).data);

    for (const std::string_view word : words) {
        if (!IsValidWord(word))
            throw std::invalid_argument("AddDocument word : contains an invalid character");

        word_to_document_freqs_[word][document_id] += 1.0 / words.size();
        document_to_word_freqs_[document_id][word] += 1.0 / words.size();
    }
    docs_id_.insert(document_id);
}

std::vector<Document> SearchServer::FindTopDocuments(const std::string_view raw_query) const {
    return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}

std::vector<Document> SearchServer::FindTopDocuments(const std::string_view raw_query, DocumentStatus needed_status) const {
    return FindTopDocuments(raw_query, [needed_status](int document_id, DocumentStatus status, int rating) {
        return status == needed_status;
    });
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

const std::set<int>::const_iterator SearchServer::begin() const {
    return docs_id_.begin();
}

const std::set<int>::const_iterator SearchServer::end() const {
    return docs_id_.end();
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::string_view raw_query, int document_id) const {
    if (!docs_id_.count(document_id))
        throw std::out_of_range("MatchDocument out_of_range - передан не сущ. id ");

    const Query query = ParseQuery(raw_query);
    for (const std::string_view word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0)
            continue;

        if (word_to_document_freqs_.at(word).count(document_id)) {
            return {{}, documents_.at(document_id).status};
        }
    }

    std::vector<std::string_view> matched_words;
    for (const std::string_view word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0)
            continue;

        if (word_to_document_freqs_.at(word).count(document_id))
            matched_words.push_back(word);
    }

    return {matched_words, documents_.at(document_id).status};
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::execution::sequenced_policy&,
                                                                                      const std::string_view raw_query,
                                                                                      int document_id) const {
    return MatchDocument(raw_query, document_id);
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::execution::parallel_policy&,
                                                                                      const std::string_view raw_query,
                                                                                      int document_id) const {
    if (!docs_id_.count(document_id))
        throw std::out_of_range("MatchDocument out_of_range - передан не сущ. id ");

    const Query query = ParseQuery(raw_query);
    if (std::any_of(std::execution::par,
                    query.minus_words.cbegin(),
                    query.minus_words.cend(),
                    [this, document_id](const std::string_view word) {
                        return (word_to_document_freqs_.count(word) > 0 &&
                                word_to_document_freqs_.at(word).count(document_id) > 0);
                    })) {
        return { {}, documents_.at(document_id).status };
    }

    std::vector<std::string_view> matched_words;
    matched_words.reserve(query.plus_words.size());

    const auto& it = std::copy_if(std::execution::par,
                                  query.plus_words.cbegin(),
                                  query.plus_words.cend(),
                                  matched_words.begin(),
                                  [this, document_id](const std::string_view word) {
                                      return (word_to_document_freqs_.count(word) > 0 && word_to_document_freqs_.at(word).count(document_id) > 0);
                                  });

    matched_words.erase(it, matched_words.end());

    std::sort(std::execution::par, matched_words.begin(), matched_words.end());
    const auto& itr = std::unique(matched_words.begin(), matched_words.end());
    matched_words.erase(itr, matched_words.end());

    return { matched_words, documents_.at(document_id).status };
}

bool SearchServer::IsStopWord(const std::string_view word) const {
    return stop_words_.count(word) > 0;
}

void SearchServer::RemoveDocument(int document_id) {
    if (document_to_word_freqs_.count(document_id) == 1) {
        for (const auto& [word, freq] : document_to_word_freqs_.at(document_id)) {
            word_to_document_freqs_.at(word).erase(document_id);
        }
    }
    document_to_word_freqs_.erase(document_id);
    documents_.erase(document_id);
    docs_id_.erase(document_id);
}

void SearchServer::RemoveDocument(const std::execution::sequenced_policy&, int document_id) {
    return RemoveDocument(document_id);
}

void SearchServer::RemoveDocument(const std::execution::parallel_policy&, int document_id) {
    if (document_to_word_freqs_.count(document_id) == 1) {
        std::vector<std::string_view> words_to_erase(document_to_word_freqs_.at(document_id).size());

        std::transform(std::execution::par,
                       document_to_word_freqs_.at(document_id).cbegin(),
                       document_to_word_freqs_.at(document_id).cend(),
                       words_to_erase.begin(),
                       [](const auto words) {
                           return words.first;
                       });

        std::for_each(std::execution::par,
                      words_to_erase.cbegin(),
                      words_to_erase.cend(),
                      [this, document_id](const auto word) {
                          word_to_document_freqs_.at(word).erase(document_id);
                      });
    }
    document_to_word_freqs_.erase(document_id);
    documents_.erase(document_id);
    docs_id_.erase(document_id);
}

std::vector<std::string_view> SearchServer::SplitIntoWordsNoStop(const std::string_view text) const {
    std::vector<std::string_view> words;
    for (const std::string_view word : SplitIntoWords(text)) {
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

const std::map<std::string_view , double>& SearchServer::GetWordFrequencies(int document_id) const {
    static const std::map<std::string_view , double> res;
    if (document_to_word_freqs_.count(document_id) == 0) {
        return res;
    }
    else {
        return document_to_word_freqs_.at(document_id);
    }
}


int SearchServer::ComputeAverageRating(const std::vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = std::accumulate(ratings.begin(), ratings.end(), 0);
    return rating_sum / static_cast<int>(ratings.size());
}

bool SearchServer::IsValidWord(const std::string_view word) {
    return std::none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
    });
}

SearchServer::QueryWord SearchServer::ParseQueryWord(std::string_view text) const {
    if (text.size() == 1 && text[0] == '-') {
        throw std::invalid_argument("ParseQueryWord word: contains an invalid character");
    }
    if (text.size() > 1 && text[0] == '-' && text[1] == '-') {
        throw std::invalid_argument("ParseQueryWord word: contains an invalid character");
    }

    bool is_minus = false;
    if (text[0] == '-') {
        is_minus = true;
        text = text.substr(1);
    }
    return {text, is_minus, IsStopWord(text)};
}

SearchServer::Query SearchServer::ParseQuery(const std::string_view text) const {
    Query query;
    for (const std::string_view word : SplitIntoWords(text)) {
        if (!IsValidWord(word))
            throw std::invalid_argument("ParseQuery word: contains an invalid character");

        const QueryWord query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                query.minus_words.push_back(query_word.data);
            } else {
                query.plus_words.push_back(query_word.data);
            }
        }
    }
    std::sort(query.minus_words.begin(), query.minus_words.end());
    auto last_it = std::unique(query.minus_words.begin(), query.minus_words.end());
    query.minus_words.resize(std::distance(query.minus_words.begin(), last_it));

    std::sort(query.plus_words.begin(), query.plus_words.end());
    auto itp = std::unique(query.plus_words.begin(), query.plus_words.end());
    query.plus_words.resize(std::distance(query.plus_words.begin(), itp));
    return query;
}

double SearchServer::ComputeWordInverseDocumentFreq(const std::string_view word) const {
    return std::log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}
