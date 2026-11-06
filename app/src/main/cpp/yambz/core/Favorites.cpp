#include "Favorites.h"
#include <vector>
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <chrono>
#include <mutex>

namespace yambz {

Favorites& Favorites::instance() {
    static Favorites f;
    return f;
}

void Favorites::init(const std::string& dir) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_path = dir + "/favorites.txt";
    m_inited = true;
    load();
}

void Favorites::load() {
    std::ifstream f(m_path);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        // Format: kind|className|methodName|methodDesc|addedAt
        std::stringstream ss(line);
        FavoriteItem it;
        std::string ts;
        if (!std::getline(ss, it.kind, '|')) continue;
        if (!std::getline(ss, it.className, '|')) continue;
        if (!std::getline(ss, it.methodName, '|')) continue;
        if (!std::getline(ss, it.methodDesc, '|')) continue;
        if (!std::getline(ss, ts, '|')) continue;
        try { it.addedAt = std::stoull(ts); } catch (...) { it.addedAt = 0; }
        m_items.push_back(std::move(it));
    }
}

void Favorites::save() {
    std::ofstream f(m_path, std::ios::trunc);
    if (!f.is_open()) return;
    for (const auto& it : m_items) {
        f << it.kind << '|' << it.className << '|' << it.methodName
          << '|' << it.methodDesc << '|' << it.addedAt << '\n';
    }
}

bool Favorites::add(const FavoriteItem& f) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_inited) return false;
    for (const auto& it : m_items) if (it.methodDesc == f.methodDesc) return false;
    m_items.push_back(f);
    save();
    return true;
}

bool Favorites::remove(const std::string& methodDesc) {
    std::lock_guard<std::mutex> lk(m_mu);
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (it->methodDesc == methodDesc) {
            m_items.erase(it);
            save();
            return true;
        }
    }
    return false;
}

bool Favorites::isFavorite(const std::string& methodDesc) const {
    std::lock_guard<std::mutex> lk(m_mu);
    for (const auto& it : m_items) if (it.methodDesc == methodDesc) return true;
    return false;
}

std::vector<FavoriteItem> Favorites::list() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_items;
}

} // namespace yambz
