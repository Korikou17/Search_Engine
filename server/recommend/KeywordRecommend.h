#pragma once
#include <string>
#include <set>
#include <map>
#include <queue>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

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
    
    // ========== 优化：混合推荐算法 ==========
    // 阶段1：多路召回（字符重叠 + 倒排索引共现 + 前缀匹配）
    void recall_by_char_overlap(const string& message);
    void recall_by_cooccurrence(const string& message);
    void recall_by_prefix(const string& message);
    // 阶段2：混合精排
    double compute_hybrid_score(const string& query, const string& candidate,
                                int freq, const set<int>* candidate_docs);
    // 工具方法
    static double jaccard_similarity(const set<int>& a, const set<int>& b);
    static int    edit_distance(const string& a, const string& b);
    // Trie 构建
    void build_prefix_trie();

private:
    // ---- 预加载的词典数据（常驻内存） ----
    map<string, set<int>> index_cn_;          // 字符 → 行号列表
    map<string, int>      dict_cn_;           // 词 → 词频
    vector<string>        words_by_line_;     // 行号 → 词

    // ---- 优化：加载文档倒排索引用于共现计算 ----
    // keyword → set<doc_id>（仅文档ID集合，用于 Jaccard 计算）
    unordered_map<string, set<int>> keyword_docs_;

    // 最大词频（用于归一化）
    int max_freq_ = 1;

    // ---- 前缀 Trie 树（用于路径3：前缀匹配召回） ----
    struct TrieNode {
        unordered_map<string, unique_ptr<TrieNode>> children;  // UTF-8字符 → 子节点
        set<int> line_numbers;  // 以当前节点为前缀的所有词的行号
    };
    unique_ptr<TrieNode> trie_root_;

    // ---- 请求级中间数据 ----
    set<int> candidate_index_;                // 候选词行号集合
    unordered_set<string> candidate_set_;     // 候选词去重集合
    vector<string> result_;                   // 最终推荐结果
};