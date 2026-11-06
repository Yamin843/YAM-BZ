#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace yambz {

struct FavoriteItem {
    std::string className;
    std::string methodDesc;   // full desc
    std::string methodName;
    uint64_t    addedAt;
    std::string kind;          // "method" | "constructor"
};

class Favorites {
public:
    static Favorites& instance();

    void init(const std::string& dir);
    bool add(const FavoriteItem& f);
    bool remove(const std::string& methodDesc);
    bool isFavorite(const std::string& methodDesc) const;
    std::vector<FavoriteItem> list() const;

private:
    Favorites() = default;

    mutable std::mutex m_mu;
    std::vector<FavoriteItem> m_items;
    std::string m_path;
    bool m_inited = false;

    void save();
    void load();
};

} // namespace yambz
