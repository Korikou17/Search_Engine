#pragma once
#include <string>
#include <set>
#include <map>
#include <queue>

using namespace std;

class KeyWordRecommmender {
public:
    KeyWordRecommmender();                              // 构造时加载词典和索引到内存
    void process(const string& message);
    vector<string> get_result();
    void clear();                                  // 清空请求级中间数据
private:
    void create_candidate_id(const string& message);
    void create_candidate_word(const string& message);
private:
    // 预加载的词典数据（常驻内存，不在每次请求中读盘）
    map<string, set<int>> index_cn_;       // 字符 → 行号列表
    map<string, int>      dict_cn_;        // 词 → 词频
    vector<string>        words_by_line_;  // 行号 → 词

    set<int> candidate_index_;             // 每次请求的候选索引
    vector<string> result_;                // 每次请求的结果
};