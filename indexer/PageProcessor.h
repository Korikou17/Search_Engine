#pragma once
#include <string>
#include <vector>
#include <set>

#include "cppjieba/Jieba.hpp"
#include "simhash/Simhasher.hpp"

using namespace std;

class PageProcessor
{
public:

    PageProcessor();
    void process(const string& dir);
private:
    /// 解析dir目录下的xml文件，提取文档，放入documents_中
    void extract_and_deduplicate_documents(const string& dir);
    /// 依据SimHash算法对documents_去重
    //void deduplicate_documents();
    /// 构建网页库和网页偏移库
    void build_pages_and_offsets(const string& pages, const string& offsets);
    /// 构建倒排索引库
    void build_inverted_index(const string& filename);

private:

    struct Document {
        int id;
        string link;
        string title;
        string content;
        uint64_t hamming;
    };

private:
cppjieba::Jieba tokenizer_;
simhash::Simhasher hasher_;
set<string> stopWords_;
vector<Document> documents_;
// 使用set, 而非vector, 是为了方便查找
map<string, map<int, double>> invertedIndex_;
};