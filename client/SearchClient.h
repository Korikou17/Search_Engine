#pragma once // 防止头文件重复包含

#include "common/codec/TLVCodec.h"
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpConnection.h>

class SearchClient {
public:
    SearchClient(muduo::net::EventLoop* loop,
        const muduo::net::InetAddress& serverAddr);

    void connect(); // 连接服务器
    void disconnect(); // 断开连接
    void send(MsgType type, const std::string& message); // 发送消息

private:
    void onConnection(const muduo::net::TcpConnectionPtr& conn); // 连接状态变化回调
    void onEntireMessage(const muduo::net::TcpConnectionPtr&, // 收到完整消息回调
        MsgType type,
        const std::string& message,
        muduo::Timestamp);

private:
    muduo::net::TcpClient client_; // TCP客户端
    muduo::net::EventLoop* loop_; // 事件循环指针
    muduo::net::TcpConnectionPtr connection_; // 当前连接
    TLVCodec codec_; // 编解码器
};
