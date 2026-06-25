#pragma once

#include "common/codec/TLVCodec.h"
#include "server/recommend/KeywordRecommend.h"
#include "server/search/PageSearcher.h"
#include <muduo/base/Mutex.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/TcpServer.h>
#include <set>

// 搜索服务器主类
class SearchServer {
public:
    SearchServer(muduo::net::EventLoop* loop, const muduo::net::InetAddress& listenAddr);
    void start();

private:
    // 处理连接建立和断开事件
    void onConnection(const muduo::net::TcpConnectionPtr& conn);
    // 处理完整的消息（广播给所有其它连接）
    void onEntireMessage(const muduo::net::TcpConnectionPtr& conn,
        MsgType type,
        const std::string& message,
        muduo::Timestamp receiveTime);

    muduo::net::TcpServer server_;          // TCP服务器
    TLVCodec codec_;                        // 编解码器
    std::set<muduo::net::TcpConnectionPtr> connections_; // 所有活跃连接的集合

    // 业务对象：构造时加载一次，常驻内存，避免每次请求读盘
    KeyWordRecommmender keyword_recommender_;
    PageSearcher        page_searcher_;
    muduo::MutexLock    recommender_mutex_;  // 保护 keyword_recommender_ 的并发访问
    muduo::MutexLock    searcher_mutex_;     // 保护 page_searcher_ 的并发访问
};
