// main.cpp
//
// Two things happen here:
//   1. A small correctness demo (a handful of orders, printed trades)
//      so you can eyeball that matching logic is doing the right thing.
//   2. A latency benchmark: fire N random limit orders at the book and
//      measure the wall-clock time of each submit_limit() call in
//      nanoseconds, then report p50 / p99 / p99.9 and throughput.
//
// This benchmark is single-threaded and includes no I/O in the timed
// region -- that's intentional. You are measuring the matching engine's
// own cost, not disk/network noise. Phase 2 (real tick data replay) will
// add I/O in a *separate*, unmeasured stage for exactly this reason.

#include "order_book.hpp"
#include <iostream>
#include <random>
#include <algorithm>
#include <iomanip>

using namespace lob;

static void run_correctness_demo() {
    std::cout << "=== correctness demo ===\n";
    OrderBook book("NIFTY_DEMO");

    // Two resting sells, then a buy that should walk both.
    book.submit_limit(1, Side::Sell, 10050, 100);  // 100.50, qty 100
    book.submit_limit(2, Side::Sell, 10060, 50);    // 100.60, qty 50

    auto res = book.submit_limit(3, Side::Buy, 10060, 120);
    std::cout << "incoming buy id=3 qty=120 -> " << res.trades.size()
              << " trade(s), remaining=" << res.remaining_qty << "\n";
    for (auto& t : res.trades) {
        std::cout << "  trade: resting=" << t.resting_id
                  << " incoming=" << t.incoming_id
                  << " price=" << t.price / 100.0
                  << " qty=" << t.qty << "\n";
    }
    std::cout << "best_ask after: "
              << (book.best_ask() ? std::to_string(*book.best_ask() / 100.0) : "none")
              << "\n\n";
}

static void run_latency_benchmark(size_t n_orders) {
    std::cout << "=== latency benchmark (" << n_orders << " orders) ===\n";
    OrderBook book("NIFTY_BENCH");

    std::mt19937_64 rng(42); // fixed seed: reproducible numbers for the README
    std::uniform_int_distribution<int64_t> price_dist(9900, 10100); // ticks around 100.00
    std::uniform_int_distribution<int64_t> qty_dist(1, 500);
    std::bernoulli_distribution side_dist(0.5);

    std::vector<long long> latencies_ns;
    latencies_ns.reserve(n_orders);

    for (OrderId id = 1; id <= n_orders; ++id) {
        Side side = side_dist(rng) ? Side::Buy : Side::Sell;
        Price price = price_dist(rng);
        Qty qty = qty_dist(rng);

        auto t0 = Clock::now();
        book.submit_limit(id, side, price, qty);
        auto t1 = Clock::now();

        latencies_ns.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    std::sort(latencies_ns.begin(), latencies_ns.end());
    auto pct = [&](double p) {
        size_t idx = static_cast<size_t>(p * (latencies_ns.size() - 1));
        return latencies_ns[idx];
    };

    long long total_ns = 0;
    for (auto v : latencies_ns) total_ns += v;
    double mean_ns = static_cast<double>(total_ns) / latencies_ns.size();
    double throughput = 1e9 / mean_ns;

    std::cout << std::fixed << std::setprecision(0);
    std::cout << "  mean:   " << mean_ns << " ns\n";
    std::cout << "  p50:    " << pct(0.50) << " ns\n";
    std::cout << "  p99:    " << pct(0.99) << " ns\n";
    std::cout << "  p99.9:  " << pct(0.999) << " ns\n";
    std::cout << "  max:    " << latencies_ns.back() << " ns\n";
    std::cout << std::setprecision(0)
              << "  throughput: ~" << throughput << " orders/sec (single thread)\n\n";
    std::cout << "  final book: " << book.bid_order_count() << " resting bids, "
              << book.ask_order_count() << " resting asks\n";
}

int main(int argc, char** argv) {
    run_correctness_demo();
    size_t n = 1'000'000;
    if (argc > 1) n = std::stoull(argv[1]);
    run_latency_benchmark(n);
    return 0;
}