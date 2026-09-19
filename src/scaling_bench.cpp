// scaling_bench.cpp
//
// Two things:
//   1. A correctness cross-check: replay the same order flow through both
//      OrderBook and NaiveOrderBook and assert every trade matches. If this
//      fails, the "naive vs optimized" comparison below is meaningless --
//      you'd be comparing two different algorithms, not two implementations
//      of the same one.
//   2. The scaling benchmark: pre-load both books with N resting sell
//      orders (huge quantity each, so they never get exhausted), then time
//      how long a single matching buy order takes, at increasing N.
//      Optimized should be roughly flat; naive should grow with N, because
//      finding the best price is an O(n) scan.

#include "order_book.hpp"
#include "naive_order_book.hpp"
#include <iostream>
#include <random>
#include <cassert>
#include <iomanip>

using namespace lob;

static void correctness_cross_check()
{
    std::cout << "=== correctness cross-check ===\n";
    OrderBook fast("X");
    NaiveOrderBook naive("X");

    struct Op
    {
        OrderId id;
        Side side;
        Price price;
        Qty qty;
    };
    std::vector<Op> ops = {
        {1, Side::Sell, 10050, 100},
        {2, Side::Sell, 10040, 50},
        {3, Side::Sell, 10040, 30},
        {4, Side::Buy, 10060, 90},  // should match 2 then part of 3
        {5, Side::Buy, 10050, 200}, // should match rest of 3, then 1
        {6, Side::Sell, 10070, 40},
        {7, Side::Buy, 10070, 40},
    };

    bool all_match = true;
    for (auto &op : ops)
    {
        auto r_fast = fast.submit_limit(op.id, op.side, op.price, op.qty);
        auto r_naive = naive.submit_limit(op.id, op.side, op.price, op.qty);

        if (r_fast.trades.size() != r_naive.trades.size() ||
            r_fast.remaining_qty != r_naive.remaining_qty)
        {
            all_match = false;
            std::cout << "  MISMATCH on order " << op.id
                      << ": fast trades=" << r_fast.trades.size()
                      << " naive trades=" << r_naive.trades.size() << "\n";
            continue;
        }
        for (size_t i = 0; i < r_fast.trades.size(); ++i)
        {
            if (r_fast.trades[i].price != r_naive.trades[i].price ||
                r_fast.trades[i].qty != r_naive.trades[i].qty)
            {
                all_match = false;
                std::cout << "  MISMATCH in trade content on order " << op.id << "\n";
            }
        }
    }
    std::cout << (all_match ? "  PASS -- both implementations agree on every trade\n\n"
                            : "  FAIL -- see mismatches above\n\n");
    if (!all_match)
    {
        std::cerr << "Aborting: naive and optimized books disagree, "
                     "the scaling comparison below would be meaningless.\n";
        std::exit(1);
    }
}

template <typename Book>
static long long time_one_match(Book &book, OrderId id, Price aggressive_price)
{
    auto t0 = Clock::now();
    book.submit_limit(id, Side::Buy, aggressive_price, /*qty=*/1);
    auto t1 = Clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
}

static void scaling_benchmark()
{
    std::cout << "=== scaling benchmark: matching latency vs resting book depth ===\n";
    std::cout << std::left << std::setw(12) << "depth"
              << std::setw(18) << "optimized (ns)"
              << std::setw(18) << "naive (ns)"
              << "speedup\n";

    std::vector<int> depths = {1000, 5000, 20000, 50000, 100000, 200000};
    const int trials = 300;
    const Price base_price = 10000;
    const Price aggressive_price = 1'000'000; // always crosses, whatever the depth

    for (int depth : depths)
    {
        OrderBook fast("BENCH");
        NaiveOrderBook naive("BENCH");

        // Load identical resting supply into both books. Huge qty means a
        // qty=1 aggressor never exhausts a level, so depth stays constant
        // across all trials -- we're isolating "find the best price" cost.
        std::mt19937_64 rng(7);
        std::uniform_int_distribution<Price> spread(0, depth);
        for (int i = 0; i < depth; ++i)
        {
            Price p = base_price + spread(rng);
            fast.submit_limit(100000 + i, Side::Sell, p, 1'000'000'000);
            naive.submit_limit(100000 + i, Side::Sell, p, 1'000'000'000);
        }

        long long fast_total = 0, naive_total = 0;
        OrderId next_id = 900000;
        for (int t = 0; t < trials; ++t)
        {
            fast_total += time_one_match(fast, next_id, aggressive_price);
            naive_total += time_one_match(naive, next_id, aggressive_price);
            ++next_id;
        }

        double fast_avg = static_cast<double>(fast_total) / trials;
        double naive_avg = static_cast<double>(naive_total) / trials;

        std::cout << std::left << std::setw(12) << depth
                  << std::setw(18) << std::fixed << std::setprecision(0) << fast_avg
                  << std::setw(18) << naive_avg
                  << std::setprecision(1) << (naive_avg / fast_avg) << "x\n";
    }
}

int main()
{
    correctness_cross_check();
    scaling_benchmark();
    return 0;
}