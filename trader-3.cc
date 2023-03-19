// Copyright 2021 Optiver Asia Pacific Pty. Ltd.
//
// This file is part of Ready Trader Go.
//
//     Ready Trader Go is free software: you can redistribute it and/or
//     modify it under the terms of the GNU Affero General Public License
//     as published by the Free Software Foundation, either version 3 of
//     the License, or (at your option) any later version.
//
//     Ready Trader Go is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU Affero General Public License for more details.
//
//     You should have received a copy of the GNU Affero General Public
//     License along with Ready Trader Go.  If not, see
//     <https://www.gnu.org/licenses/>.

#include <boost/asio/io_context.hpp>

#include <ready_trader_go/logging.h>

#include "trader-3.h"

using namespace ReadyTraderGo;

RTG_INLINE_GLOBAL_LOGGER_WITH_CHANNEL(LG_AT, "AUTO")

constexpr int LOT_SIZE = 20;
constexpr int POSITION_LIMIT = 100;
constexpr int ARBITRAGE_LIMIT = 20;
constexpr int TICK_SIZE_IN_CENTS = 100;
constexpr int MIN_BID_NEARST_TICK = (MINIMUM_BID + TICK_SIZE_IN_CENTS) / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;
constexpr int MAX_ASK_NEAREST_TICK = MAXIMUM_ASK / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;

AutoTrader::AutoTrader(boost::asio::io_context& context) : BaseAutoTrader(context)
{
}

void AutoTrader::DisconnectHandler()
{
    BaseAutoTrader::DisconnectHandler();
    RLOG(LG_AT, LogLevel::LL_INFO) << "execution connection lost";
}

void AutoTrader::ErrorMessageHandler(unsigned long clientOrderId,
                                     const std::string& errorMessage)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "error with order " << clientOrderId << ": " << errorMessage;
    if (clientOrderId != 0 && (mAsks.count(clientOrderId) || mBids.count(clientOrderId)))
    {
        OrderStatusMessageHandler(clientOrderId, 0, 0, 0);
    }
}

bool AutoTrader::checkMessageLimit() {
    double current_time = std::time(nullptr);
    while (orderTimestamps.size() > 0 && orderTimestamps.front() < current_time - 1.01) {
        orderTimestamps.pop_front();
    }
    if (orderTimestamps.size() >= 50) {
        return false;
    }
    orderTimestamps.push_back(current_time);
    return true;
}

bool AutoTrader::sendBidOrder(unsigned long price, long volume, Lifespan lifespanType) {
    if (!checkMessageLimit()) {
        return false;
    }
    
    unsigned long bidId = mNextMessageId++;
    SendInsertOrder(bidId, Side::BUY, price, volume, lifespanType);
    mBids[bidId] = price;
    return true;
}

bool AutoTrader::sendAskOrder(unsigned long price, long volume, Lifespan lifespanType) {
    if (!checkMessageLimit()) {
        return false;
    }
    
    unsigned long askId = mNextMessageId++;
    SendInsertOrder(askId, Side::SELL, price, volume, lifespanType);
    mAsks[askId] = price;
    return true;
}

bool AutoTrader::sendHedgeOrder(unsigned long price, unsigned long volume, Side side) {
    while (!checkMessageLimit()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    unsigned long order_id = mNextMessageId++;
    if (side == Side::BUY) {
        hedgeBid.insert(order_id);
    } else {
        hedgeAsk.insert(order_id);
    }

    SendHedgeOrder(order_id, side, price, volume); // call super SendHedgeOrder function
    return true;
}

bool AutoTrader::sendCancelOrder(unsigned long orderId){
    if (!checkMessageLimit()) {
        return false;
    }

    SendCancelOrder(orderId); // call super SendCancelOrder function
    return true;
}

void AutoTrader::trimOrder(){
    for (auto const& [bid_id, bid] : mBids){
        if (bid > futureAsk){
            sendCancelOrder(bid_id);
        }
    }

    for (auto const& [ask_id, ask] : mAsks){
        if (ask < futureBid){
            sendCancelOrder(ask_id);
        }
    }
}

void AutoTrader::handleArbitrage(const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes){
    if (askPrices[0] < futureBid){
        // arbitrage, buy etf and sell future
        long buy_volume = std::min((long)askVolumes[0], (long)ARBITRAGE_LIMIT - mPosition);
        unsigned long buy_price = askPrices[0];
        if (buy_volume > 0){
            sendBidOrder(buy_price, buy_volume, Lifespan::FILL_AND_KILL);
        }
    }else if (bidPrices[0] > futureAsk){
        // arbitrage, buy future and sell etf
        long sell_volume = std::min((long)bidVolumes[0], (long)ARBITRAGE_LIMIT + mPosition);
        unsigned long sell_price = bidPrices[0];
        if (sell_volume > 0){
            sendAskOrder(sell_price, sell_volume, Lifespan::FILL_AND_KILL);
        }
    }
}

void AutoTrader::clearBook(const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                            const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                            const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                            const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes){
    unsigned long cutoff_ask = askPrices.back();
    unsigned long cutoff_bid = bidPrices.back();
    unsigned long bid_vol = 0;
    unsigned long ask_vol = 0;

    for (int i = 0; i < askVolumes.size(); i++) {
        ask_vol += askVolumes[i];
        if (ask_vol >= 3 * LOT_SIZE) {
            cutoff_ask = askPrices[i];
            break;
        }
    }

    for (int i = 0; i < bidVolumes.size(); i++) {
        bid_vol += bidVolumes[i];
        if (bid_vol >= 3 * LOT_SIZE) {
            cutoff_bid = bidPrices[i];
            break;
        }
    }

    for (auto it = mBids.begin(); it != mBids.end(); it++) {
        if (it->second <= cutoff_bid) {
            sendCancelOrder(it->first);
        }
    }

    for (auto it = mAsks.begin(); it != mAsks.end(); it++) {
        if (it->second >= cutoff_ask) {
            sendCancelOrder(it->first);
        }
    }
}

void AutoTrader::handleMarketMaking(const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes){
    clearBook(askPrices, bidPrices, askVolumes, bidVolumes);
    int max_buy_order = (int)(((long)POSITION_LIMIT - mPosition) / LOT_SIZE) - mBids.size();
    int max_sell_order = (int)((mPosition + (long)POSITION_LIMIT) / LOT_SIZE) - mAsks.size();

    unsigned long max_bid = futureBid - 2 * TICK_SIZE_IN_CENTS;
    unsigned long min_ask = futureAsk + 2 * TICK_SIZE_IN_CENTS;
    unsigned long etf_bid = bidPrices[0];
    unsigned long etf_ask = askPrices[0];

    for (unsigned long i = min_ask; i < etf_ask; i += TICK_SIZE_IN_CENTS) {
        bool found = false;
        for (const auto& pair : mAsks) {
            if (pair.second == i) {
                found = true;
                break;
            }
        }
        
        if (!found && max_sell_order > 0) {
            sendAskOrder(i, LOT_SIZE, Lifespan::GOOD_FOR_DAY);
            max_sell_order -= 1;
        }
    }

    for (unsigned long i = etf_bid; i < max_bid; i += TICK_SIZE_IN_CENTS) {
        bool found = false;
        for (const auto& pair : mBids) {
            if (pair.second == i) {
                found = true;
                break;
            }
        }

        if (!found && max_buy_order > 0) {
            sendBidOrder(i, LOT_SIZE, Lifespan::GOOD_FOR_DAY);
            max_buy_order -= 1;
        }
    }
}

void AutoTrader::HedgeFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "hedge order " << clientOrderId << " filled for " << volume
                                   << " lots at $" << price << " average price in cents";
    
    if (hedgeBid.count(clientOrderId)){
        hedgeBid.erase(clientOrderId);
        delta += volume;
    }else if (hedgeAsk.count(clientOrderId)){
        hedgeAsk.erase(clientOrderId);
        delta -= volume;
    }
}

void AutoTrader::OrderBookMessageHandler(Instrument instrument,
                                         unsigned long sequenceNumber,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
    // RLOG(LG_AT, LogLevel::LL_INFO) << "order book received for " << instrument << " instrument"
    //                                << ": ask prices: " << askPrices[0]
    //                                << "; ask volumes: " << askVolumes[0]
    //                                << "; bid prices: " << bidPrices[0]
    //                                << "; bid volumes: " << bidVolumes[0];
    // discard old seq data
    msgSeq = std::max(msgSeq, sequenceNumber);
    if (sequenceNumber != msgSeq) {
        return;
    }

    // error data, return directly
    if (bidPrices[0] == 0 || askPrices[0] == 0) {
        return;
    }

    if (instrument == Instrument::ETF){
        if (askPrices[0] < futureBid || bidPrices[0] > futureAsk){
            handleArbitrage(askPrices, askVolumes, bidPrices, bidVolumes);
        }else if (askPrices[0] > futureAsk && bidPrices[0] < futureBid){
            // set range for bid and ask and make the market also need to cancel unnecessary orders
            handleMarketMaking(askPrices, askVolumes, bidPrices, bidVolumes);
        }
    }
    
    if (instrument == Instrument::FUTURE){
        futureBid = bidPrices[0];
        futureAsk = askPrices[0];
        trimOrder();
    }
}

void AutoTrader::OrderFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "order " << clientOrderId << " filled for " << volume
                                   << " lots at $" << price << " cents";
    
    if (mBids.count(clientOrderId))
    {
        mPosition += (long)volume;
        delta += (long)volume;
        // SendHedgeOrder(mNextMessageId++, Side::SELL, MIN_BID_NEARST_TICK, volume);
        sendHedgeOrder(MIN_BID_NEARST_TICK, volume, Side::SELL);
    }else if (mAsks.count(clientOrderId))
    {
        mPosition -= (long)volume;
        delta -= (long)volume;
        // SendHedgeOrder(mNextMessageId++, Side::BUY, MAX_ASK_NEAREST_TICK, volume);
        sendHedgeOrder(MAX_ASK_NEAREST_TICK, volume, Side::BUY);
    }
}

void AutoTrader::OrderStatusMessageHandler(unsigned long clientOrderId,
                                           unsigned long fillVolume,
                                           unsigned long remainingVolume,
                                           signed long fees)
{
    if (remainingVolume == 0)
    {
        // if (clientOrderId == mAskId)
        // {
        //     mAskId = 0;
        // }
        // else if (clientOrderId == mBidId)
        // {
        //     mBidId = 0;
        // }

        mAsks.erase(clientOrderId);
        mBids.erase(clientOrderId);
    }
}

void AutoTrader::TradeTicksMessageHandler(Instrument instrument,
                                          unsigned long sequenceNumber,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "trade ticks received for " << instrument << " instrument"
                                   << ": ask prices: " << askPrices[0]
                                   << "; ask volumes: " << askVolumes[0]
                                   << "; bid prices: " << bidPrices[0]
                                   << "; bid volumes: " << bidVolumes[0];
}
