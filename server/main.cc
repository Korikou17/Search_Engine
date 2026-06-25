#include "server/SearchServer.h"
#include <iostream>
#include <memory>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace std;
using namespace muduo;
using namespace muduo::net;

void init_logger()
{
    // 同时输出到控制台(带颜色) 和 文件
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("server.log", true);

    vector<spdlog::sink_ptr> sinks {console_sink, file_sink};
    auto logger = make_shared<spdlog::logger>("search_engine", sinks.begin(), sinks.end());
    
    logger->set_level(spdlog::level::debug);   // debug 级别以上都输出
    logger->flush_on(spdlog::level::info);     // info 级别以上立即刷新到磁盘
    spdlog::set_default_logger(logger);
}

int main(int argc, char* argv[])
{

    // if (argc < 2) {
    //     cout << "Usage: " << argv[0] << " <port>" << endl;
    //     return 1;
    // }

    //spdlog初始化
    init_logger();

    // 创建事件循环
    EventLoop loop;
    // 设置监听地址和端口
    //uint16_t port = static_cast<uint16_t>(atoi(argv[1]));
    //InetAddress serverAddr(port);
    InetAddress serverAddr((uint16_t)1234);
    // 创建搜索服务器
    SearchServer server(&loop, serverAddr);
    // 启动服务器
    server.start();
    // 进入事件循环
    loop.loop();

    return 0;
}
