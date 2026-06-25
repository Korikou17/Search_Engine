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

    set<int> candidates;
    bool first = true;

    for(auto &word:keywords)
    {
        auto it = inverted_index_.find(word);
        if(it == inverted_index_.end()) return;

        if(first) {
            for(auto &[doc_id, weight] : it->second)
                candidates.insert(doc_id);
            first = false;
        }else {
            set<int> tmp;
            for(auto &[doc_id, weight] : it->second)
                if(candidates.count(doc_id))
                    tmp.insert(doc_id);
            candidates = std::move(tmp);
        }
        if(candidates.empty()) return;
    }
    spdlog::info("候选文档数: {}", candidates.size());

    map<int, double> doc_score;

    for(auto &kw : keywords) {
        for(auto &[doc_id, weight] : inverted_index_[kw]) 
        {
            if(candidates.count(doc_id))
            {
                doc_score[doc_id] += weight;
            }
        }
    }

    ranked_.assign(doc_score.begin(), doc_score.end());
    sort(ranked_.begin(), ranked_.end(),
         [](auto &a, auto &b) { return a.second > b.second; });
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
        // 提取 content → 生成摘要（前 50 个 UTF-8 字符）
        tinyxml2::XMLElement* contentElem = root->FirstChildElement("content");
        string content = contentElem ? contentElem->GetText() : "";
        
        const int ABSTRACT_LEN = 50;
        int count = 0;
        const char *curr = content.c_str();
        const char *end  = content.c_str() + content.size();

        string abstract;
        while (count <= ABSTRACT_LEN && curr != end) {
            auto start = curr;
            utf8::next(curr, end);
            abstract.append(start, curr);
            ++count;
        }

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
}

vector<Page> PageSearcher::get_result() 
{
     return result_;
}
