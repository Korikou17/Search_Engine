#pragma once
#include <cppjieba/Jieba.hpp>
#include <string>
#include <set>

using namespace std;

class KeyWordProcessor {
public:

    KeyWordProcessor();
    // chDir: 中文语料库
    // enDir: 英文语料库
    void process(const string& chDir, const string& enDir);
private:
    void create_cn_dict_and_index(const string& dir, const string& dict_file, const string& index_file);
    
    void create_en_dict_and_index(const string& dir, const string& dict_file, const string& index_file);
private:
    cppjieba::Jieba tokenizer_;
    set<string> enStopWords_;
    set<string> chStopWords_;
};