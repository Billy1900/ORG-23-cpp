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
#include <array>

#include <boost/asio/io_context.hpp>

#include <ready_trader_go/logging.h>

#include "trader-2.h"

using namespace ReadyTraderGo;

RTG_INLINE_GLOBAL_LOGGER_WITH_CHANNEL(LG_AT, "AUTO")

constexpr int LOT_SIZE = 10;
constexpr int POSITION_LIMIT = 100;
constexpr int TICK_SIZE_IN_CENTS = 100;
constexpr int MIN_BID_NEARST_TICK = (MINIMUM_BID + TICK_SIZE_IN_CENTS) / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;
constexpr int MAX_ASK_NEAREST_TICK = MAXIMUM_ASK / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;

AutoTrader::AutoTrader(boost::asio::io_context& context) : BaseAutoTrader(context)
{
    std::tie(bid_vol_map, ask_vol_map) = QuoteMaps(risk_factor);
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
    if (clientOrderId != 0 && ((mAsks.count(clientOrderId) == 1) || (mBids.count(clientOrderId) == 1)))
    {
        OrderStatusMessageHandler(clientOrderId, 0, 0, 0);
    }
}

void AutoTrader::HedgeFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "hedge order " << clientOrderId << " filled for " << volume
                                   << " lots at $" << price << " average price in cents";
}

void AutoTrader::OrderBookMessageHandler(Instrument instrument,
                                         unsigned long sequenceNumber,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "order book received for " << instrument << " instrument"
                                   << ": ask prices: " << askPrices[0]
                                   << "; ask volumes: " << askVolumes[0]
                                   << "; bid prices: " << bidPrices[0]
                                   << "; bid volumes: " << bidVolumes[0];

    if (instrument == Instrument::FUTURE)
    {
        unsigned long theo_price = (bidPrices[0]*bidVolumes[0]*l0_w + bidPrices[1]*bidVolumes[1]*l1_w + bidPrices[2]*bidVolumes[2]*l2_w + 
                                    askPrices[0]*askVolumes[0]*l0_w + askPrices[1]*askVolumes[1]*l1_w + askPrices[2]*askVolumes[2]*l2_w) /
                                    (bidVolumes[0]*l0_w + bidVolumes[1]*l1_w + bidVolumes[2]*l2_w + askVolumes[0]*l0_w + askVolumes[1]*l1_w + askVolumes[2]*l2_w);
        
        unsigned long newBidPrice = (bidPrices[0] != 0) ? theo_price - 100 : 0;
        unsigned long newAskPrice = (askPrices[0] != 0) ? theo_price + 100 : 0;

        // If the new quoted price differs from the existing quoted price, cancel the old order.
        if (mAskId != 0 && newAskPrice != 0 && newAskPrice != mAskPrice)
        {
            SendCancelOrder(mAskId);
            mAskId = 0;
        }
        if (mBidId != 0 && newBidPrice != 0 && newBidPrice != mBidPrice)
        {
            SendCancelOrder(mBidId);
            mBidId = 0;
        }
        // Determine bid volume according to current position.
        if (mAskId == 0 && newAskPrice != 0 && mPosition > -POSITION_LIMIT && mAskVolume != 0)
        {
            mAskId = mNextMessageId++;
            mAskPrice = newAskPrice;
            SendInsertOrder(mAskId, Side::SELL, newAskPrice, mAskVolume, Lifespan::GOOD_FOR_DAY); // dynamic mAskVolume will consider market impact or minimize risk
            mAsks.emplace(mAskId);
        }
        if (mBidId == 0 && newBidPrice != 0 && mPosition < POSITION_LIMIT && mBidVolume != 0)
        {
            mBidId = mNextMessageId++;
            mBidPrice = newBidPrice;
            SendInsertOrder(mBidId, Side::BUY, newBidPrice, mBidVolume, Lifespan::GOOD_FOR_DAY);
            mBids.emplace(mBidId);
        }
        
    }
}

void AutoTrader::OrderFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "order " << clientOrderId << " filled for " << volume
                                   << " lots at $" << price << " cents";
    if (mAsks.count(clientOrderId) == 1)
    {
        mPosition -= (long)volume;
        SendHedgeOrder(mNextMessageId++, Side::BUY, MAX_ASK_NEAREST_TICK, volume);
    }
    else if (mBids.count(clientOrderId) == 1)
    {
        mPosition += (long)volume;
        SendHedgeOrder(mNextMessageId++, Side::SELL, MIN_BID_NEARST_TICK, volume);
    }
}

void AutoTrader::OrderStatusMessageHandler(unsigned long clientOrderId,
                                           unsigned long fillVolume,
                                           unsigned long remainingVolume,
                                           signed long fees)
{
    if (remainingVolume == 0)
    {
        if (clientOrderId == mAskId)
        {
            mAskId = 0;
        }
        else if (clientOrderId == mBidId)
        {
            mBidId = 0;
        }

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


void AutoTrader::PositionChangeMessageHandler(int future_position, int etf_position){
    mPosition = etf_position;
    mBidVolume = bid_vol_map[mPosition];
    mAskVolume = ask_vol_map[mPosition];
}

std::pair<std::map<int, int>, std::map<int, int>> AutoTrader::QuoteMaps(unsigned int riskFactor){
    std::map<int, int> bid_vol_map;
    std::map<int, int> ask_vol_map;

    for (int position = -100; position <= 100; ++position) {
        int bid_vol = std::floor(((100 - position - riskFactor) / 2.0)) - std::floor(riskFactor / 2.0);
        bid_vol = (bid_vol < 0) ? 0 : bid_vol;

        int ask_vol = 0;
        if (position < 0) {
            ask_vol = std::floor(((100 - std::abs(position) - riskFactor) / 2.0)) - std::floor(riskFactor / 2.0);
        } else {
            ask_vol = std::floor(((100 + std::abs(position) - riskFactor) / 2.0)) - std::floor(riskFactor / 2.0);
        }
        ask_vol = (ask_vol < 0) ? 0 : ask_vol;

        bid_vol_map[position] = bid_vol;
        ask_vol_map[position] = ask_vol;
    }

    return std::make_pair(bid_vol_map, ask_vol_map);
}
