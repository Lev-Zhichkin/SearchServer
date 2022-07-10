#pragma once

#include <algorithm>
#include <execution>
#include <future>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "document.h"
#include "concurrent_map.h"
#include "log_duration.h"
#include "string_processing.h"

class SearchServer {

public:

    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words)
        : stop_words_(MakeUniqueNonEmptyStrings(stop_words))  // Extract non-empty stop words
    {
        using namespace std;
        if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
            throw invalid_argument("Some of stop words are invalid"s);
        }
    }

    explicit SearchServer(const std::string& stop_words_text);

    explicit SearchServer(std::string_view stop_words_text);        

    void AddDocument(int document_id, std::string_view document, DocumentStatus status, const std::vector<int>& ratings); 

    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentStatus status) const;

    std::vector<Document> FindTopDocuments(std::string_view raw_query) const;

    template <typename ExecutionPolicy, typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(ExecutionPolicy policy, const std::string_view& raw_query, DocumentPredicate document_predicate) const {
        const auto query = ParseQuery(raw_query);

        auto matched_documents = FindAllDocuments(policy, query, document_predicate);

        sort(matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
            return (std::abs(lhs.relevance - rhs.relevance) < 1e-6) ?
                (lhs.rating > rhs.rating) : (lhs.relevance > rhs.relevance);
            });
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }

        return matched_documents;
    }

    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy policy, const std::string_view& raw_query, DocumentStatus status) const {
        return FindTopDocuments(policy, raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
            return document_status == status;
            });
    }

    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy policy, const std::string_view& raw_query) const {
        return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
    }

    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(const std::string_view& raw_query, DocumentPredicate document_predicate) const {
        return FindTopDocuments(std::execution::seq, raw_query, document_predicate);
    }

    int GetDocumentCount() const;

    std::set<int>::const_iterator begin() const;

    std::set<int>::const_iterator end() const;

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::execution::sequenced_policy&, std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::execution::parallel_policy&, std::string_view raw_query, int document_id) const;

    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;         

    void RemoveDocument(int document_id);

    template<typename ExecutionPolicy>
    void RemoveDocument(ExecutionPolicy& policy, int document_id) {

        if (document_to_word_freqs_.count(document_id) == 0) {
            return;
        }

        std::for_each(policy, word_to_document_freqs_.begin(), word_to_document_freqs_.end(),
            [&document_id](auto& to_erase) {
                auto find_erase = to_erase.second.find(document_id);
                if (find_erase != to_erase.second.end()) {
                    to_erase.second.erase(find_erase);
                }
            });

        documents_.erase(documents_.find(document_id));

        document_ids_.erase(find(document_ids_.begin(), document_ids_.end(), document_id));

        document_to_word_freqs_.erase(document_id);

    }

private:

    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    const std::set<std::string, std::less<>> stop_words_;
    std::map<std::string_view, std::map<int, double>> word_to_document_freqs_;
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;
    std::map<std::string, std::pair<std::string, std::string_view>> strings_and_view_;
    std::map<int, DocumentData> documents_;
    std::set<int,std::less<>> document_ids_;

    bool IsStopWord(const std::string_view& word) const;

    static bool IsValidWord(std::string_view word);

    std::vector<std::string_view> SplitIntoWordsNoStop(std::string_view text) const;

    static int ComputeAverageRating(const std::vector<int>& ratings);

    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(const std::string_view text) const;

    struct Query {
        std::set<std::string_view> plus_words;
        std::set<std::string_view> minus_words;
    };

    Query ParseQuery(const std::string_view& text) const;

    double ComputeWordInverseDocumentFreq(std::string_view word) const;

    template <typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const {
        std::map<int, double> document_to_relevance;
        for (const std::string_view word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                const auto& document_data = documents_.at(document_id);
                if (document_predicate(document_id, document_data.status, document_data.rating)) {
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
            matched_documents.push_back({ document_id, relevance, documents_.at(document_id).rating });
        }
        return matched_documents;
    }

    template <typename ExecutionPolicy, typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(ExecutionPolicy policy, const Query& query, DocumentPredicate document_predicate) const {
        if constexpr (std::is_same_v<ExecutionPolicy, std::execution::sequenced_policy>) {
            return FindAllDocuments(query, document_predicate);
        }
        else {
            static constexpr int MINUS_LOCK_COUNT = 10;
            ConcurrentMap<int, int> minus_ids(MINUS_LOCK_COUNT);
            for_each(
                policy,
                query.minus_words.begin(),
                query.minus_words.end(),
                [this, &minus_ids](const std::string_view word) {
                    if (word_to_document_freqs_.count(word)) {
                        for (const auto& document_freqs : word_to_document_freqs_.at(word)) {
                            minus_ids[document_freqs.first];
                        }
                    }
                }
            );

            auto minus = minus_ids.BuildOrdinaryMap();

            static constexpr int PLUS_LOCK_COUNT = 10000;
            ConcurrentMap<int, double> document_to_relevance(PLUS_LOCK_COUNT);
            static constexpr int PART_COUNT = 4;
            const auto part_length = query.plus_words.size() / PART_COUNT;
            auto part_begin = query.plus_words.begin();
            auto part_end = next(part_begin, part_length);

            std::vector<std::future<void>> futures;
            for (int i = 0; i < PART_COUNT; ++i, part_begin = part_end, part_end = (i == PART_COUNT - 1 ? query.plus_words.end() : next(part_begin, part_length)))
            {
                futures.push_back(std::async([this, part_begin, part_end, document_predicate, &document_to_relevance, &minus]
                    {
                        for_each(part_begin, part_end, [this, document_predicate, &document_to_relevance, &minus](std::string_view word)
                            {
                                if (word_to_document_freqs_.count(word)) {
                                    const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
                                    //                        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
                                    for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                                        const auto& document_data = documents_.at(document_id);
                                        if (document_predicate(document_id, document_data.status, document_data.rating) &&
                                            (minus.count(document_id) == 0)) {
                                            document_to_relevance[document_id].ref_to_value += term_freq * inverse_document_freq;
                                        }
                                    }
                                }

                            });
                    }));
            }

            for (auto& f : futures) {
                f.get();
            }

            std::vector<Document> matched_documents;
            for (const auto [document_id, relevance] : document_to_relevance.BuildOrdinaryMap()) {
                matched_documents.push_back({ document_id, relevance, documents_.at(document_id).rating });
            }
            return matched_documents;
        }
    }

};