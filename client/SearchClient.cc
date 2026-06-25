#include "client/SearchClient.h"
#include <iostream>
#include <muduo/net/EventLoop.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

using namespace std;
using namespace std::placeholders;
using namespace muduo;
using namespace muduo::net;
using json = nlohmann::json;

SearchClient::SearchClient(EventLoop* loop, const InetAddress& serverAddr)
    : client_(loop, serverAddr, "SearchClient") // 初始化TCP客户端
    , loop_(loop)
    , codec_(std::bind(&SearchClient::onEntireMessage, this, _1, _2, _3, _4))
{
    // 注册连接回调：当与服务器的连接建立或断开时调用
    client_.setConnectionCallback(
        std::bind(&SearchClient::onConnection, this, _1));
    // 注册消息回调：使用编解码器处理接收到的数据
    client_.setMessageCallback(
        std::bind(&TLVCodec::onMessage, &codec_, _1, _2, _3));
}

void SearchClient::connect()
{
    client_.connect(); // 发起连接到服务器
}

void SearchClient::disconnect()
{
    client_.disconnect(); // 断开与服务器的连接
}

void SearchClient::send(MsgType type,const string& message)
{
    if (connection_) {
        codec_.send(connection_, type ,message); // 通过编解码器发送消息
    }
}

void SearchClient::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected()) {
        // 连接建立成功，保存连接指针
        connection_ = conn;
        spdlog::info("Connected to server: {}", conn->peerAddress().toIpPort());
        cout << "=== Connected to search server ===" << endl;
        cout<<"'/w': 关键字推荐  '/p': 网页搜索 '/quit': 退出" <<endl;
    } else {
        // 连接断开，重置连接指针
        connection_.reset();
        spdlog::info("Disconnected from server");
        cout << "=== Disconnected from server ===" << endl;
        // 退出事件循环，程序结束
        loop_->quit();
    }
}

// 收到服务器广播的消息时的回调
void SearchClient::onEntireMessage(const TcpConnectionPtr& conn,
    MsgType type,
    const string& message,
    Timestamp receiveTime)
{
    switch (type) {
        case MsgType::CommonMsg:
            cout << message << endl;
            break;
        case MsgType::KeywordResp:
        case MsgType::PageResp: {
            json data = json::parse(message);
            cout << data.dump() << endl;
            break;
        }
        default:
            break;
    }
}
