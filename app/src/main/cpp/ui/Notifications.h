#pragma once
#include <string>
#include <deque>
#include <mutex>
#include <chrono>

namespace yambz {
class Notifications {
public:
    static Notifications& instance();
    void push(const std::string& msg, float duration = 3.0f);
    void draw();
private:
    Notifications() = default;
    struct Item {
        std::string text;
        std::chrono::steady_clock::time_point born;
        float duration;
    };
    mutable std::mutex m_mu;
    std::deque<Item> m_items;
};
}
