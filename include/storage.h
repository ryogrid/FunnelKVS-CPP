#ifndef FUNNELKVS_STORAGE_H
#define FUNNELKVS_STORAGE_H

#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <memory>

namespace funnelkvs {

class Storage {
private:
    std::unordered_map<std::string, std::vector<uint8_t>> data;
    mutable std::mutex mutex;

public:
    Storage() = default;
    ~Storage() = default;
    
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    
    bool get(const std::string& key, std::vector<uint8_t>& value) const;
    void put(const std::string& key, const std::vector<uint8_t>& value);
    bool remove(const std::string& key);
    void clear();
    size_t size() const;
    bool exists(const std::string& key) const;
};

} // namespace funnelkvs

#endif // FUNNELKVS_STORAGE_H