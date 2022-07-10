#include "request_queue.h"

RequestQueue::RequestQueue(const SearchServer& search_server)
    : server(search_server)
{

}

std::vector<Document> RequestQueue::AddFindRequest(const std::string& raw_query, DocumentStatus status) {
    return AddFindRequest(raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
        });
}

std::vector<Document> RequestQueue::AddFindRequest(const std::string& raw_query) {
    return AddFindRequest(raw_query, DocumentStatus::ACTUAL);
}

int RequestQueue::GetNoResultRequests() const {
    int non_existant = 0;
    for (size_t req_num = 0; req_num != requests_.size(); ++req_num) {
        auto& ref = requests_.at(req_num).results;
        if (ref.at(0).id == -1 && ref.at(0).rating == -1 && ref.size() == 1) {
            ++non_existant;
        }
    }
    return non_existant;
}
