// market_replay.cpp
//
// Phase 2: replace uniform-random synthetic order flow with order flow
// derived from a REAL trading day's 15-second Nifty 50 OHLC data.
//
// Honesty note (read this before citing this anywhere): true exchange-level
// L2 order-by-order tick data is not publicly available to a retail/student
// user -- that requires a paid vendor feed or exchange membership. What IS
// real here is the 15-second OHLC price path itself (genuine NSE Nifty 50
// prices, timestamped). For each 15-second bar, this program submits a
// sequence of orders whose prices walk through that bar's actual
// Open -> Low/High -> High/Low -> Close path (direction chosen from
// whether the bar closed up or down), so the book's trade prices are
// anchored to real market movement rather than invented numbers. This is
// "OHLC-to-order-flow reconstruction", a known, named technique for
// working backward from bar data when tick data isn't available -- it is
// explicitly NOT a claim of having real L2 depth data.
//
// What this produces that's genuinely a finding, not decoration: it
// compares matching activity and real price-range volatility between the
// market open, a calm midday window, and the close -- and you can check
// whether the open really was more volatile in this specific real trading
// day, rather than assuming it from a textbook.

#include "order_book.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <random>

using namespace lob;

struct Bar {
    long epoch;
    std::string timestamp; // ISO 8601, e.g. 2026-09-17T09:15:00
    double open, high, low, close;
};

static std::string strip_cr(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

static std::vector<Bar> load_csv(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Cannot open " << path << " -- pass the CSV path as argv[1]\n";
        std::exit(1);
    }
    std::string line;
    std::getline(in, line); // header: Epoch,Timestamp,Open,High,Low,Close
    std::vector<Bar> bars;
    while (std::getline(in, line)) {
        line = strip_cr(line);
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string epoch_s, ts, o_s, h_s, l_s, c_s;
        std::getline(ss, epoch_s, ',');
        std::getline(ss, ts, ',');
        std::getline(ss, o_s, ',');
        std::getline(ss, h_s, ',');
        std::getline(ss, l_s, ',');
        std::getline(ss, c_s, ',');
        bars.push_back(Bar{std::stol(epoch_s), ts,
                            std::stod(o_s), std::stod(h_s),
                            std::stod(l_s), std::stod(c_s)});
    }
    return bars;
}

static Price to_ticks(double price) {
    return static_cast<Price>(std::llround(price * 100.0));
}

static std::string time_of_day(const std::string& ts) {
    return ts.substr(11, 5); // "HH:MM"
}

struct WindowStats {
    std::string label, start, end;
    long long total_latency_ns = 0;
    long long order_count = 0;
    double total_range = 0.0;
    long bar_count = 0;
};

static bool in_window(const std::string& hm, const std::string& s, const std::string& e) {
    return hm >= s && hm < e;
}

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "data/NIFTY50_15sec_20260917.csv";
    auto bars = load_csv(path);
    std::cout << "Loaded " << bars.size() << " real 15-second Nifty 50 bars from "
              << path << "\n\n";

    OrderBook book("NIFTY_REPLAY");
    std::mt19937_64 rng(123);
    std::uniform_int_distribution<Qty> qty_dist(10, 200);

    std::vector<WindowStats> windows = {
        {"Opening (09:15-09:30)", "09:15", "09:30"},
        {"Midday calm (12:00-12:15)", "12:00", "12:15"},
        {"Closing (15:15-15:30)", "15:15", "15:30"},
    };

    OrderId next_id = 1;

    for (auto& bar : bars) {
        std::string hm = time_of_day(bar.timestamp);

        // Real path through this bar's actual OHLC range. If the bar
        // closed up, the conventional read is open -> low -> high -> close
        // (dip then rally); if it closed down, open -> high -> low -> close.
        std::vector<double> path = (bar.close >= bar.open)
            ? std::vector<double>{bar.open, bar.low, bar.high, bar.close}
            : std::vector<double>{bar.open, bar.high, bar.low, bar.close};

        // Ambient resting liquidity around this bar's midpoint, three
        // levels each side -- keeps the book populated as the real price
        // drifts across the session, the same way a real book always has
        // some resting depth near the touch.
        Price mid = to_ticks((bar.high + bar.low) / 2.0);
        for (int lvl = 1; lvl <= 3; ++lvl) {
            book.submit_limit(next_id++, Side::Buy, mid - lvl * 5, qty_dist(rng));
            book.submit_limit(next_id++, Side::Sell, mid + lvl * 5, qty_dist(rng));
        }

        long long bar_latency_ns = 0;
        long bar_orders = 0;
        for (size_t i = 1; i < path.size(); ++i) {
            Side side = (path[i] >= path[i - 1]) ? Side::Buy : Side::Sell;
            Price price = to_ticks(path[i]);
            Qty qty = qty_dist(rng);

            auto t0 = Clock::now();
            book.submit_limit(next_id++, side, price, qty);
            auto t1 = Clock::now();

            bar_latency_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            ++bar_orders;
        }

        double range = bar.high - bar.low; // real, from the actual CSV
        for (auto& w : windows) {
            if (in_window(hm, w.start, w.end)) {
                w.total_latency_ns += bar_latency_ns;
                w.order_count += bar_orders;
                w.total_range += range;
                ++w.bar_count;
            }
        }
    }

    std::cout << "=== real trading-day window comparison ===\n";
    std::cout << std::left << std::setw(28) << "window" << std::setw(8) << "bars"
              << std::setw(20) << "avg range (pts)" << "avg latency/order (ns)\n";
    for (auto& w : windows) {
        if (w.bar_count == 0) continue;
        double avg_range = w.total_range / w.bar_count;
        double avg_latency = w.order_count > 0
            ? static_cast<double>(w.total_latency_ns) / w.order_count : 0.0;
        std::cout << std::left << std::setw(28) << w.label << std::setw(8) << w.bar_count
                  << std::setw(20) << std::fixed << std::setprecision(2) << avg_range
                  << std::setprecision(0) << avg_latency << "\n";
    }

    std::cout << "\nfinal book depth: " << book.bid_order_count() << " resting bids, "
              << book.ask_order_count() << " resting asks (built up over the full session)\n";
    return 0;
}