#include "client/SearchClient.h"
#include <iostream>
#include <muduo/net/EventLoop.h>
#include <pthread.h>
#include <spdlog/spdlog.h>

using namespace std;
using namespace muduo;
using namespace muduo::net;

// 输入线程函数：独立线程读取用户输入
void* inputThreadFunc(void* args)
{
    SearchClient* client = static_cast<SearchClient*>(args);
    string line;
    while (getline(cin, line)) { // 阻塞等待用户输入
        if (line == "/quit") {
            client->disconnect(); // 用户输入退出命令
            break;
        }
        else if (line == "/w") {
            string req;
            cout<<"请输入中文:"<<endl;
            getline(cin, req);
            if(!req.empty())
                client->send(MsgType::KeywordReq,req); // 发送用户输入的消息
        }
        else if (line == "/p") {
            string req;
            cout<<"请输入中文:"<<endl;
            getline(cin, req);
            if(!req.empty())
                client->send(MsgType::PageReq,req); // 发送用户输入的消息
        }
        else{
            cout<<"无效输入"<<endl;
        }
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    // if (argc < 3) {
    //     cout << "Usage: " << argv[0] << " <server_ip> <port>" << endl;
    //     return 1;
    // }
    // 创建事件循环（主线程运行）
    EventLoop loop;
    // 解析服务器地址和端口
    //InetAddress serverAddr(argv[1], static_cast<uint16_t>(atoi(argv[2])));
    InetAddress serverAddr("127.0.0.1", static_cast<uint16_t>(1234));
    // 创建搜索客户端
    SearchClient client(&loop, serverAddr);
    // 连接服务器
    client.connect();

    // 启动输入线程，避免阻塞网络事件循环
    pthread_t tid;
    pthread_create(&tid, NULL, &inputThreadFunc, (void*)&client);

    // 进入事件循环（阻塞，直到loop->quit()被调用）
    loop.loop();

    return 0;
}
