#include "server/SearchServer.h"
#include <muduo/base/Timestamp.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <iostream>

using namespace std;
using namespace muduo;
using namespace muduo::net;
using namespace std::placeholders;
using json = nlohmann::json;

SearchServer::SearchServer(EventLoop* loop, const InetAddress& listenAddr)
    : server_(loop, listenAddr, "SearchServer")
    , codec_(std::bind(&SearchServer::onEntireMessage, this, _1, _2, _3, _4))
{
    // 注册连接回调：当有新客户端连接或断开时调用
    server_.setConnectionCallback(
        std::bind(&SearchServer::onConnection, this, _1));

    // 注册消息回调：使用编解码器处理消息分包
    // 解码器解析出完整消息后，会调用 onEntireMessage
    server_.setMessageCallback(
        std::bind(&TLVCodec::onMessage, &codec_, _1, _2, _3));

    // 设置I/O线程数为4，提高并发处理能力
    server_.setThreadNum(4);
}

void SearchServer::start()
{
    server_.start();
    spdlog::info("SearchServer started on port {}", server_.ipPort());
}

// 处理客户端连接和断开事件
void SearchServer::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected()) {
        // 新客户端连接，加入连接集合
        connections_.insert(conn);
        spdlog::info("{} -> {} is UP, total connections: {}",
                     conn->peerAddress().toIpPort(),
                     conn->localAddress().toIpPort(),
                     connections_.size());

    } else {
        // 客户端断开连接，从集合中移除
        connections_.erase(conn);
        spdlog::info("{} is DOWN, remaining connections: {}",
                     conn->peerAddress().toIpPort(),
                     connections_.size());
    }
}

// 处理完整的字符串消息（广播给所有其他客户端）
void SearchServer::onEntireMessage(const TcpConnectionPtr& conn,
    MsgType type,
    const string& message,
    Timestamp receiveTime)
{
    //服务器接收到客户端的搜索消息，主要处理逻辑
    if(type==MsgType::KeywordReq)
    {
        vector<string> result;
        {
            muduo::MutexLockGuard lock(recommender_mutex_);
            keyword_recommender_.process(message);
            result = keyword_recommender_.get_result();
            keyword_recommender_.clear();
        }

        // json格式化消息
        json responseMsg=json::array();
        for(auto &word:result)
        {
            responseMsg.push_back(word);
        }
        codec_.send(conn, MsgType::KeywordResp, responseMsg.dump());
    }

    if(type==MsgType::PageReq)
    {
        vector<Page> result;
        {
            muduo::MutexLockGuard lock(searcher_mutex_);
            page_searcher_.process(message);
            result = page_searcher_.get_result();
            page_searcher_.clear();
        }

        // json格式化消息
        json responseMsg=json::array();
        for(auto &page:result)
        {
            json pageJson;
            pageJson["id"]=page.id;
            pageJson["title"]=page.title;
            pageJson["link"]=page.link;
            pageJson["abstract"]=page.abstract;
            responseMsg.push_back(pageJson);
        }
        codec_.send(conn,MsgType::PageResp, responseMsg.dump());
    }

}
