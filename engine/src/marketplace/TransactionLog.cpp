#include "marketplace/TransactionLog.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace engine::marketplace {

void TransactionLog::record(TransactionRecord record) { records_.push_back(std::move(record)); }
void TransactionLog::recordPublish(PublishRecord record) { publishRecords_.push_back(std::move(record)); }

std::vector<TransactionRecord> TransactionLog::recordsForBuyer(uint64_t buyerProfileId) const {
    std::vector<TransactionRecord> result;
    for (const auto& r : records_) {
        if (r.buyerProfileId == buyerProfileId) result.push_back(r);
    }
    return result;
}

std::vector<PublishRecord> TransactionLog::publishRecordsForCreator(const std::string& creatorId) const {
    std::vector<PublishRecord> result;
    for (const auto& r : publishRecords_) {
        if (r.creatorId == creatorId) result.push_back(r);
    }
    return result;
}

bool TransactionLog::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "TRANSACTIONLOG 1\n";
    for (const auto& r : records_) {
        // Real multi-sub-line-per-record shape (itemName/sellerCreatorId
        // are real free text that could contain spaces) -- same
        // convention moderation::AppealLog's own APPEAL/STATEMENT/
        // CONTEXT/NOTE record already establishes.
        out << "TXN " << r.buyerProfileId << ' ' << r.priceCredits << ' ' << r.timestampUnixSeconds << "\n";
        out << "ITEMID " << r.itemId << "\n";
        out << "ITEMNAME " << r.itemName << "\n";
        out << "SELLER " << r.sellerCreatorId << "\n";
    }
    // Real publish events, same file, own real "PUB" record kind -- kept
    // in the same real, one-file transaction ledger rather than a second
    // file (both are real, small, append-mostly commerce/creator records
    // this Alpha keeps together, same "one file, whole database" spirit
    // core::CatalogueDatabase already uses).
    for (const auto& p : publishRecords_) {
        out << "PUB " << p.timestampUnixSeconds << "\n";
        out << "PUBITEMID " << p.itemId << "\n";
        out << "PUBITEMNAME " << p.itemName << "\n";
        out << "PUBCREATOR " << p.creatorId << "\n";
        out << "PUBSTATUS " << p.moderationStatus << "\n";
        out << "PUBCATEGORY " << p.category << "\n";
    }
    out << "END\n";
    return out.good();
}

bool TransactionLog::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("TRANSACTIONLOG", 0) != 0) return false;

    std::vector<TransactionRecord> loadedTxns;
    std::vector<PublishRecord> loadedPubs;
    TransactionRecord* currentTxn = nullptr;
    PublishRecord* currentPub = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("TXN ", 0) == 0) {
            std::istringstream iss(line.substr(4));
            TransactionRecord r;
            iss >> r.buyerProfileId >> r.priceCredits >> r.timestampUnixSeconds;
            loadedTxns.push_back(std::move(r));
            currentTxn = &loadedTxns.back();
            currentPub = nullptr;
        } else if (line.rfind("ITEMID ", 0) == 0 && currentTxn != nullptr) {
            currentTxn->itemId = line.substr(7);
        } else if (line.rfind("ITEMNAME ", 0) == 0 && currentTxn != nullptr) {
            currentTxn->itemName = line.substr(9);
        } else if (line.rfind("SELLER ", 0) == 0 && currentTxn != nullptr) {
            currentTxn->sellerCreatorId = line.substr(7);
        } else if (line.rfind("PUB ", 0) == 0) {
            PublishRecord p;
            p.timestampUnixSeconds = std::strtoll(line.substr(4).c_str(), nullptr, 10);
            loadedPubs.push_back(std::move(p));
            currentPub = &loadedPubs.back();
            currentTxn = nullptr;
        } else if (line.rfind("PUBITEMID ", 0) == 0 && currentPub != nullptr) {
            currentPub->itemId = line.substr(10);
        } else if (line.rfind("PUBITEMNAME ", 0) == 0 && currentPub != nullptr) {
            currentPub->itemName = line.substr(12);
        } else if (line.rfind("PUBCREATOR ", 0) == 0 && currentPub != nullptr) {
            currentPub->creatorId = line.substr(11);
        } else if (line.rfind("PUBSTATUS ", 0) == 0 && currentPub != nullptr) {
            currentPub->moderationStatus = line.substr(10);
        } else if (line.rfind("PUBCATEGORY ", 0) == 0 && currentPub != nullptr) {
            currentPub->category = line.substr(12);
        } else if (line == "END") {
            break;
        }
    }

    records_ = std::move(loadedTxns);
    publishRecords_ = std::move(loadedPubs);
    return true;
}

} // namespace engine::marketplace
