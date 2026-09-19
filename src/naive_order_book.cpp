#include "naive_order_book.hpp"
#include <algorithm>
#include <utility>

namespace lob
{

    NaiveOrderBook::NaiveOrderBook(std::string symbol) : symbol_(std::move(symbol)) {}

    int NaiveOrderBook::best_index_for_buy(const std::vector<Order> &asks, Price limit)
    {
        int best = -1;
        for (size_t i = 0; i < asks.size(); ++i)
        {
            if (asks[i].price > limit)
                continue; // buyer won't pay this much
            if (best == -1)
            {
                best = static_cast<int>(i);
                continue;
            }
            const Order &cur = asks[best];
            const Order &cand = asks[i];
            // lower price wins; tie -> earlier seq (time priority) wins
            if (cand.price < cur.price ||
                (cand.price == cur.price && cand.seq < cur.seq))
            {
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    int NaiveOrderBook::best_index_for_sell(const std::vector<Order> &bids, Price limit)
    {
        int best = -1;
        for (size_t i = 0; i < bids.size(); ++i)
        {
            if (bids[i].price < limit)
                continue; // seller won't accept this little
            if (best == -1)
            {
                best = static_cast<int>(i);
                continue;
            }
            const Order &cur = bids[best];
            const Order &cand = bids[i];
            // higher price wins; tie -> earlier seq wins
            if (cand.price > cur.price ||
                (cand.price == cur.price && cand.seq < cur.seq))
            {
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    void NaiveOrderBook::match_buy(Order &incoming, std::vector<Trade> &trades)
    {
        while (incoming.qty > 0)
        {
            int idx = best_index_for_buy(asks_, incoming.price); // O(n) every step
            if (idx == -1)
                break;
            Order &resting = asks_[static_cast<size_t>(idx)];

            Qty traded = std::min(incoming.qty, resting.qty);
            trades.push_back(Trade{resting.id, incoming.id, resting.price, traded, next_seq_++});
            incoming.qty -= traded;
            resting.qty -= traded;

            if (resting.qty == 0)
            {
                asks_.erase(asks_.begin() + idx); // O(n) shift
            }
        }
    }

    void NaiveOrderBook::match_sell(Order &incoming, std::vector<Trade> &trades)
    {
        while (incoming.qty > 0)
        {
            int idx = best_index_for_sell(bids_, incoming.price);
            if (idx == -1)
                break;
            Order &resting = bids_[static_cast<size_t>(idx)];

            Qty traded = std::min(incoming.qty, resting.qty);
            trades.push_back(Trade{resting.id, incoming.id, resting.price, traded, next_seq_++});
            incoming.qty -= traded;
            resting.qty -= traded;

            if (resting.qty == 0)
            {
                bids_.erase(bids_.begin() + idx);
            }
        }
    }

    SubmitResult NaiveOrderBook::submit_limit(OrderId id, Side side, Price price, Qty qty)
    {
        Order incoming{id, side, price, qty, next_seq_++};
        std::vector<Trade> trades;

        if (side == Side::Buy)
            match_buy(incoming, trades);
        else
            match_sell(incoming, trades);

        SubmitResult result;
        result.trades = std::move(trades);
        result.remaining_qty = incoming.qty;

        if (incoming.qty > 0)
        {
            if (side == Side::Buy)
                bids_.push_back(incoming);
            else
                asks_.push_back(incoming);
            result.resting = true;
        }
        return result;
    }

    bool NaiveOrderBook::cancel(OrderId id)
    {
        for (auto *v : {&bids_, &asks_})
        {
            auto it = std::find_if(v->begin(), v->end(),
                                   [id](const Order &o)
                                   { return o.id == id; });
            if (it != v->end())
            {
                v->erase(it); // O(n) scan + shift
                return true;
            }
        }
        return false;
    }

    std::optional<Price> NaiveOrderBook::best_bid() const
    {
        if (bids_.empty())
            return std::nullopt;
        Price best = bids_[0].price;
        for (auto &o : bids_)
            best = std::max(best, o.price);
        return best;
    }

    std::optional<Price> NaiveOrderBook::best_ask() const
    {
        if (asks_.empty())
            return std::nullopt;
        Price best = asks_[0].price;
        for (auto &o : asks_)
            best = std::min(best, o.price);
        return best;
    }

} // namespace lob