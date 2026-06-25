#include "server/search/PageSearcher.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <tinyxml2.h>
#include <utfcpp/utf8.h>
#include <spdlog/spdlog.h>

static const int TOP_K = 5;

PageSearcher::PageSearcher()
{
    //加载停用词
    ifstream ifs{"stopwords/cn_stopwords.txt"};
    if(!ifs){spdlog::error("打开停用词文件失败: stopwords/cn_stopwords.txt");return;}
    string word;
    while(ifs>>word)
    {
        stopWords_.emplace(word);
    }
    ifs.close();

    //加载倒排索引
    ifstream ifs2{"data/inverted_index.dat"};
    if(!ifs2){ spdlog::error("打开倒排索引失败"); return; }
    string line;
    while(getline(ifs2, line)) {
        istringstream iss(line);
        string keyword;
        iss >> keyword;
        int doc_id;
        double weight;
        while(iss >> doc_id >> weight) {
            inverted_index_[keyword][doc_id] = weight;
        }
    }
    spdlog::info("倒排索引加载完成，共 {} 个关键词", inverted_index_.size());
    ifs2.close();

    //加载网页偏移库
    ifstream ifs3{"data/offsets.dat"};
    if(!ifs3){ spdlog::error("打开网页偏移库失败"); return; }
    int doc_id;
    int offset;
    int length;
    while(ifs3 >> doc_id >> offset >> length) {
        offsets_[doc_id] = {offset, length};
    }
    spdlog::info("网页偏移库加载完成");
    ifs3.close();
}

void PageSearcher::find_doc(const string& message)
{
    set<string> keywords;
    vector<string> words;
    tokenizer_.Cut(message,words);
    for(auto &word:words)
    {
        // 跳过空字符串和纯空白字符
        if(word.empty()) continue;
        if(word.find_first_not_of(" \t\n\r\f\v") == string::npos) continue;
        //跳过停用词
        if (stopWords_.find(word)!=stopWords_.end()){
                continue;
        }
        keywords.emplace(word);
        spdlog::info("查询关键词: {}", word);
    }
    if(keywords.empty()){
        spdlog::info("没有有效关键词");
        return;
    }

    // ========== P1: 渐进式放宽检索 ==========
    // 一轮遍历：统计每个文档的关键词命中数 + BM25 总分
    map<int, int>    doc_match_count;  // doc_id → 命中关键词数
    map<int, double> doc_score;        // doc_id → BM25 总分
    int total_kw = 0;                  // 实际在索引中的关键词数

    for(auto &kw : keywords) {
        auto it = inverted_index_.find(kw);
        if(it == inverted_index_.end()) {
            spdlog::debug("关键词 '{}' 不在索引中，跳过", kw);
            continue;  // 跳过未知关键词（不再直接返回空）
        }
        ++total_kw;
        for(auto &[doc_id, weight] : it->second) {
            doc_match_count[doc_id]++;
            doc_score[doc_id] += weight;
        }
    }

    if(total_kw == 0) {
        spdlog::info("所有关键词均不在索引中，无结果");
        return;
    }

    spdlog::info("命中关键词: {}/{}，文档候选池: {}", 
                 total_kw, keywords.size(), doc_score.size());

    // 渐进式筛选：AND → 60%匹配 → OR
    auto apply_filter = [&](int min_match) {
        ranked_.clear();
        for(auto &[doc_id, score] : doc_score) {
            if(doc_match_count[doc_id] >= min_match) {
                ranked_.push_back({doc_id, score});
            }
        }
    };

    // 第1轮：严格 AND（所有关键词必须命中）
    apply_filter(total_kw);
    if((int)ranked_.size() >= TOP_K) {
        spdlog::info("第1轮 AND: {} 个结果", ranked_.size());
        goto sort_and_return;
    }

    // 第2轮：≥60% 关键词命中
    {
        int min_match = max(1, (total_kw * 60 + 99) / 100);  // ceil(60%)
        if(min_match < total_kw) {  // 不重复 AND
            apply_filter(min_match);
            spdlog::info("第2轮 ≥{}% 匹配({}/{}): {} 个结果",
                         60, min_match, total_kw, ranked_.size());
        }
    }
    if((int)ranked_.size() >= TOP_K) goto sort_and_return;

    // 第3轮：OR（命中任意关键词即可）
    apply_filter(1);
    spdlog::info("第3轮 OR: {} 个结果", ranked_.size());

sort_and_return:
    sort(ranked_.begin(), ranked_.end(),
         [](auto &a, auto &b) { return a.second > b.second; });
    query_keywords_ = keywords;  // P3: 保存查询关键词，供摘要生成
}

void PageSearcher::fetch_top_pages()
{
    ifstream page_ifs{"data/pages.dat"};
    if(!page_ifs){ spdlog::error("打开pages.dat失败"); return; }

    int limit = min(TOP_K, (int)ranked_.size());
    for(int i = 0; i < limit; ++i) {
        int doc_id = ranked_[i].first;
        auto [offset, length] = offsets_[doc_id];

        spdlog::info("返回的文档id:{}", doc_id);
        
        page_ifs.seekg(offset);
        string doc_str(length, '\0');
        page_ifs.read(&doc_str[0], length);

        // 用 tinyxml2 解析 XML
        tinyxml2::XMLDocument xdoc;
        if(xdoc.Parse(doc_str.c_str()) != tinyxml2::XML_SUCCESS){
            spdlog::error("XML建立失败");
            continue;
        }
        tinyxml2::XMLElement* root = xdoc.RootElement();
        if(!root) {spdlog::error("XML根节点建立失败"); continue;}

        // 提取 title
        tinyxml2::XMLElement* titleElem = root->FirstChildElement("title");
        string title = titleElem ? titleElem->GetText() : "";
        // 提取 link
        tinyxml2::XMLElement* linkElem = root->FirstChildElement("link");
        string link = linkElem ? linkElem->GetText() : "";
        // 提取 content → P3: 生成关键词上下文摘要
        tinyxml2::XMLElement* contentElem = root->FirstChildElement("content");
        string content = contentElem ? contentElem->GetText() : "";
        string abstract = generate_kwic_abstract(content);

        Page page;
        page.id       = doc_id;
        page.title    = std::move(title);
        page.link     = std::move(link);
        page.abstract = std::move(abstract);
        result_.push_back(page);
    }
}

void PageSearcher::process(const string& message)
{
    find_doc(message);
    fetch_top_pages();
}

void PageSearcher::clear()
{
    ranked_.clear();
    result_.clear();
    query_keywords_.clear();
}

// ===================================================================
// P3: 关键词上下文摘要（KWIC - Keyword in Context）
// 在正文中定位查询关键词，取命中位置前后各 25 个 UTF-8 字符作为摘要
// 多个命中位置窗口重叠时自动合并
// ===================================================================
string PageSearcher::generate_kwic_abstract(const string& content)
{
    const int HALF = 25;  // 命中位置前后各 25 个 UTF-8 字符

    if (content.empty() || query_keywords_.empty()) {
        return "";
    }

    // ---- 1. 构建 UTF-8 字符 → 字节偏移 映射 ----
    vector<size_t> char_offsets;  // char_offsets[i] = 第 i 个 UTF-8 字符的起始字节位置
    const char* curr = content.c_str();
    const char* end  = content.c_str() + content.size();
    while (curr != end) {
        char_offsets.push_back(curr - content.c_str());
        utf8::next(curr, end);
    }
    int total_chars = char_offsets.size();
    if (total_chars == 0) return "";

    // ---- 2. 找到所有关键词的命中位置（字节偏移 → UTF-8 字符下标） ----
    struct Interval {
        int begin, end;  // UTF-8 字符下标区间 [begin, end)
        int kw_hits;     // 该区间覆盖的关键词命中次数
    };
    vector<Interval> intervals;

    for (auto& kw : query_keywords_) {
        if (kw.empty()) continue;
        size_t byte_pos = 0;
        while ((byte_pos = content.find(kw, byte_pos)) != string::npos) {
            // 字节偏移 → UTF-8 字符下标（二分查找）
            auto it = upper_bound(char_offsets.begin(), char_offsets.end(), byte_pos);
            int char_idx = (it == char_offsets.begin()) ? 0 : (it - char_offsets.begin() - 1);

            int win_begin = max(0, char_idx - HALF);
            int win_end   = min(total_chars, char_idx + (int)utf8::distance(kw.begin(), kw.end()) + HALF);
            intervals.push_back({win_begin, win_end, 1});

            byte_pos += kw.size();  // 继续搜索下一个命中
        }
    }

    if (intervals.empty()) {
        // 关键词不在正文中，回退到前 50 个字符
        int n = min(50, total_chars);
        size_t byte_len = (n < total_chars) ? char_offsets[n] : content.size();
        return content.substr(0, byte_len) + (n < total_chars ? "..." : "");
    }

    // ---- 3. 按起始位置排序 ----
    sort(intervals.begin(), intervals.end(),
         [](const Interval& a, const Interval& b) { return a.begin < b.begin; });

    // ---- 4. 合并重叠窗口 ----
    vector<Interval> merged;
    for (auto& iv : intervals) {
        if (merged.empty() || merged.back().end < iv.begin) {
            merged.push_back(iv);
        } else {
            merged.back().end = max(merged.back().end, iv.end);
            merged.back().kw_hits += iv.kw_hits;
        }
    }

    // ---- 5. 选择最佳窗口：命中关键词最多者，相同时取较大者 ----
    Interval best = merged[0];
    for (auto& iv : merged) {
        if (iv.kw_hits > best.kw_hits ||
            (iv.kw_hits == best.kw_hits && (iv.end - iv.begin) > (best.end - best.begin))) {
            best = iv;
        }
    }

    // ---- 6. 截取摘要文本 ----
    size_t byte_begin = char_offsets[best.begin];
    size_t byte_end   = (best.end < total_chars) ? char_offsets[best.end] : content.size();
    string snippet = content.substr(byte_begin, byte_end - byte_begin);

    // ---- 7. 添加省略号 ----
    if (best.begin > 0) snippet = "..." + snippet;
    if (best.end < total_chars) snippet += "...";

    return snippet;
}

vector<Page> PageSearcher::get_result() 
{
     return result_;
}
