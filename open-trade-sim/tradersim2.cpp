/////////////////////////////////////////////////////////////////////////
///@file tradersim2.cpp
///@brief	Sim交易逻辑实现
///@copyright	上海信易信息科技股份有限公司 版权所有
/////////////////////////////////////////////////////////////////////////

#include "tradersim.h"
#include "log.h"
#include "rapid_serialize.h"
#include "numset.h"
#include "types.h"
#include "utility.h"
#include "config.h"
#include "types.h"
#include "ins_list.h"
#include "datetime.h"
#include "SerializerTradeBase.h"

using namespace std::chrono;

void SerializerSim::DefineStruct(ActionOrder& d)
{
	AddItem(d.aid, "aid");
	AddItem(d.order_id, "order_id");
	AddItem(d.user_id, "user_id");
	AddItem(d.exchange_id, "exchange_id");
	AddItem(d.ins_id, "instrument_id");
	AddItemEnum(d.direction, "direction", {
		{ kDirectionBuy, "BUY" },
		{ kDirectionSell, "SELL" },
		});
	AddItemEnum(d.offset, "offset", {
		{ kOffsetOpen, "OPEN" },
		{ kOffsetClose, "CLOSE" },
		{ kOffsetCloseToday, "CLOSETODAY" },
		});
	AddItemEnum(d.price_type, "price_type", {
		{ kPriceTypeLimit, "LIMIT" },
		{ kPriceTypeAny, "ANY" },
		{ kPriceTypeBest, "BEST" },
		{ kPriceTypeFiveLevel, "FIVELEVEL" },
		});
	AddItemEnum(d.volume_condition, "volume_condition", {
		{ kOrderVolumeConditionAny, "ANY" },
		{ kOrderVolumeConditionMin, "MIN" },
		{ kOrderVolumeConditionAll, "ALL" },
		});
	AddItemEnum(d.time_condition, "time_condition", {
		{ kOrderTimeConditionIOC, "IOC" },
		{ kOrderTimeConditionGFS, "GFS" },
		{ kOrderTimeConditionGFD, "GFD" },
		{ kOrderTimeConditionGTD, "GTD" },
		{ kOrderTimeConditionGTC, "GTC" },
		{ kOrderTimeConditionGFA, "GFA" },
		});
	AddItem(d.volume, "volume");
	AddItem(d.limit_price, "limit_price");
}

void SerializerSim::DefineStruct(ActionTransfer& d)
{
	AddItem(d.currency, "currency");
	AddItem(d.amount, "amount");
}

void tradersim::OnClientReqInsertOrder(ActionOrder action_insert_order)
{
	std::string symbol = action_insert_order.exchange_id + "." + action_insert_order.ins_id;
	if (action_insert_order.order_id.empty())
	{
		action_insert_order.order_id = 
			std::to_string(duration_cast<milliseconds>
			(steady_clock::now().time_since_epoch()).count());
	}
		
	std::string order_key = action_insert_order.order_id;
	auto it = m_data.m_orders.find(order_key);
	if (it != m_data.m_orders.end()) 
	{
		OutputNotifyAllSycn(1
			,u8"下单, 已被服务器拒绝,原因:单号重复", "WARNING");
		return;
	}

	m_something_changed = true;
	const Instrument* ins = GetInstrument(symbol);
	Order* order = &(m_data.m_orders[order_key]);
	order->user_id = action_insert_order.user_id;
	order->exchange_id = action_insert_order.exchange_id;
	order->instrument_id = action_insert_order.ins_id;
	order->order_id = action_insert_order.order_id;
	order->exchange_order_id = order->order_id;
	order->direction = action_insert_order.direction;
	order->offset = action_insert_order.offset;
	order->price_type = action_insert_order.price_type;
	order->limit_price = action_insert_order.limit_price;
	order->volume_orign = action_insert_order.volume;
	order->volume_left = action_insert_order.volume;
	order->status = kOrderStatusAlive;
	order->volume_condition = action_insert_order.volume_condition;
	order->time_condition = action_insert_order.time_condition;
	order->insert_date_time = GetLocalEpochNano();
	order->seqno = m_last_seq_no++;
	if (action_insert_order.user_id.substr(0, m_user_id.size()) != m_user_id) 
	{
		OutputNotifyAllSycn(1
			,u8"下单, 已被服务器拒绝, 原因:下单指令中的用户名错误", "WARNING");
		order->status = kOrderStatusFinished;
		return;
	}
	if (!ins) 
	{
		OutputNotifyAllSycn(1
			,u8"下单, 已被服务器拒绝, 原因:合约不合法", "WARNING");
		order->status = kOrderStatusFinished;
		return;
	}
	if (ins->product_class != kProductClassFutures) 
	{
		OutputNotifyAllSycn(1
			,u8"下单, 已被服务器拒绝, 原因:模拟交易只支持期货合约", "WARNING");
		order->status = kOrderStatusFinished;
		return;
	}
	if (action_insert_order.volume <= 0) 
	{
		OutputNotifyAllSycn(1
			,u8"下单, 已被服务器拒绝, 原因:下单手数应该大于0", "WARNING");
		order->status = kOrderStatusFinished;
		return;
	}
	double xs = action_insert_order.limit_price / ins->price_tick;
	if (xs - int(xs + 0.5) >= 0.001) 
	{
		OutputNotifyAllSycn(1
			,u8"下单, 已被服务器拒绝, 原因:下单价格不是价格单位的整倍数", "WARNING");
		order->status = kOrderStatusFinished;
		return;
	}
	Position* position = &(m_data.m_positions[symbol]);
	position->ins = ins;
	position->instrument_id = order->instrument_id;
	position->exchange_id = order->exchange_id;
	position->user_id = m_user_id;
	if (action_insert_order.offset == kOffsetOpen) 
	{
		if (position->ins->margin * action_insert_order.volume > m_account->available) 
		{
			OutputNotifyAllSycn(1
				,u8"下单, 已被服务器拒绝, 原因:开仓保证金不足", "WARNING");
			order->status = kOrderStatusFinished;
			return;
		}
	}
	else 
	{
		if ((action_insert_order.direction == kDirectionBuy && position->volume_short < action_insert_order.volume + position->volume_short_frozen_today)
			|| (action_insert_order.direction == kDirectionSell && position->volume_long < action_insert_order.volume + position->volume_long_frozen_today)) 
		{
			OutputNotifyAllSycn(1
				,u8"下单, 已被服务器拒绝, 原因:平仓手数超过持仓量", "WARNING");
			order->status = kOrderStatusFinished;
			return;
		}
	}
	m_alive_order_set.insert(order);
	UpdateOrder(order);
	SaveUserDataFile();
	OutputNotifyAllSycn(1, u8"下单成功");
	return;
}

void tradersim::UpdateOrder(Order* order)
{
	order->seqno = m_last_seq_no++;
	order->changed = true;
	Position& position = GetPosition(order->symbol());
	assert(position.ins);
	UpdatePositionVolume(&position);
}

Position& tradersim::GetPosition(const std::string symbol)
{
	Position& position = m_data.m_positions[symbol];
	return position;
}

void tradersim::UpdatePositionVolume(Position* position)
{
	position->frozen_margin = 0;
	position->volume_long_frozen_today = 0;
	position->volume_short_frozen_today = 0;
	for (auto it_order = m_alive_order_set.begin()
		; it_order != m_alive_order_set.end()
		; ++it_order) 
	{
		Order* order = *it_order;
		if (order->status != kOrderStatusAlive)
			continue;
		if (position->instrument_id != order->instrument_id)
			continue;
		if (order->offset == kOffsetOpen) 
		{
			position->frozen_margin += position->ins->margin * order->volume_left;
		}
		else 
		{
			if (order->direction == kDirectionBuy)
				position->volume_short_frozen_today += order->volume_left;
			else
				position->volume_long_frozen_today += order->volume_left;
		}
	}
	position->volume_long_frozen = position->volume_long_frozen_his + position->volume_long_frozen_today;
	position->volume_short_frozen = position->volume_short_frozen_his + position->volume_short_frozen_today;
	position->volume_long = position->volume_long_his + position->volume_long_today;
	position->volume_short = position->volume_short_his + position->volume_short_today;
	position->margin_long = position->ins->margin * position->volume_long;
	position->margin_short = position->ins->margin * position->volume_short;
	position->margin = position->margin_long + position->margin_short;
	if (position->volume_long > 0) 
	{
		position->open_price_long = position->open_cost_long / (position->volume_long * position->ins->volume_multiple);
		position->position_price_long = position->position_cost_long / (position->volume_long * position->ins->volume_multiple);
	}
	if (position->volume_short > 0) 
	{
		position->open_price_short = position->open_cost_short / (position->volume_short * position->ins->volume_multiple);
		position->position_price_short = position->position_cost_short / (position->volume_short * position->ins->volume_multiple);
	}
	position->changed = true;
}

void tradersim::OnClientReqCancelOrder(ActionOrder action_cancel_order)
{
	if (action_cancel_order.user_id.substr(0, m_user_id.size()) != m_user_id)
	{
		OutputNotifyAllSycn(1
			,u8"撤单, 已被服务器拒绝, 原因:撤单指令中的用户名错误", "WARNING");
		return;
	}
	for (auto it_order = m_alive_order_set.begin(); it_order != m_alive_order_set.end(); ++it_order) 
	{
		Order* order = *it_order;
		if (order->order_id == action_cancel_order.order_id
			&& order->status == kOrderStatusAlive) 
		{
			order->status = kOrderStatusFinished;
			UpdateOrder(order);
			m_something_changed = true;
			OutputNotifyAllSycn(1, u8"撤单成功");
			return;
		}
	}
	OutputNotifyAllSycn(1,u8"要撤销的单不存在","WARNING");
	return;
}

TransferLog& tradersim::GetTransferLog(const std::string& seq_id)
{
	return m_data.m_transfers[seq_id];
}

void tradersim::OnClientReqTransfer(ActionTransfer action_transfer)
{
	if (action_transfer.amount > 0) 
	{
		m_account->deposit += action_transfer.amount;
	}
	else 
	{
		m_account->withdraw -= action_transfer.amount;
	}
	m_account->static_balance += action_transfer.amount;
	m_account->changed = true;

	m_transfer_seq++;
	TransferLog& d = GetTransferLog(std::to_string(m_transfer_seq));
	d.currency = action_transfer.currency;
	d.amount = action_transfer.amount;
	boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
	DateTime dt;
	dt.date.year = now.date().year();
	dt.date.month = now.date().month();
	dt.date.day = now.date().day();
	dt.time.hour = now.time_of_day().hours();
	dt.time.minute = now.time_of_day().minutes();
	dt.time.second = now.time_of_day().seconds();
	dt.time.microsecond = 0;
	d.datetime = DateTimeToEpochNano(&dt);
	d.error_id = 0;
	d.error_msg = u8"正确";

	m_something_changed = true;
	OutputNotifyAllSycn(0, u8"转账成功");

	SendUserData();
}

void tradersim::SendUserDataImd(int connectId)
{
	//构建数据包		
	SerializerTradeBase nss;
	nss.dump_all = true;
	rapidjson::Pointer("/aid").Set(*nss.m_doc, "rtn_data");
	rapidjson::Value node_data;
	nss.FromVar(m_data, &node_data);
	rapidjson::Value node_user_id;
	node_user_id.SetString(m_user_id, nss.m_doc->GetAllocator());
	rapidjson::Value node_user;
	node_user.SetObject();
	node_user.AddMember(node_user_id, node_data, nss.m_doc->GetAllocator());
	rapidjson::Pointer("/data/0/trade").Set(*nss.m_doc, node_user);
	std::string json_str;
	nss.ToString(&json_str);
	//发送	
	std::shared_ptr<std::string> msg_ptr(new std::string(json_str));
	SendMsg(connectId,msg_ptr);
}

void tradersim::SendUserData()
{
	if (!m_peeking_message)
		return;
	//尝试匹配成交
	TryOrderMatch();
	//重算所有持仓项的持仓盈亏和浮动盈亏
	double total_position_profit = 0;
	double total_float_profit = 0;
	double total_margin = 0;
	double total_frozen_margin = 0.0;
	for (auto it = m_data.m_positions.begin();
		it != m_data.m_positions.end(); ++it) 
	{
		const std::string& symbol = it->first;
		Position& ps = it->second;
		if (!ps.ins)
			ps.ins = GetInstrument(symbol);
		if (!ps.ins) 
		{
			Log(LOG_ERROR,"msg=miss symbol %s when processing position", symbol);
			continue;
		}
		double last_price = ps.ins->last_price;
		if (!IsValid(last_price))
			last_price = ps.ins->pre_settlement;
		if ((IsValid(last_price) && (last_price != ps.last_price)) || ps.changed) {
			ps.last_price = last_price;
			ps.position_profit_long = ps.last_price * ps.volume_long * ps.ins->volume_multiple - ps.position_cost_long;
			ps.position_profit_short = ps.position_cost_short - ps.last_price * ps.volume_short * ps.ins->volume_multiple;
			ps.position_profit = ps.position_profit_long + ps.position_profit_short;
			ps.float_profit_long = ps.last_price * ps.volume_long * ps.ins->volume_multiple - ps.open_cost_long;
			ps.float_profit_short = ps.open_cost_short - ps.last_price * ps.volume_short * ps.ins->volume_multiple;
			ps.float_profit = ps.float_profit_long + ps.float_profit_short;
			ps.changed = true;
			m_something_changed = true;
		}
		if (IsValid(ps.position_profit))
			total_position_profit += ps.position_profit;
		if (IsValid(ps.float_profit))
			total_float_profit += ps.float_profit;
		if (IsValid(ps.margin))
			total_margin += ps.margin;
		total_frozen_margin += ps.frozen_margin;
	}
	//重算资金账户
	if (m_something_changed)
	{
		m_account->position_profit = total_position_profit;
		m_account->float_profit = total_float_profit;
		m_account->balance = m_account->static_balance + m_account->float_profit + m_account->close_profit - m_account->commission;
		m_account->margin = total_margin;
		m_account->frozen_margin = total_frozen_margin;
		m_account->available = m_account->balance - m_account->margin - m_account->frozen_margin;
		if (IsValid(m_account->available) && IsValid(m_account->balance) && !IsZero(m_account->balance))
			m_account->risk_ratio = 1.0 - m_account->available / m_account->balance;
		else
			m_account->risk_ratio = NAN;
		m_account->changed = true;
	}
	if (!m_something_changed)
		return;
	//构建数据包
	m_data.m_trade_more_data = false;
	SerializerTradeBase nss;
	rapidjson::Pointer("/aid").Set(*nss.m_doc, "rtn_data");
	rapidjson::Value node_data;
	nss.FromVar(m_data, &node_data);
	rapidjson::Value node_user_id;
	node_user_id.SetString(m_user_id, nss.m_doc->GetAllocator());
	rapidjson::Value node_user;
	node_user.SetObject();
	node_user.AddMember(node_user_id, node_data, nss.m_doc->GetAllocator());
	rapidjson::Pointer("/data/0/trade").Set(*nss.m_doc, node_user);
	std::string json_str;
	nss.ToString(&json_str);
	//发送
	std::shared_ptr<std::string> msg_ptr(new std::string(json_str));
	SendMsgAll(msg_ptr);
	m_something_changed = false;
	m_peeking_message = false;
}

void tradersim::TryOrderMatch()
{
	for (auto it_order = m_alive_order_set.begin(); it_order != m_alive_order_set.end(); ) 
	{
		Order* order = *it_order;
		if (order->status == kOrderStatusFinished)
		{
			it_order = m_alive_order_set.erase(it_order);
			continue;
		}
		else 
		{
			CheckOrderTrade(order);
			++it_order;
		}
	}
}

void tradersim::CheckOrderTrade(Order* order)
{
	auto ins = GetInstrument(order->symbol());
	if (order->price_type == kPriceTypeLimit) 
	{
		if (order->limit_price - 0.0001 > ins->upper_limit) 
		{
			OutputNotifyAllSycn(1
				,u8"下单,已被服务器拒绝,原因:已撤单报单被拒绝价格超出涨停板", "WARNING");
			order->status = kOrderStatusFinished;
			UpdateOrder(order);
			return;
		}
		if (order->limit_price + 0.0001 < ins->lower_limit) 
		{
			OutputNotifyAllSycn(1
				,u8"下单,已被服务器拒绝,原因:已撤单报单被拒绝价格跌破跌停板"
				,"WARNING");
			order->status = kOrderStatusFinished;
			UpdateOrder(order);
			return;
		}
	}
	if (order->direction == kDirectionBuy && IsValid(ins->ask_price1)
		&& (order->price_type == kPriceTypeAny || order->limit_price >= ins->ask_price1))
		DoTrade(order, order->volume_left, ins->ask_price1);
	if (order->direction == kDirectionSell && IsValid(ins->bid_price1)
		&& (order->price_type == kPriceTypeAny || order->limit_price <= ins->bid_price1))
		DoTrade(order, order->volume_left, ins->bid_price1);
}

void tradersim::DoTrade(Order* order, int volume, double price)
{
	//创建成交记录
	std::string trade_id = std::to_string(m_last_seq_no++);
	Trade* trade = &(m_data.m_trades[trade_id]);
	trade->trade_id = trade_id;
	trade->seqno = m_last_seq_no++;
	trade->user_id = order->user_id;
	trade->order_id = order->order_id;
	trade->exchange_trade_id = trade_id;
	trade->exchange_id = order->exchange_id;
	trade->instrument_id = order->instrument_id;
	trade->direction = order->direction;
	trade->offset = order->offset;
	trade->volume = volume;
	trade->price = price;
	trade->trade_date_time = GetLocalEpochNano();

	//生成成交通知
	std::stringstream ss;
	ss << u8"成交通知,合约:" << trade->exchange_id
		<< u8"." << trade->instrument_id << u8",手数:" << trade->volume<< "!";
	OutputNotifyAllSycn(1,ss.str().c_str());

	//调整委托单数据
	assert(volume <= order->volume_left);
	assert(order->status == kOrderStatusAlive);
	order->volume_left -= volume;
	if (order->volume_left == 0) 
	{
		order->status = kOrderStatusFinished;
	}
	order->seqno = m_last_seq_no++;
	order->changed = true;
	//调整持仓数据
	Position* position = &(m_data.m_positions[order->symbol()]);
	double commission = position->ins->commission * volume;
	trade->commission = commission;
	if (order->offset == kOffsetOpen) 
	{
		if (order->direction == kDirectionBuy)
		{
			position->volume_long_today += volume;
			position->open_cost_long += price * volume * position->ins->volume_multiple;
			position->position_cost_long += price * volume * position->ins->volume_multiple;
			position->open_price_long = position->open_cost_long / (position->volume_long * position->ins->volume_multiple);
			position->position_price_long = position->open_cost_long / (position->volume_long * position->ins->volume_multiple);
		}
		else 
		{
			position->volume_short_today += volume;
			position->open_cost_short += price * volume * position->ins->volume_multiple;
			position->position_cost_short += price * volume * position->ins->volume_multiple;
			position->open_price_short = position->open_cost_short / (position->volume_short * position->ins->volume_multiple);
			position->position_price_short = position->open_cost_short / (position->volume_short * position->ins->volume_multiple);
		}
	}
	else 
	{
		double close_profit = 0;
		if (order->direction == kDirectionBuy) 
		{
			position->open_cost_short = position->open_cost_short * (position->volume_short - volume) / position->volume_short;
			position->position_cost_short = position->position_cost_short * (position->volume_short - volume) / position->volume_short;
			close_profit = (position->position_price_short - price) * volume * position->ins->volume_multiple;
			position->volume_short_today -= volume;
		}
		else 
		{
			position->open_cost_long = position->open_cost_long * (position->volume_long - volume) / position->volume_long;
			position->position_cost_long = position->position_cost_long * (position->volume_long - volume) / position->volume_long;
			close_profit = (price - position->position_price_long) * volume * position->ins->volume_multiple;
			position->volume_long_today -= volume;
		}
		m_account->close_profit += close_profit;
	}
	//调整账户资金
	m_account->commission += commission;
	UpdatePositionVolume(position);
}

void tradersim::OnClientPeekMessage()
{
	m_peeking_message = true;
	SendUserData();
}