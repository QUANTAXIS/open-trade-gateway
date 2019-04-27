/////////////////////////////////////////////////////////////////////////
///@file config.cpp
///@brief	配置文件读写
///@copyright	上海信易信息科技股份有限公司 版权所有 
/////////////////////////////////////////////////////////////////////////

#include "config.h"
#include "log.h"
#include "utility.h"
#include "SerializerTradeBase.h"
#include "rapid_serialize.h"

#include <iostream>
#include <boost/filesystem.hpp>

using namespace std;

Config g_config;

class SerializerConfig
    : public RapidSerialize::Serializer<SerializerConfig>
{
public:
    using RapidSerialize::Serializer<SerializerConfig>::Serializer;

    void DefineStruct(Config& d)
    {
        AddItem(d.host, "host");
        AddItem(d.port, "port");
        AddItem(d.user_file_path, "user_file_path");
        AddItem(d.auto_confirm_settlement, "auto_confirm_settlement");
    }

    void DefineStruct(BrokerConfig& d)
    {
        AddItem(d.broker_name, "name");
        AddItem(d.broker_type, "type");
        AddItem(d.ctp_broker_id, "broker_id");
        AddItem(d.trading_fronts, "trading_fronts");
        AddItem(d.product_info, "product_info");
        AddItem(d.auth_code, "auth_code");
    }
};



bool LoadConfig()
{
    g_config.trading_day = GuessTradingDay();

    Log(LOG_INFO, NULL,"trading_day:%s", g_config.trading_day.c_str());

    SerializerConfig ss;
    if (!ss.FromFile("/etc/open-trade-gateway/config.json"))
    {
        Log(LOG_FATAL, NULL, "load /etc/open-trade-gateway/config.json file fail");
        return false;
    }
    ss.ToVar(g_config);

    SerializerConfig ss_broker;
    if (!ss_broker.FromFile("/etc/open-trade-gateway/broker_list.json"))
    {
        Log(LOG_FATAL, NULL, "load /etc/open-trade-gateway/broker_list.json file fail");
        return false;
    }
    std::vector<BrokerConfig> broker_list;
    ss_broker.ToVar(broker_list);

    SerializerTradeBase ss_broker_list_str;
    rapidjson::Pointer("/aid").Set(*ss_broker_list_str.m_doc,"rtn_brokers");

    boost::filesystem::path ufpath(g_config.user_file_path);
    for (long long i = 0; i < broker_list.size(); ++i) 
    {
        BrokerConfig& broker = broker_list[i];

        g_config.brokers[broker.broker_name] = broker;

		if (!boost::filesystem::exists(ufpath/broker.broker_name))
		{
			boost::filesystem::create_directory(ufpath / broker.broker_name);
		}		

        rapidjson::Pointer("/brokers/" + std::to_string(i)).Set(*ss_broker_list_str.m_doc,broker.broker_name);
    }
    ss_broker_list_str.ToString(&g_config.broker_list_str);
    return true;
}

Config::Config()
{
    //配置参数默认值
    port = 7788;
    auto_confirm_settlement = false;
}
