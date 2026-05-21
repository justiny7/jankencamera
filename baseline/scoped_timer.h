#include <iostream>
#include <chrono>
#include <string_view>
#include <map>
#include <iomanip>

class ScopedTimer {
public:
    explicit ScopedTimer(std::string_view name) 
        : name_(name), start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        std::cout << name_ << ": " << duration.count() << " us\n";

        if (!avg_.count(name_)) {
            avg_[name_] = std::make_pair(0, 0);
        }

        avg_[name_].first += 1LL * duration.count();
        avg_[name_].second++;
    }

    static void print_stats() {
        std::cout << "--- PRINTING STATS ---\n";
        for (auto [name, p] : avg_) {
            double avg = (1.0 * p.first / p.second) / 1000.0;
            std::cout << std::fixed << std::setprecision(3) << name << ": " << avg << " ms\n";
        }
    }

private:
    std::string name_;
    std::chrono::steady_clock::time_point start_;

    inline static std::map<std::string, std::pair<long long, int>> avg_;
};
