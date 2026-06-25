#pragma once
#include <string>
#include <set>
#include <map>
#include <vector>
#include "cppjieba/Jieba.hpp"

using namespace std;

struct Page{
    int id;
    string title;
    string link;
    string abstract;
};

class PageSearcher {
public:

    PageSearcher();

    void process(const string& message);
    vector<Page> get_result();
    void clear();                      // 清空请求级中间数据

private:
    void find_doc(const string& message);

    void fetch_top_pages();                             // 读取 Top-5 页面内容 → result_
    string generate_kwic_abstract(const string& content); // P3: 关键词上下文摘要

private:
    cppjieba::Jieba tokenizer_;
    
    map<string, map<int, double>> inverted_index_;
    map<int, pair<int, int>> offsets_;
    set<string> stopWords_;
    set<string> query_keywords_;         // P3: 当前查询关键词，供摘要生成使用

    vector<pair<int, double>> ranked_;   // 排序后的 (doc_id, score) 列表
    vector<Page> result_;
};