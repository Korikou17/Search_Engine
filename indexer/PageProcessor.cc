#include "PageProcessor.h" 
#include "DirectoryScanner.h"
#include "tinyxml2.h"
#include <bitset>
#include <ios>
#include <algorithm>
#include <regex>
#include <utfcpp/utf8.h>
#include <cmath>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <unordered_set>

using namespace tinyxml2;
using namespace simhash;

//加载停用词
PageProcessor::PageProcessor()
{
    ifstream ifs{"stopwords/en_stopwords.txt"};
    if(!ifs){spdlog::error("打开停用词文件失败: stopwords/en_stopwords.txt");return;}
    string word;
    while(ifs>>word)
    {
        stopWords_.emplace(word);
    }
    ifs.close();
}

/// 解析dir目录下的xml文件，提取文档，放入documents_中,并依据SimHash算法对documents_去重
void PageProcessor::extract_and_deduplicate_documents(const string& dir)
{
    //扫描目录
    vector<string>xmlfiles = DirectoryScanner::scan(dir);
    int doc_id=1;
    //通用正则表达式
    regex reg("<[^>]+>");

    // 4 张分块哈希表：块值(16位) → doc_id 列表
    unordered_map<uint16_t, vector<int>> block_table[4];

    //循环文件
    for(auto &file:xmlfiles)
    {
        string filename="corpus/webpages/"+file;
        XMLDocument doc;
        if (doc.LoadFile(filename.c_str()) != XML_SUCCESS) {
            spdlog::error("加载 XML 失败: {} ({})", filename, doc.ErrorStr());
            continue;
        }

        XMLElement *root = doc.RootElement();
        if (!root) { spdlog::error("文件 {} 没有根元素", filename); continue; }

        XMLElement *channel = root->FirstChildElement("channel");
        if (!channel) { spdlog::error("文件 {} 没有 channel 元素", filename); continue; }
    
        for (XMLElement* itemElem = channel->FirstChildElement("item");
            itemElem != nullptr;
            itemElem = itemElem->NextSiblingElement("item"))
        {
            XMLElement* contentElem = itemElem->FirstChildElement("content");
            if(!contentElem){
                contentElem = itemElem->FirstChildElement("description");
                if(!contentElem){
                    spdlog::debug("文件 {} 中某个 item 没有 content/description，跳过", file);
                    continue;
                }
            }
            string content = contentElem->GetText();
            content = regex_replace(content, reg, "");
            //计算海明指纹
            int topN = max(5, min(200, static_cast<int>(content.size()/120)));
            uint64_t hamming;
            hasher_.make(content, topN, hamming);

            //========== 分块哈希表去重 ==========
            uint16_t blocks[4] = {
                static_cast<uint16_t>(hamming >> 48),
                static_cast<uint16_t>((hamming >> 32) & 0xFFFF),
                static_cast<uint16_t>((hamming >> 16) & 0xFFFF),
                static_cast<uint16_t>(hamming & 0xFFFF)
            };

            unordered_set<int> candidates;
            for (int i = 0; i < 4; ++i) {
                auto it = block_table[i].find(blocks[i]);
                if (it != block_table[i].end()) {
                    candidates.insert(it->second.begin(), it->second.end());
                }
            }

            bool flag = false;
            for (int candidate_id : candidates) {
                if (Simhasher::isEqual(hamming, documents_[candidate_id - 1].hamming)) {
                    flag = true;
                    break;
                }
            }
            if (flag) continue;
            
            XMLElement* titleElem = itemElem->FirstChildElement("title");
            string title = titleElem?titleElem->GetText():"";
            title = regex_replace(title, reg, "");

            XMLElement* linkElem = itemElem->FirstChildElement("link");
            string link = linkElem?linkElem->GetText():"";
            link = regex_replace(link, reg, "");

            documents_.push_back(Document{doc_id,link,title,content,hamming});
            for (int i = 0; i < 4; ++i) {
                block_table[i][blocks[i]].push_back(doc_id);
            }
            //spdlog::debug("处理文档 #{}: {}", doc_id, title);
            
            ++doc_id;
        } 
    }
}

/// 构建网页库和网页偏移库
void PageProcessor::build_pages_and_offsets(const string& pages, const string& offsets)
{
    //构建网页库 和 网页偏移库（每行: docid offset length）
    ofstream page_ofs{pages};
    if(!page_ofs){ spdlog::error("无法创建网页库文件: {}", pages); return; }
    ofstream offset_ofs{offsets};
    if(!offset_ofs){ spdlog::error("无法创建偏移库文件: {}", offsets); return; }

    for (auto &page:documents_)
    {
        // 记录写入前的偏移位置
        streampos start = page_ofs.tellp();

        page_ofs << "<doc>" << endl;
        page_ofs << "\t<docid>" << page.id << "</docid>" << endl;
        page_ofs << "\t<link>" << page.link << "</link>" << endl;
        page_ofs << "\t<title>" << page.title << "</title>" << endl;
        page_ofs << "\t<content>" << page.content << "</content>" << endl;
        page_ofs << "</doc>" << endl;

        // 记录写入后的偏移位置，计算当前文档长度
        streampos end = page_ofs.tellp();
        offset_ofs << page.id << " " << start << " " << (end - start) << endl;
    }
    page_ofs.close();
    offset_ofs.close();
}

static bool isAllChinese(const string& word) {
    if (word.empty()) return false;

    auto it  = utf8::iterator<string::const_iterator>(word.begin(), word.begin(), word.end());
    auto end = utf8::iterator<string::const_iterator>(word.end(),   word.begin(), word.end());

    for (; it != end; ++it) {
        char32_t cp = *it;
        // CJK 统一汉字 U+4E00 ~ U+9FFF
        if (cp < 0x4E00 || cp > 0x9FFF) {
            return false;
        }
    }
    return true;
}

/// 构建倒排索引库
void PageProcessor::build_inverted_index(const string& filename)
{
    constexpr double k1 = 1.2;   // 词频饱和参数
    constexpr double b  = 0.75;  // 长度归一化参数
    double N = documents_.size();

    // 统计阶段
    map<string, map<int, int>> keyword_times_in_page;
    map<int, int> total_words_in_doc;

    for(auto &doc:documents_)
    {
        vector<string> words;
        tokenizer_.Cut(doc.content, words);
        for(auto &word:words){
            if(!isAllChinese(word)){
                continue;
            }
            if (stopWords_.find(word)!=stopWords_.end()){
                continue;
            }
            ++keyword_times_in_page[word][doc.id];
            ++total_words_in_doc[doc.id];
        }
    }

    // 平均文档长度
    double avgdl = 0;
    for(auto &doc:documents_) avgdl += total_words_in_doc[doc.id];
    avgdl /= N;

    // BM25 计算权重
    for(auto &[word, value] : keyword_times_in_page)
    {
        double df = value.size();
        double idf = log2((N - df + 0.5) / (df + 0.5));

        for(auto &[doc_id, tf] : value)
        {
            double doc_len = total_words_in_doc[doc_id];
            double numerator   = tf * (k1 + 1.0);
            double denominator = tf + k1 * (1.0 - b + b * doc_len / avgdl);
            invertedIndex_[word][doc_id] = idf * numerator / denominator;
        }
    }

    ofstream ofs{filename};
    if(!ofs){ spdlog::error("无法创建倒排索引文件: {}", filename); return; }
    for(auto &[key,value]:invertedIndex_)
    {
        ofs<<key;
        for(auto &[id,score]:value)
        {
            ofs<<" "<<id<<" "<<score;
        }
        ofs << endl;
    }
    ofs.close();
}

void PageProcessor::process(const string& dir)
{
    spdlog::info("===== 开始处理网页 =====");
    extract_and_deduplicate_documents(dir);
    spdlog::info("文档提取完成，共 {} 篇", documents_.size());
    build_pages_and_offsets("data/pages.dat","data/offsets.dat");
    spdlog::info("网页库 & 偏移库构建完成");
    spdlog::info("倒排索引库开始构建");
    build_inverted_index("data/inverted_index.dat");
    spdlog::info("倒排索引库构建完成");
    spdlog::info("===== 网页处理完毕 =====");
}
