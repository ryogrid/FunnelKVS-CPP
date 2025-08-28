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

} // namespace funnelkvs