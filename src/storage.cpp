#include "storage.h"

namespace funnelkvs {

bool Storage::get(const std::string& key, std::vector<uint8_t>& value) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = data.find(key);
    if (it != data.end()) {
        value = it->second;
        return true;
    }
    return false;
}

void Storage::put(const std::string& key, const std::vector<uint8_t>& value) {
    std::lock_guard<std::mutex> lock(mutex);
    data[key] = value;
}

bool Storage::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex);
    return data.erase(key) > 0;
}

void Storage::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    data.clear();
}

size_t Storage::size() const {
    std::lock_guard<std::mutex> lock(mutex);
    return data.size();
}

bool Storage::exists(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex);
    return data.find(key) != data.end();
}

std::vector<std::string> Storage::get_all_keys() const {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<std::string> keys;
    keys.reserve(data.size());
    for (const auto& pair : data) {
        keys.push_back(pair.first);
    }
    return keys;
}

std::unordered_map<std::string, std::vector<uint8_t>> Storage::get_all_data() const {
    std::lock_guard<std::mutex> lock(mutex);
    return data;
}

std::unordered_map<std::string, std::vector<uint8_t>> Storage::get_keys_in_range(
    const std::function<bool(const std::string&)>& predicate) const {
    std::lock_guard<std::mutex> lock(mutex);
    std::unordered_map<std::string, std::vector<uint8_t>> result;
    for (const auto& pair : data) {
        if (predicate(pair.first)) {
            result[pair.first] = pair.second;
        }
    }
    return result;
}

} // namespace funnelkvs