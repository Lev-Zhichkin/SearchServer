#include "search_server.h"

#include <cmath>

#include "string_processing.h"

using namespace std;


SearchServer::SearchServer(const std::string& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))
{
}

SearchServer::SearchServer(std::string_view stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))
{
}

void SearchServer::AddDocument(int document_id, std::string_view document, DocumentStatus status, const std::vector<int>& ratings) {

    using namespace std;

    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw invalid_argument("Invalid document_id"s);
    }
    vector<string_view> words = SplitIntoWordsNoStop(document);

    const double inv_word_count = 1.0 / words.size();
    for (string_view& word : words) {
        string s_word{ word };
        if (strings_and_view_.count(s_word) == 0) {
            strings_and_view_[s_word].first = s_word;
            string_view sv_word{ strings_and_view_.at(s_word).first };
            strings_and_view_.at(s_word).second = sv_word;
        }
        word_to_document_freqs_[strings_and_view_.at(s_word).second][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][strings_and_view_.at(s_word).second] += inv_word_count;
    }
    documents_.emplace(document_id, DocumentData{ ComputeAverageRating(ratings), status });
    document_ids_.insert(document_id);
}

std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentStatus status) const {   
    return FindTopDocuments(raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
        });
}

std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query) const {   
    return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

std::set<int>::const_iterator  SearchServer::begin() const {
    return document_ids_.begin();
}

std::set<int>::const_iterator  SearchServer::end() const {
    return document_ids_.end();
}

const std::map<std::string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {              

    static std::map<std::string_view, double> document_to_word_freqs_view;

    if (document_to_word_freqs_.count(document_id) && document_to_word_freqs_.at(document_id).size() != 0) {
        document_to_word_freqs_view = document_to_word_freqs_.at(document_id);
        return document_to_word_freqs_view;
    }
    else {
        document_to_word_freqs_view = {};
        return document_to_word_freqs_view;
    }

}

void SearchServer::RemoveDocument(int document_id) {

    if (document_to_word_freqs_.count(document_id) == 0) {
        return;
    }

    for (auto it = word_to_document_freqs_.begin(); it != word_to_document_freqs_.end(); ++it) {
        auto w_t_d_f_ = it->second.find(document_id);
        if (w_t_d_f_ != it->second.end()) {
            it->second.erase(w_t_d_f_);
        }
    }

    documents_.erase(documents_.find(document_id));

    document_ids_.erase(find(document_ids_.begin(), document_ids_.end(), document_id));

    document_to_word_freqs_.erase(document_id);

}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(std::string_view raw_query, int document_id) const {
    return MatchDocument(std::execution::seq, raw_query, document_id);
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::execution::sequenced_policy&, std::string_view raw_query, int document_id) const {   

    if (!IsValidWord(raw_query)) {
        throw std::invalid_argument("Query is invalid");
    }

    const Query query = ParseQuery(raw_query);

    if (document_ids_.count(document_id) == 0) {
        return { {}, {} };
        //throw std::out_of_range("Invalid Argument");
    }

    std::vector<std::string_view> matched_words;
    std::for_each(std::execution::seq, query.plus_words.begin(), query.plus_words.end(),
        [this, &matched_words, &document_id](std::string_view word) {
            if (!(this->word_to_document_freqs_.count(word) == 0)) {
                if (this->word_to_document_freqs_.at(word).count(document_id)) {
                    matched_words.push_back(word);
                }
            }});

    std::any_of(std::execution::seq, query.minus_words.begin(), query.minus_words.end(),
        [this, &matched_words, &document_id](std::string_view word) {
            if (this->word_to_document_freqs_.count(word) == 0) {
                return false;
            }
            if (this->word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                return true;
            }
            return true;
        });

    return { matched_words, documents_.at(document_id).status };
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::execution::parallel_policy&, std::string_view raw_query, int document_id) const {

    if (!IsValidWord(raw_query)) {
        throw std::invalid_argument("Query is invalid");
    }

    const Query query = ParseQuery(raw_query);

    if (document_ids_.count(document_id) == 0) {
        return { {}, {} };
        //throw std::out_of_range("Invalid Argument");
    }

    std::vector<std::string_view> matched_words;
    std::for_each(std::execution::par, query.plus_words.begin(), query.plus_words.end(),
        [this, &matched_words, &document_id](std::string_view word) {
            if (!(this->word_to_document_freqs_.count(word) == 0)) {
                if (this->word_to_document_freqs_.at(word).count(document_id)) {
                    matched_words.push_back(word);
                }
            }});

    std::any_of(std::execution::par, query.minus_words.begin(), query.minus_words.end(),
        [this, &matched_words, &document_id](std::string_view word) {
            if (this->word_to_document_freqs_.count(word) == 0) {
                return false;
            }
            if (this->word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                return true;
            }
            return true;
        });

    return { matched_words, documents_.at(document_id).status };
}

bool SearchServer::IsStopWord(const std::string_view& word) const {
    return stop_words_.count(word) > 0;
}


bool SearchServer::IsValidWord(std::string_view word) {
    // A valid word must not contain special characters
    return std::none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
        });
}

std::vector<std::string_view> SearchServer::SplitIntoWordsNoStop(std::string_view text) const {

    using namespace std;

    vector<string_view> words;
    for (const string_view word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            string s_word(word.begin(), word.end());
            throw invalid_argument("Word "s + string(s_word) + " is invalid"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const std::vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = 0;
    for (const int rating : ratings) {
        rating_sum += rating;
    }
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(std::string_view text) const {

    using namespace std;

    if (text.empty()) {
        throw invalid_argument("Query word is empty"s);
    }
    bool is_minus = false;
    if (text[0] == '-') {
        is_minus = true;
        text = text.substr(1);
    }
    if (text.empty() || text[0] == '-' || !IsValidWord(text)) {
        throw invalid_argument("Query word "s + string(text) + " is invalid"s);
    }
    return { text, is_minus, IsStopWord(text) };
}


SearchServer::Query SearchServer::ParseQuery(const std::string_view& text) const {
    Query result = {};
    for (const std::string_view word : SplitIntoWords(text)) {
        QueryWord query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.insert(query_word.data);
            }
            else {
                result.plus_words.insert(query_word.data);
            }
        }
    }
    return result;
}

// Existence required
double SearchServer::ComputeWordInverseDocumentFreq(std::string_view word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
} 