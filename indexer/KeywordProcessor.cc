#include "KeywordProcessor.h"
#include "DirectoryScanner.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <set>
#include <utfcpp/utf8.h>
#include <spdlog/spdlog.h>

KeyWordProcessor::KeyWordProcessor()
{
    ifstream ifs{"stopwords/cn_stopwords.txt"};
    if(!ifs){ spdlog::error("打开停用词文件失败: stopwords/cn_stopwords.txt"); return; }
    string word;
    while(ifs>>word)
    {
        chStopWords_.emplace(word);
    }
    ifs.close();
    ifstream en_stop_ifs{"stopwords/en_stopwords.txt"};
    if(!en_stop_ifs){ spdlog::error("打开停用词文件失败: stopwords/en_stopwords.txt"); return; }
    while(en_stop_ifs>>word)
    {
        enStopWords_.emplace(word);
    }
    en_stop_ifs.close();
}


void KeyWordProcessor::create_en_dict_and_index(const string& dir, const string& dict_file,const string& index_file)
{
    //扫描目录
    vector<string>enfiles = DirectoryScanner::scan(dir);
    //create en_dict
    map<string,int> dict;
    for(auto& filename:enfiles)
    {
        ifstream ifs{dir+"/"+filename};
        if(!ifs){ spdlog::error("打开文件失败: {}/{}", dir, filename); continue; }

        ostringstream oss;
        oss << ifs.rdbuf();
        string text = oss.str();

        for (char &ch : text) {
            if (isalpha(ch)) {
                ch = tolower(ch);
            } else {
                ch = ' ';  // 数字、标点全变空格
            }
        }
        istringstream iss(text); 
        string word;
        while(iss>>word){
            if (enStopWords_.find(word) != enStopWords_.end()) {
                continue; 
            }
            ++dict[word];
        }
        ifs.close();
    }
    ofstream dict_ofs{dict_file};
    if(!dict_ofs){ spdlog::error("无法创建词典文件: {}", dict_file); return; }
    for(auto &d:dict){
        dict_ofs<<d.first<<" "<<d.second<<endl;
    }
    dict_ofs.close();
    //create en_index
    map<char, set<int> > charNumbers;
    for(auto it = dict.begin(); it != dict.end(); ++it)
    {
        int line_num=distance(dict.begin(), it) + 1;
        //cout<<it->first<<endl;
        for(auto ch=it->first.begin();ch!=it->first.end();++ch){
            charNumbers[*ch].insert(line_num);
        }
    }

    ofstream index_ofs{index_file};
    if(!index_ofs){ spdlog::error("无法创建索引文件: {}", index_file); return; }
    for(auto &[key,value]:charNumbers){
        index_ofs<<key;
        for(auto &num:value)
        {
            index_ofs<<" "<<num;
        }
        index_ofs<<endl;
    }
    index_ofs.close();
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

void KeyWordProcessor::create_cn_dict_and_index(const string& dir, const string& dict_file,const string& index_file)
{
    //扫描目录
    vector<string>cnfiles = DirectoryScanner::scan(dir);
    //create cn_dict
    map<string,int> dict;
    for(auto& filename:cnfiles)
    {
        ifstream ifs{dir+"/"+filename};
        if(!ifs){ spdlog::error("打开文件失败: {}/{}", dir, filename); continue; }

        ostringstream oss;
        oss << ifs.rdbuf();
        string text = oss.str();

        vector<string> words;
        tokenizer_.Cut(text, words);
        for(auto &word:words){
            if(!isAllChinese(word)){
                continue;
            }
            if (chStopWords_.find(word)!=chStopWords_.end()){
                continue;
            }
            ++dict[word];
        }
        ifs.close();
    }

    ofstream dict_ofs{dict_file};
    if(!dict_ofs){ spdlog::error("无法创建词典文件: {}", dict_file); return; }
    for(auto &d:dict){
        dict_ofs<<d.first<<" "<<d.second<<endl;
    }
    dict_ofs.close();

    //create cn_index
    map<string, set<int> > chwordNumbers;
    for(auto word = dict.begin(); word != dict.end(); ++word)
    {
        //cout<<word->first<<endl;
        const char* curr = word->first.c_str();
        const char* end = word->first.c_str() + word->first.size();
        int line_num=distance(dict.begin(), word) + 1;
        while (curr != end) {
            auto start = curr;
            // 将it移动到下一个utf8字符所在的位置
            utf8::next(curr, end);
            // 一个汉字会占用多个字节，因此需要用string来表示一个汉字
            string character = string(start, curr);
            chwordNumbers[character].insert(line_num);
        }
    }

    ofstream index_ofs{index_file};
    if(!index_ofs){ spdlog::error("无法创建索引文件: {}", index_file); return; }
    for(auto &[key,value]:chwordNumbers){
        index_ofs<<key;
        for(auto &num:value)
        {
            index_ofs<<" "<<num;
        }
        index_ofs<<endl;
    }
    index_ofs.close();
}

void KeyWordProcessor::process(const string& chDir, const string& enDir)
{
    spdlog::info("===== 开始生成关键词词典 & 索引 =====");
    spdlog::info("英文词典 & 索引开始构建");
    create_en_dict_and_index(enDir,"data/dict_en.dat","data/index_en.dat");
    spdlog::info("英文词典 & 索引构建完成");
    spdlog::info("中文词典 & 索引开始构建");
    create_cn_dict_and_index(chDir,"data/dict_cn.dat","data/index_cn.dat");
    spdlog::info("中文词典 & 索引构建完成");
    spdlog::info("===== 关键词推荐处理完毕 =====");
}

