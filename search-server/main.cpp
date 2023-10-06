#include "search_server.h"
#include "read_input_functions.h"
#include "string_processing.h"
#include "request_queue.h"
#include "paginator.h"
#include "process_queries.h"

using namespace std;

template <typename T, typename U>
void AssertEqualImpl(const T& t, const U& u, const string& t_str, const string& u_str, const string& file,
                     const string& func, unsigned line, const string& hint) {
    if (t != u) {
        cout << boolalpha;
        cout << file << "("s << line << "): "s << func << ": "s;
        cout << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cout << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}

#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)
#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))

void AssertImpl(bool value, const string& expr_str, const string& file, const string& func, unsigned line,
                const string& hint) {
    if (!value) {
        cout << file << "("s << line << "): "s << func << ": "s;
        cout << "ASSERT("s << expr_str << ") failed."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}

#define ASSERT(expr) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, ""s)
#define ASSERT_HINT(expr, hint) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, (hint))

void TestExcludeStopWordsFromAddedDocumentContent() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server(""s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id);
    }

    {
        SearchServer server("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT_HINT(server.FindTopDocuments("in"s).empty(),
                    "Stop words must be excluded from documents"s);
    }
}

// Добавление документов. Добавленный документ должен находиться по поисковому запросу, который содержит слова из документа.
void TestAddDocs() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    // Сначала убеждаемся, что поиск слова, не входящего в список стоп-слов,
    // находит нужный документ
    {
        SearchServer server(""s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 1);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id);
    }

    // Затем убеждаемся, что поиск этого же слова, входящего в список стоп-слов,
    // возвращает пустой результат
    {
        SearchServer server("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT(server.FindTopDocuments("in"s).empty());
    }

    // Найдет документ по слову не вход в стоп слова
    {
        Document res = {42, 0.0, 2};
        SearchServer server("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT(server.FindTopDocuments("cat"s).size() == 1);
        for (const Document doc : server.FindTopDocuments("cat"s)) {
            ASSERT_EQUAL(doc.id, res.id);
            ASSERT_EQUAL(doc.relevance, res.relevance);
            ASSERT_EQUAL(doc.rating, res.rating);
        }
    }
}

// Поддержка минус-слов. Документы, содержащие минус-слова поискового запроса, не должны включаться в результаты поиска.
void TestMinusWord() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const string content3 = "pretty dog in the city"s;
    const string content4 = "pretty cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    // Сначала убеждаемся, что поиск слова, не входящего в список стоп-слов,
    // находит нужный документ
    {
        SearchServer server(""s);
        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(11, content2, DocumentStatus::ACTUAL, {1, 1, 1});
        server.AddDocument(1, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(2, content4, DocumentStatus::ACTUAL, {5, 5, 4});

        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 4);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, 2);
    }

    {
        vector<Document> res = {{11, 0.693147, 1}, {42, 0.346574, 2}};
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(11, content2, DocumentStatus::ACTUAL, {1, 1, 1});
        server.AddDocument(1, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(2, content4, DocumentStatus::ACTUAL, {5, 5, 4});

        const auto found_docs = server.FindTopDocuments("cat dog -pretty scary"s);

        ASSERT_EQUAL(found_docs.size(), 2);

        for (int i = 0; i < found_docs.size(); i++) {
            ASSERT_EQUAL(found_docs[i].id, res[i].id);
            ASSERT(abs(found_docs[i].relevance - res[i].relevance) < ACCURACY);
            ASSERT_EQUAL(found_docs[i].rating, res[i].rating);
        }
    }
}

// Матчинг документов.
// При матчинге документа по поисковому запросу должны быть возвращены все слова из поискового запроса, присутствующие в документе.
// Если есть соответствие хотя бы по одному минус-слову, должен возвращаться пустой список слов.
void TestMatch() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const string content3 = "pretty dog in the city"s;
    const string content4 = "pretty cat in the city"s;
    const string content5 = "scary boy"s;

    {
        vector<string> res = {"dog"s, "scary"};
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(11, content2, DocumentStatus::IRRELEVANT, {1, 1, 1});
        server.AddDocument(1, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(2, content4, DocumentStatus::ACTUAL, {5, 5, 4});
        server.AddDocument(9, content5, DocumentStatus::IRRELEVANT, {5, 5, 4});

        const tuple<vector<string_view>, DocumentStatus> match_docs1 = server.MatchDocument("cat dog -pretty scary", 11);
        const auto& match_docs2 = server.MatchDocument("cat in dog -pretty scary", 1);

        int i = 0;
        ASSERT(get<1>(match_docs1) == DocumentStatus::IRRELEVANT);
        for (const auto& d : get<0>(match_docs1))  {
            ASSERT_EQUAL(d, res[i]);
            i++;
        }
        ASSERT(get<1>(match_docs2) == DocumentStatus::ACTUAL);
        ASSERT_EQUAL(get<0>(match_docs2).empty(), true);
    }
}

void TestSort() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const string content3 = "dog dogs in the city"s;
    const string content4 = "pretty cat in the city"s;
    const string content5 = "scary boy"s;
    const vector<int> res = {12, 42};
    // Найдет документ по слову не вход в стоп слова
    {
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(11, content2, DocumentStatus::IRRELEVANT, {1, 1, 1});
        server.AddDocument(12, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(2, content4, DocumentStatus::ACTUAL, {5, 5, 4});
        server.AddDocument(10, content5, DocumentStatus::IRRELEVANT, {5, 5, 4});

        const auto& sort_docs = server.FindTopDocuments(
                "dog cat -pretty dogs"s,
                [](int document_id, DocumentStatus status, int rating) {
                    return document_id % 2 == 0;
                });

        int i = 0;
        for (const auto& doc : sort_docs) {
            ASSERT_EQUAL(doc.id, res[i]);
            i++;
        }
        ASSERT(sort_docs[0].relevance > sort_docs[1].relevance);
    }
}

void TestStatus() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const string content3 = "pretty dog in the city"s;
    const string content4 = "pretty cat in the city"s;
    const string content5 = "scary boy"s;

    // Найдет документ по слову не вход в стоп слова
    {
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(11, content2, DocumentStatus::IRRELEVANT, {1, 1, 1});
        server.AddDocument(1, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(2, content4, DocumentStatus::ACTUAL, {5, 5, 4});
        server.AddDocument(9, content5, DocumentStatus::IRRELEVANT, {5, 5, 4});

        const auto& irrelevant_docs = server.FindTopDocuments("dog cat -pretty"s, DocumentStatus::IRRELEVANT);

        ASSERT_EQUAL(irrelevant_docs.size(), 1);
        for (const auto& doc : irrelevant_docs) {
            ASSERT_EQUAL(doc.id, 11);
        }

        const auto& actual_docs = server.FindTopDocuments("dog cat -pretty"s, DocumentStatus::ACTUAL);
        ASSERT_EQUAL(actual_docs.size(), 1);
        for (const auto& doc : actual_docs) {
            ASSERT_EQUAL(doc.id, 42);
        }

        const auto& banned_docs = server.FindTopDocuments("dog cat -pretty"s, DocumentStatus::BANNED);
        ASSERT_EQUAL(banned_docs.size(), 0);

        const auto& removed_docs = server.FindTopDocuments("dog cat -pretty"s, DocumentStatus::REMOVED);
        ASSERT_EQUAL(removed_docs.size(), 0);

        const auto& default_docs = server.FindTopDocuments("dog cat -pretty"s);
        ASSERT_EQUAL(default_docs.size(), 1);
        for (const auto& doc : default_docs) {
            ASSERT_EQUAL(doc.id, 42);
        }
    }
}

void TestPositiveRating() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const vector<int> res = {2, 1};

    {
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(11, content2, DocumentStatus::ACTUAL, {1, 1, 1});

        const auto& docs = server.FindTopDocuments("dog cat -pretty"s);
        int i = 0;
        for (const auto& doc : docs) {
            ASSERT_EQUAL(doc.rating, res[i]);
            i++;
        }
    }
}

void TestNegativeRating() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const vector<int> res = {-2, -1};

    {
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {-1, -5, -2});
        server.AddDocument(11, content2, DocumentStatus::ACTUAL, {-1, -1});

        const auto& docs = server.FindTopDocuments("dog cat -pretty"s);
        int i = 0;
        for (const auto& doc : docs) {
            ASSERT_EQUAL(doc.rating, res[i]);
            i++;
        }
    }
}

void TestMixedRating() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const vector<int> res = {-2, 0};

    {
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, -5, -2});
        server.AddDocument(11, content2, DocumentStatus::ACTUAL, {2, 1, -3});

        const auto& docs = server.FindTopDocuments("dog cat -pretty"s);
        int i = 0;
        for (const auto& doc : docs) {
            ASSERT_EQUAL(doc.rating, res[i]);
            i++;
        }
    }
}

void TestSearchPredicate() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const string content3 = "dog dogs in the city"s;
    const string content4 = "pretty cat in the city"s;
    const string content5 = "scary boy"s;
    const vector<int> res = {12};

    {
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 2, 2});
        server.AddDocument(11, content2, DocumentStatus::IRRELEVANT, {1, 1, 1});
        server.AddDocument(12, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(2, content4, DocumentStatus::ACTUAL, {5, 5, 4});
        server.AddDocument(10, content5, DocumentStatus::IRRELEVANT, {5, 5, 4});

        const auto& sort_docs = server.FindTopDocuments(
                "dog cat -pretty dogs"s,
                [](int document_id, DocumentStatus status, int rating) {
                    return rating > 2;
                });

        ASSERT_EQUAL(sort_docs.size(), 1);
        for (const auto& doc : sort_docs) {
            ASSERT_EQUAL(doc.id, res[0]);
        }
    }
}

void TestRelevance() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const string content3 = "pretty dog in the city"s;
    const string content4 = "pretty cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    vector<Document> res = {{11, 0.693147, 1}, {42, 0.346574, 2}};

    {
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(11, content2, DocumentStatus::ACTUAL, {1, 1, 1});
        server.AddDocument(1, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(2, content4, DocumentStatus::ACTUAL, {5, 5, 4});

        const auto found_docs = server.FindTopDocuments("cat dog -pretty scary"s);

        ASSERT_EQUAL(found_docs.size(), 2);

        for (int i = 0; i < found_docs.size(); i++) {
            ASSERT_EQUAL(found_docs[i].id, res[i].id);
            ASSERT(abs(found_docs[i].relevance - res[i].relevance) < ACCURACY);
            ASSERT_EQUAL(found_docs[i].rating, res[i].rating);
        }
        ASSERT(found_docs[0].relevance > found_docs[1].relevance);
    }
}

void TestRelevanceSingleDoc() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const string content3 = "pretty dog in the city"s;
    const string content4 = "pretty cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    vector<Document> res = {{42, 0.231049, 2}};

    {
        SearchServer server("the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(11, content2, DocumentStatus::ACTUAL, {1, 1, 1});
        server.AddDocument(1, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(2, content4, DocumentStatus::ACTUAL, {5, 5, 4});

        const auto found_docs = server.FindTopDocuments("cat in city -dog -pretty"s);

        ASSERT_EQUAL(found_docs.size(), 1);

        for (int i = 0; i < found_docs.size(); i++) {
            ASSERT_EQUAL(found_docs[i].id, res[i].id);
            ASSERT(abs(found_docs[i].relevance - res[i].relevance) < ACCURACY);
            ASSERT_EQUAL(found_docs[i].rating, res[i].rating);
        }
    }
}

void TestBasePaginate() {
    {
        SearchServer search_server("and with"s);
        search_server.AddDocument(1, "funny pet and nasty rat"s, DocumentStatus::ACTUAL, {7, 2, 7});
        search_server.AddDocument(2, "funny pet with curly hair"s, DocumentStatus::ACTUAL, {1, 2, 3});
        search_server.AddDocument(3, "big cat nasty hair"s, DocumentStatus::ACTUAL, {1, 2, 8});
        search_server.AddDocument(4, "big dog cat Vladislav"s, DocumentStatus::ACTUAL, {1, 3, 2});
        search_server.AddDocument(5, "big dog hamster Borya"s, DocumentStatus::ACTUAL, {1, 1, 1});

        const auto search_results = search_server.FindTopDocuments("curly dog"s);
        ASSERT_EQUAL(search_results.size(), 3);

        const auto pages = Paginate(search_results, 2);
        ASSERT_EQUAL(distance(pages.begin(), pages.end()), 2);
    }
}

void TestPaginateOnePage() {
    {
        SearchServer search_server("and with"s);
        search_server.AddDocument(1, "funny pet and nasty rat"s, DocumentStatus::ACTUAL, {7, 2, 7});
        search_server.AddDocument(2, "funny pet with curly hair"s, DocumentStatus::ACTUAL, {1, 2, 3});
        search_server.AddDocument(3, "big cat nasty hair"s, DocumentStatus::ACTUAL, {1, 2, 8});
        search_server.AddDocument(4, "big dog cat Vladislav"s, DocumentStatus::ACTUAL, {1, 3, 2});
        search_server.AddDocument(5, "big dog hamster Borya"s, DocumentStatus::ACTUAL, {1, 1, 1});

        const auto search_results = search_server.FindTopDocuments("dog"s);
        ASSERT_EQUAL(search_results.size(), 2);

        const auto pages = Paginate(search_results, 2);
        ASSERT_EQUAL(distance(pages.begin(), pages.end()), 1);
    }
}

void TestResultQueue() {
    SearchServer search_server("and in at"s);
    RequestQueue request_queue(search_server);
    search_server.AddDocument(1, "curly cat curly tail"s, DocumentStatus::ACTUAL, {7, 2, 7});
    search_server.AddDocument(2, "curly dog and fancy collar"s, DocumentStatus::ACTUAL, {1, 2, 3});
    search_server.AddDocument(3, "big cat fancy collar "s, DocumentStatus::ACTUAL, {1, 2, 8});
    search_server.AddDocument(4, "big dog sparrow Eugene"s, DocumentStatus::ACTUAL, {1, 3, 2});
    search_server.AddDocument(5, "big dog sparrow Vasiliy"s, DocumentStatus::ACTUAL, {1, 1, 1});
    // 1439 запросов с нулевым результатом
    for (int i = 0; i < 1439; ++i) {
        request_queue.AddFindRequest("empty request"s);
    }

    ASSERT_EQUAL(request_queue.GetNoResultRequests(), 1439);

    // все еще 1439 запросов с нулевым результатом
    request_queue.AddFindRequest("curly dog"s);
    ASSERT_EQUAL(request_queue.GetNoResultRequests(), 1439);

    // новые сутки, первый запрос удален, 1438 запросов с нулевым результатом
    request_queue.AddFindRequest("big collar"s);
    ASSERT_EQUAL(request_queue.GetNoResultRequests(), 1438);

    // первый запрос удален, 1437 запросов с нулевым результатом
    request_queue.AddFindRequest("sparrow"s);
    ASSERT_EQUAL(request_queue.GetNoResultRequests(), 1437);
}

void TestSearchServerIterators() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const string content3 = "pretty dog in the city"s;
    const string content4 = "pretty cat in the city"s;
    const int id1 = 42;
    const int id2 = 11;
    const int id3 = 2;
    const int id4 = 1;
    const vector<int> res = {id1, id2, id3, id4};

    {
        SearchServer server("in the"s);

        server.AddDocument(id1, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(id2, content2, DocumentStatus::ACTUAL, {1, 1, 1});
        server.AddDocument(id3, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(id4, content4, DocumentStatus::ACTUAL, {5, 5, 4});

        int i = 3;
        for (const auto document_id : server) {
            ASSERT_EQUAL(document_id, res[i]);
            i--;
        }
    }
}

void TestGetWordFrequencies() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const string content3 = "pretty dog in the city"s;
    const string content4 = "pretty cat in the city"s;
    {
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(11, content2, DocumentStatus::ACTUAL, {1, 1, 1});
        server.AddDocument(1, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(2, content4, DocumentStatus::ACTUAL, {5, 5, 4});

        const auto wordFreq = server.GetWordFrequencies(42);

        ASSERT_EQUAL(wordFreq.at("cat"s), 0.5);
    }
}

void TestRemoveDocs() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const string content3 = "pretty dog in the city"s;
    const string content4 = "pretty cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    vector<Document> res = {{11, 0.693147, 1}, {42, 0.346574, 2}};

    {
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(11, content2, DocumentStatus::ACTUAL, {1, 1, 1});
        server.AddDocument(1, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(2, content4, DocumentStatus::ACTUAL, {5, 5, 4});

        server.RemoveDocument(42);
        ASSERT_EQUAL(server.GetDocumentCount(), 3);

        server.RemoveDocument(42);
        ASSERT_EQUAL(server.GetDocumentCount(), 3);

        server.RemoveDocument(2);
        ASSERT_EQUAL(server.GetDocumentCount(), 2);
    }
}

void TestFindTopParWithLambda() {
    const string content1 = "cat in the city"s;
    const string content2 = "dog in the city scary"s;
    const string content3 = "dog dogs in the city"s;
    const string content4 = "pretty cat in the city"s;
    const string content5 = "scary boy"s;
    const vector<int> res = {12, 42};
    // Найдет документ по слову не вход в стоп слова
    {
        SearchServer server("in the"s);

        server.AddDocument(42, content1, DocumentStatus::ACTUAL, {1, 5, 2});
        server.AddDocument(11, content2, DocumentStatus::IRRELEVANT, {1, 1, 1});
        server.AddDocument(12, content3, DocumentStatus::ACTUAL, {4, 2, 3});
        server.AddDocument(2, content4, DocumentStatus::ACTUAL, {5, 5, 4});
        server.AddDocument(10, content5, DocumentStatus::IRRELEVANT, {5, 5, 4});

        const auto& sort_docs = server.FindTopDocuments(
                std::execution::par,
                "dog cat -pretty dogs"s,
                [](int document_id, DocumentStatus status, int rating) {
                    return document_id % 2 == 0;
                });

        int i = 0;
        for (const auto& doc : sort_docs) {
            ASSERT_EQUAL(doc.id, res[i]);
            i++;
        }
        ASSERT(sort_docs[0].relevance > sort_docs[1].relevance);
    }
}

void TestFindTopParWithoutLambda() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    // Сначала убеждаемся, что поиск слова, не входящего в список стоп-слов,
    // находит нужный документ
    {
        SearchServer server(""s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments(std::execution::par, "in"s);
        ASSERT_EQUAL(found_docs.size(), 1);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id);
    }

    // Затем убеждаемся, что поиск этого же слова, входящего в список стоп-слов,
    // возвращает пустой результат
    {
        SearchServer server("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT(server.FindTopDocuments(std::execution::par, "in"s).empty());
    }

    // Найдет документ по слову не вход в стоп слова
    {
        Document res = {42, 0.0, 2};
        SearchServer server("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT(server.FindTopDocuments(std::execution::par, "cat"s).size() == 1);
        for (const Document doc : server.FindTopDocuments(std::execution::par, "cat"s)) {
            ASSERT_EQUAL(doc.id, res.id);
            ASSERT_EQUAL(doc.relevance, res.relevance);
            ASSERT_EQUAL(doc.rating, res.rating);
        }
    }
}

// Функция TestSearchServer является точкой входа для запуска тестов
void TestSearchServer() {
    TestExcludeStopWordsFromAddedDocumentContent();
    TestStatus();
    TestSort();
    TestMatch();
    TestMinusWord();
    TestAddDocs();
    TestPositiveRating();
    TestNegativeRating();
    TestMixedRating();
    TestSearchPredicate();
    TestRelevance();
    TestRelevanceSingleDoc();
    TestBasePaginate();
    TestPaginateOnePage();
    TestResultQueue();
    TestSearchServerIterators();
    TestGetWordFrequencies();
    TestRemoveDocs();
    TestFindTopParWithLambda();
    TestFindTopParWithoutLambda();
}

int main() {
    TestSearchServer();
    cout << "Test successed" << endl;
    return 0;
}