#include "SearchEngine.h"
#include <vector>
#include "ClassEnumerator.h"
#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <atomic>
#include <mutex>
#include "core/Helpers.h"

namespace yambz {

// ═══════════════════════════════════════════════════════════
//  Index: copies of lowercased names (parallel to classes vector)
// ═══════════════════════════════════════════════════════════
struct IndexState {
    std::vector<std::string> fullLower;
    std::vector<std::string> pkgLower;
    std::vector<std::string> simpleLower;
    std::atomic<bool> ready{false};
    size_t sourceSize = 0;
    mutable std::mutex mu;
};

static IndexState g_idx;

static std::string toLower(const std::string& s) {
    std::string out;
    out.resize(s.size());
    std::transform(s.begin(), s.end(), out.begin(),
                   [](unsigned char c) { return (char) std::tolower(c); });
    return out;
}

bool SearchEngine::indexReady() { return g_idx.ready.load(); }
size_t SearchEngine::indexSize() { return g_idx.sourceSize; }

void SearchEngine::ensureLowercase(const std::vector<ClassInfo>& classes) {
    std::lock_guard<std::mutex> lk(g_idx.mu);
    if (g_idx.sourceSize == classes.size() && g_idx.ready.load()) return;

    g_idx.fullLower.clear();
    g_idx.pkgLower.clear();
    g_idx.simpleLower.clear();
    g_idx.fullLower.reserve(classes.size());
    g_idx.pkgLower.reserve(classes.size());
    g_idx.simpleLower.reserve(classes.size());

    for (const auto& c : classes) {
        g_idx.fullLower.push_back(toLower(c.fullName));
        g_idx.pkgLower.push_back(toLower(c.package));
        g_idx.simpleLower.push_back(toLower(c.simpleName));
    }
    g_idx.sourceSize = classes.size();
    g_idx.ready.store(true);
    LOGI("SearchEngine: indexed %zu classes", classes.size());
}

void SearchEngine::rebuildIndex(const std::vector<ClassInfo>& classes) {
    std::lock_guard<std::mutex> lk(g_idx.mu);
    g_idx.ready.store(false);
    ensureLowercase(classes);
}

bool SearchEngine::containsCI(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
        });
    return it != haystack.end();
}

bool SearchEngine::fuzzyMatch(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    size_t j = 0;
    for (size_t i = 0; i < haystack.size() && j < needle.size(); i++) {
        if (std::tolower((unsigned char)haystack[i]) ==
            std::tolower((unsigned char)needle[j])) {
            j++;
        }
    }
    return j == needle.size();
}

// ─── main search ───
std::vector<SearchEngine::Result> SearchEngine::search(
    const std::vector<ClassInfo>& classes,
    const FilterOptions& opts,
    size_t maxResults) {

    std::vector<Result> results;
    if (classes.empty()) return results;

    ensureLowercase(classes);

    const std::string q = opts.caseSensitive ? opts.query : toLower(opts.query);
    const std::string pkgFilter = opts.packagePrefix;
    const bool hasQuery = !q.empty();
    const bool hasPkgFilter = !pkgFilter.empty();

    std::regex regexObj;
    bool useRegex = opts.useRegex && hasQuery;
    if (useRegex) {
        try {
            regexObj = std::regex(q, std::regex::ECMAScript | std::regex::icase);
        } catch (const std::regex_error& e) {
            LOGW("SearchEngine: bad regex: %s", e.what());
            useRegex = false;
        }
    }

    results.reserve(std::min<size_t>(classes.size(), maxResults));

    for (size_t i = 0; i < classes.size(); i++) {
        if (results.size() >= maxResults) break;

        const auto& c = classes[i];

        // package filter
        if (hasPkgFilter && c.package.rfind(pkgFilter, 0) != 0) continue;

        int score = 0;
        bool matched = !hasQuery;

        if (hasQuery) {
            const std::string& full   = g_idx.fullLower[i];
            const std::string& simple = g_idx.simpleLower[i];
            const std::string& pkg    = g_idx.pkgLower[i];

            if (useRegex) {
                if (std::regex_search(c.fullName, regexObj)) {
                    matched = true;
                    score = 100;
                }
            } else {
                // Tier 0: exact match (سريع)
                if (simple == q) {
                    matched = true;
                    score = 1000;
                }
                // Tier 1: prefix
                else if (simple.rfind(q, 0) == 0) {
                    matched = true;
                    score = 800;
                }
                // Tier 2: substring
                else if (full.find(q) != std::string::npos) {
                    matched = true;
                    score = 600;
                }
                // Tier 3: fuzzy (نادراً ما يحتاجها المستخدم)
                else if (q.size() <= 8 && fuzzyMatch(full, q)) {
                    matched = true;
                    score = 300;
                }
            }
        }

        if (!matched) continue;

        // bonus: shorter names are more relevant
        score -= (int)(c.fullName.size() / 4);

        results.push_back({(uint32_t)i, -1, score});
    }

    // sort by score (desc), tiebreak by name length (asc)
    std::sort(results.begin(), results.end(),
              [&classes](const Result& a, const Result& b) {
                  if (a.score != b.score) return a.score > b.score;
                  return classes[a.classIdx].fullName.size() <
                         classes[b.classIdx].fullName.size();
              });

    LOGD("SearchEngine: '%s' → %zu results", opts.query.c_str(), results.size());
    return results;
}

} // namespace yambz
