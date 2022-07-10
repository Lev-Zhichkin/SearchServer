#pragma once

#include <vector>
#include <deque>
#include <string>

#include "search_server.h"

class RequestQueue {
public:

    explicit RequestQueue(const SearchServer& search_server);

    // сделаем "обёртки" для всех методов поиска, чтобы сохранять результаты для нашей статистики
    template <typename DocumentPredicate>
    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentPredicate document_predicate) {

        std::vector<Document> documents = server.FindTopDocuments(raw_query, document_predicate);
        QueryResult QR;
        Document pushQR;

        if (documents.size() == 0) {
            pushQR.id = -1;
            pushQR.relevance = -1.1;
            pushQR.rating = -1;
            QR.query = raw_query;
            QR.results.push_back(pushQR);
            requests_.push_back(QR);
            ++cache;
        }
        else {
            ++cache;
            for (auto document : documents) {
                pushQR = document;
                QR.query = raw_query;
                QR.results.push_back(pushQR);
                requests_.push_back(QR);
            }
        }

        while (cache > sec_in_day_) {
            requests_.pop_front();
            --cache;
        }

        return documents;

    }

    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentStatus status);

    std::vector<Document> AddFindRequest(const std::string& raw_query);

    int GetNoResultRequests() const;

private:

    struct QueryResult {
        std::vector<Document> results;
        std::string query;
    };

    std::deque<QueryResult> requests_;
    const static int sec_in_day_ = 1440;
    const SearchServer& server;
    int cache = 0;

};