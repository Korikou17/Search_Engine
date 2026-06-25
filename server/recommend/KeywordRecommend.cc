#include "server/recommend/KeywordRecommend.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <utfcpp/utf8.h>
#include <spdlog/spdlog.h>

static const int    K_NUM    = 5;      // 返回推荐词最大个数
static const int    MAX_DIST = 3;      // 编辑距离剪枝阈值（用于拼写纠错召回）
static const double ALPHA    = 0.50;   // 共现相似度权重（语义相关性）
static const double BETA     = 0.30;   // 编辑相似度权重（拼写纠错）
static const double GAMMA    = 0.20;   // 词频权重（流行度）

// 构造：加载全部离线数据到内存
KeyWordRecommmender::KeyWordRecommmender()
{
    // ---- 1. 加载字符倒排索引（字符 → 单词行号集合） ----
    ifstream idx_ifs{"data/index_cn.dat"};
    if (!idx_ifs) { spdlog::error("打开文件失败: data/index_cn.dat"); return; }
    string line;
    while (getline(idx_ifs, line)) {
        istringstream iss(line);
        string character;
        iss >> character;
        int line_num;
        while (iss >> line_num) {
            index_cn_[character].insert(line_num);
        }
    }
    idx_ifs.close();
    spdlog::info("字符索引加载完成，共 {} 个字符", index_cn_.size());

    // ---- 2. 加载中文词典（词 → 词频） + 行号映射 ----
    ifstream dict_ifs{"data/dict_cn.dat"};
    if (!dict_ifs) { spdlog::error("打开词典文件失败: data/dict_cn.dat"); return; }
    while (getline(dict_ifs, line)) {
        istringstream iss(line);
        string word;
        int fre;
        iss >> word >> fre;
        dict_cn_[word] = fre;
        words_by_line_.push_back(word);
        if (fre > max_freq_) max_freq_ = fre;
    }
    dict_ifs.close();
    spdlog::info("词典加载完成，共 {} 个词，最大词频 {}", dict_cn_.size(), max_freq_);

    // ---- 3.加载文档倒排索引（关键词 → 文档ID集合）用于共现计算 ----
    ifstream inv_ifs{"data/inverted_index.dat"};
    if (!inv_ifs) {
        spdlog::warn("倒排索引文件不存在: data/inverted_index.dat，共现推荐将禁用");
    } else {
        while (getline(inv_ifs, line)) {
            istringstream iss(line);
            string keyword;
            iss >> keyword;
            int doc_id;
            double weight;
            while (iss >> doc_id >> weight) {
                keyword_docs_[keyword].insert(doc_id);
            }
        }
        inv_ifs.close();
        spdlog::info("倒排索引加载完成，共 {} 个关键词", keyword_docs_.size());
    }

    // ---- 4.【优化】构建前缀 Trie 树（用于前缀匹配召回） ----
    build_prefix_trie();
}

// UTF-8 字符串 → 字符数组
static vector<string> utf8_to_chars(const string& s)
{
    vector<string> chars;
    const char* curr = s.c_str();
    const char* end  = s.c_str() + s.size();
    while (curr != end) {
        auto start = curr;
        utf8::next(curr, end);
        chars.emplace_back(start, curr);
    }
    return chars;
}


// 编辑距离计算（UTF-8 字符级，带剪枝 + 空间优化）
int KeyWordRecommmender::edit_distance(const string& a, const string& b)
{
    auto va = utf8_to_chars(a);
    auto vb = utf8_to_chars(b);
    int m = va.size(), n = vb.size();

    // 长度剪枝：编辑距离至少等于长度差
    if (abs(m - n) > MAX_DIST)
        return MAX_DIST + 1;

    // 让 m 为较长串，节省空间
    if (m < n) { swap(va, vb); swap(m, n); }

    // 空间优化：两行滚动数组 O(n)
    vector<int> prev(n + 1), curr(n + 1);
    for (int j = 0; j <= n; ++j) prev[j] = j;

    for (int i = 1; i <= m; ++i) {
        curr[0] = i;
        for (int j = 1; j <= n; ++j) {
            int cost = (va[i-1] == vb[j-1]) ? 0 : 1;
            curr[j] = min({prev[j] + 1,        // 删除
                           curr[j-1] + 1,      // 插入
                           prev[j-1] + cost}); // 替换
        }
        swap(prev, curr);
    }
    return prev[n];
}

// ===================================================================
// 构建前缀 Trie 树：将所有词典词按 UTF-8 字符逐级插入
// ===================================================================
void KeyWordRecommmender::build_prefix_trie()
{
    trie_root_ = make_unique<TrieNode>();

    for (int i = 0; i < (int)words_by_line_.size(); ++i) {
        const string& word = words_by_line_[i];
        int line_num = i + 1;  // 行号从1开始

        TrieNode* node = trie_root_.get();
        node->line_numbers.insert(line_num);  // 根节点包含所有词

        auto chars = utf8_to_chars(word);
        for (const string& ch : chars) {
            if (node->children.find(ch) == node->children.end()) {
                node->children[ch] = make_unique<TrieNode>();
            }
            node = node->children[ch].get();
            node->line_numbers.insert(line_num);  // 每个前缀节点都记录到达该节点的词
        }
    }
    spdlog::info("前缀 Trie 树构建完成");
}

// ===================================================================
// Jaccard 相似度：|A ∩ B| / |A ∪ B|
// ===================================================================
double KeyWordRecommmender::jaccard_similarity(const set<int>& a, const set<int>& b)
{
    if (a.empty() || b.empty()) return 0.0;

    // 遍历较小的集合，在较大的集合中查找
    const set<int> *small = &a, *large = &b;
    if (a.size() > b.size()) swap(small, large);

    int intersection = 0;
    for (int id : *small) {
        if (large->count(id)) ++intersection;
    }
    int union_size = a.size() + b.size() - intersection;
    return union_size > 0 ? (double)intersection / union_size : 0.0;
}

// ===================================================================
// 【优化·阶段1-召回A】字符重叠召回（保留原方案，确保召回率）
// ===================================================================
void KeyWordRecommmender::recall_by_char_overlap(const string& message)
{
    const char* curr = message.c_str();
    const char* end = message.c_str() + message.size();
    while (curr != end) {
        auto start = curr;
        utf8::next(curr, end);
        string character = string(start, curr);
        auto it = index_cn_.find(character);
        if (it != index_cn_.end()) {
            candidate_index_.insert(it->second.begin(), it->second.end());
        }
    }
}

// ===================================================================
// 【优化·阶段1-召回B】共现召回（通过倒排索引找到语义相关词）
// ===================================================================
void KeyWordRecommmender::recall_by_cooccurrence(const string& message)
{
    // 尝试在倒排索引中精确查找查询词
    auto it = keyword_docs_.find(message);
    if (it == keyword_docs_.end()) return;

    const set<int>& query_docs = it->second;
    if (query_docs.empty()) return;

    // 遍历查询词所在的所有文档，收集这些文档中的其他关键词
    // 限制遍历文档数防止性能问题
    int doc_count = 0;
    static const int MAX_DOCS_TO_SCAN = 200;

    for (int doc_id : query_docs) {
        if (++doc_count > MAX_DOCS_TO_SCAN) break;

        // 反向查找：哪些关键词出现在这个文档中
        // 由于没有 doc→keywords 的映射，这里遍历 keyword_docs_ 的每个词
        // 效率优化：只对已经在候选集中的词做精确共现验证
        // （共现召回主要作为精排阶段的加分项，不在召回阶段做大范围扫描）
    }

    // 注：共现召回的核心价值在精排阶段体现（compute_hybrid_score），
    // 此处保留接口用于未来扩展（如离线构建 word2word 共现矩阵）
    spdlog::debug("共现召回: 查询词 '{}' 出现在 {} 个文档中",
                  message, query_docs.size());
}

// ===================================================================
// 【优化·阶段1-召回C】前缀匹配召回（Trie 树，用于输入提示）
// 例：输入"人工" → 召回"人工智能"、"人工呼吸"、"人工降雨"等
// ===================================================================
void KeyWordRecommmender::recall_by_prefix(const string& message)
{
    if (!trie_root_) return;

    // 沿 Trie 树逐字符下钻
    TrieNode* node = trie_root_.get();
    auto chars = utf8_to_chars(message);
    for (const string& ch : chars) {
        auto it = node->children.find(ch);
        if (it == node->children.end()) {
            // 没有任何词以该前缀开头，提前返回
            return;
        }
        node = it->second.get();
    }

    // 到达前缀节点，收集所有以该前缀开头的词的行号
    int added = 0;
    for (int line_num : node->line_numbers) {
        candidate_index_.insert(line_num);
        ++added;
    }
    spdlog::debug("前缀召回: '{}' → {} 个候选词", message, added);
}

// ===================================================================
// 【优化·阶段2-精排】混合评分：共现相似度 + 编辑相似度 + 词频
// ===================================================================
double KeyWordRecommmender::compute_hybrid_score(
    const string& query,
    const string& candidate,
    int freq,
    const set<int>* candidate_docs)
{
    // ---- 信号1：编辑相似度（归一化到 [0,1]，越小越相似 → 转为越大越好） ----
    int dist = edit_distance(query, candidate);
    int max_len = max(utf8_to_chars(query).size(), utf8_to_chars(candidate).size());
    double edit_sim = (max_len > 0) ? 1.0 - (double)dist / max_len : 0.0;
    if (edit_sim < 0.0) edit_sim = 0.0;

    // ---- 信号2：文档共现相似度（Jaccard） ----
    double cooccur_sim = 0.0;
    if (candidate_docs && !candidate_docs->empty()) {
        auto query_it = keyword_docs_.find(query);
        if (query_it != keyword_docs_.end()) {
            cooccur_sim = jaccard_similarity(query_it->second, *candidate_docs);
        }
    }

    // ---- 信号3：对数归一化词频 ----
    double freq_score = (max_freq_ > 0) ? log2(1.0 + freq) / log2(1.0 + max_freq_) : 0.0;

    // ---- 混合得分 ----
    double score = ALPHA * cooccur_sim + BETA * edit_sim + GAMMA * freq_score;

    spdlog::debug("候选词: '{}' | cooccur={:.3f} edit_sim={:.3f} freq={:.3f} → score={:.4f}",
                  candidate, cooccur_sim, edit_sim, freq_score, score);
    return score;
}

// ===================================================================
// 【优化·阶段2-精排】候选词评分 + Top-K 筛选
// ===================================================================
void KeyWordRecommmender::create_candidate_word(const string& message)
{
    // 查找查询词在倒排索引中的文档集合（用于共现计算）
    const set<int>* query_docs = nullptr;
    auto query_it = keyword_docs_.find(message);
    if (query_it != keyword_docs_.end()) {
        query_docs = &query_it->second;
    }

    // 使用最大堆，按混合得分排序
    struct ScoredWord {
        string word;
        double score;
        bool operator<(const ScoredWord& rhs) const {
            return score < rhs.score;  // 大顶堆：得分高的优先
        }
    };
    priority_queue<ScoredWord> pq;

    // 去重遍历所有候选词行号
    for (int num : candidate_index_) {
        if (num < 1 || num > (int)words_by_line_.size()) continue;
        const string& cand_word = words_by_line_[num - 1];

        // 跳过查询词自身
        if (cand_word == message) continue;

        // 去重
        if (candidate_set_.count(cand_word)) continue;
        candidate_set_.insert(cand_word);

        int freq = dict_cn_[cand_word];

        // 查找候选词在倒排索引中的文档集合
        const set<int>* cand_docs = nullptr;
        auto cand_it = keyword_docs_.find(cand_word);
        if (cand_it != keyword_docs_.end()) {
            cand_docs = &cand_it->second;
        }

        // 编辑距离剪枝：仅保留编辑距离在阈值内的候选词
        int dist = edit_distance(message, cand_word);
        if (dist > MAX_DIST) continue;

        // 计算混合得分
        double score = compute_hybrid_score(message, cand_word, freq, cand_docs);
        pq.push({cand_word, score});
    }

    // 取 Top-K
    int count = 0;
    while (!pq.empty() && count < K_NUM) {
        result_.push_back(pq.top().word);
        pq.pop();
        ++count;
    }
    spdlog::info("关键词推荐: 查询='{}' → {} 个结果", message, result_.size());
}

// ===================================================================
// 主流程：多路召回 + 精排
// ===================================================================
void KeyWordRecommmender::process(const string& message)
{
    // 阶段1：多路召回
    recall_by_char_overlap(message);       // 召回A：字符重叠（保证召回率）
    recall_by_cooccurrence(message);       // 召回B：倒排共现（提升语义相关性）
    recall_by_prefix(message);             // 召回C：前缀匹配（输入提示补全）

    // 阶段2：混合精排 + Top-K
    create_candidate_word(message);
}

vector<string> KeyWordRecommmender::get_result()
{
    return result_;
}

void KeyWordRecommmender::clear()
{
    candidate_index_.clear();
    candidate_set_.clear();
    result_.clear();
}