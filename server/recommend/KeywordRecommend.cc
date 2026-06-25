#include "server/recommend/KeywordRecommend.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <utfcpp/utf8.h>
#include <spdlog/spdlog.h>

static const int K_NUM=5;       //返回的推荐词的最大个数
static const int MAX_DIST = 3;  //编辑距离剪枝阈值

KeyWordRecommmender::KeyWordRecommmender()
{
    // 加载字符倒排索引（常驻内存）
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

    // 加载中文词典（常驻内存）
    ifstream dict_ifs{"data/dict_cn.dat"};
    if (!dict_ifs) { spdlog::error("打开词典文件失败: data/dict_cn.dat"); return; }
    while (getline(dict_ifs, line)) {
        istringstream iss(line);
        string word;
        int fre;
        iss >> word >> fre;
        dict_cn_[word] = fre;
        words_by_line_.push_back(word);
    }
    dict_ifs.close();
    spdlog::info("词典加载完成，共 {} 个词", dict_cn_.size());
}

struct CandWord
{
    string word;
    int frequence;
    int edit_distance;

    // priority_queue 默认大顶堆，返回 true 表示 this 优先级低于 rhs
    bool operator<(const CandWord& rhs) const
    {
        if (edit_distance != rhs.edit_distance)
            return edit_distance > rhs.edit_distance;  // 编辑距离小的优先
        if (frequence != rhs.frequence)
            return frequence < rhs.frequence;          // 词频高的优先
        return word > rhs.word;                        // 字典序小的优先
    }
};

void KeyWordRecommmender::create_candidate_id(const string& message)
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

static int edit_distance(const string& a, const string& b)
{
    auto va = utf8_to_chars(a);
    auto vb = utf8_to_chars(b);
    int m = va.size(), n = vb.size();

    // 长度剪枝：编辑距离至少等于长度差，超过阈值直接返回
    if (abs(m - n) > MAX_DIST)
        return MAX_DIST + 1;

    // 让 m 为较长串，n 为较短串，节省空间
    if (m < n) { swap(va, vb); swap(m, n); }

    // 空间优化：两行一维数组代替完整二维矩阵，O(n) 空间
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

void KeyWordRecommmender::create_candidate_word(const string& message)
{

    priority_queue<CandWord> candidate_CandWord;

    for(auto &num:candidate_index_)
    {
        CandWord cword;
        cword.word=words_by_line_[num-1];
        cword.frequence = dict_cn_[cword.word];
        cword.edit_distance = edit_distance(message, cword.word);
        // 编辑距离在阈值内才入队
        if (cword.edit_distance <= MAX_DIST)
        {
            candidate_CandWord.push(cword);
        }
    }

    // 剔除多余候选词，仅保留前 K_NUM 个（最优的在堆顶，逐个取出放入结果集）
    int count = 0;
    while (!candidate_CandWord.empty() && count < K_NUM) {
        result_.push_back(candidate_CandWord.top().word);
        candidate_CandWord.pop();
        ++count;
    }
}


void KeyWordRecommmender::process(const string& message)
{
    create_candidate_id(message);
    create_candidate_word(message);
}

vector<string> KeyWordRecommmender::get_result()
{
    return result_;
}

void KeyWordRecommmender::clear()
{
    candidate_index_.clear();
    result_.clear();
}