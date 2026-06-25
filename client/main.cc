#include "client/SearchClient.h"
#include <iostream>
#include <muduo/net/EventLoop.h>
#include <pthread.h>
#include <spdlog/spdlog.h>

using namespace std;
using namespace muduo;
using namespace muduo::net;

// 打印帮助信息
void print_help()
{
    cout<<"[/k <keyword> 关键词推荐]"<<endl
        <<"[/s <keyword>   网页搜索]"<<endl
        <<"[/quit           退出  ]"<<endl
        <<"[/help       获取帮助说明]"<<endl;
}

// 输入线程函数：独立线程读取用户输入，避免阻塞网络事件循环
void* inputThreadFunc(void* args)
{
    SearchClient* client = static_cast<SearchClient*>(args);
    string line;
    while (getline(cin, line)) {
        // 去除首尾空白
        size_t start = line.find_first_not_of(" \t");
        if (start == string::npos) {  // 空行，跳过
            continue;
        }
        size_t end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);

        // ---- 命令解析 ----
        if (line == "/quit") {
            client->disconnect();
            break;
        }
        if (line == "/help") {
            print_help();
            cout<<">"<<flush;
            continue;
        }

        // /k <keyword> — 关键词推荐
        if (line.size() >= 3 && line[0] == '/' && line[1] == 'k' && line[2] == ' ') {
            string keyword = line.substr(3);
            if (!keyword.empty()) {
                cout << "[关键词推荐] " << keyword << endl;
                client->send(MsgType::KeywordReq, keyword);
            }
            continue;
        }

        // /s <keyword> — 网页搜索（显式）
        if (line.size() >= 3 && line[0] == '/' && line[1] == 's' && line[2] == ' ') {
            string keyword = line.substr(3);
            if (!keyword.empty()) {
                cout << "[网页搜索] " << keyword << endl;
                client->send(MsgType::PageReq, keyword);
            }
            continue;
        }

        // 默认：直接输入 → 网页搜索
        if (line[0] != '/') {
            cout << "[网页搜索] " << line << endl;
            client->send(MsgType::PageReq, line);
        } else {
            cout << "未知命令: " << line << "（输入 /help 查看帮助）" << endl;
            cout<<">"<<flush;
        }
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    // 创建事件循环（主线程运行）
    EventLoop loop;
    // 解析服务器地址和端口
    InetAddress serverAddr("127.0.0.1", static_cast<uint16_t>(1234));
    // 创建搜索客户端
    SearchClient client(&loop, serverAddr);
    // 连接服务器
    client.connect();

    // 启动输入线程，避免阻塞网络事件循环
    pthread_t tid;
    pthread_create(&tid, NULL, &inputThreadFunc, (void*)&client);

    // 进入事件循环（阻塞，直到 loop->quit() 被调用）
    loop.loop();

    cout << "再见!" << endl;
    return 0;
}
