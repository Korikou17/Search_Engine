#include "PageProcessor.h" 
#include "DirectoryScanner.h"
#include "tinyxml2.h"
#include <bitset>
#include <ios>
#include <algorithm>
#include <regex>
#include <utfcpp/utf8.h>
#include <cmath>

using namespace tinyxml2;
using namespace simhash;

//加载停用词
PageProcessor::PageProcessor()
{
    ifstream ifs{"stopwords/en_stopwords.txt"};
    if(!ifs){cerr << "ifstream open file failed!" << endl;return;}
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
    //循环文件
    for(auto &file:xmlfiles)
    {
        string filename="corpus/webpages/"+file;
        XMLDocument doc;
        if (doc.LoadFile(filename.c_str()) != XML_SUCCESS) {cerr << "Failed to load XML file: " << doc.ErrorStr() << endl;return;}

        XMLElement *root = doc.RootElement();
        if (!root) {cerr << "No root element found." << endl;return;}

        XMLElement *channel = root->FirstChildElement("channel");
        if (!channel) {cerr << "No channel element found." << endl;return;}
    
        for (XMLElement* itemElem = channel->FirstChildElement("item");
            itemElem != nullptr;
            itemElem = itemElem->NextSiblingElement("item"))
        {
            XMLElement* contentElem = itemElem->FirstChildElement("content");
            if(!contentElem){
                contentElem = itemElem->FirstChildElement("description");
                if(!contentElem){
                    cout<<"contiune"<<endl;
                    continue;
                }
            }
            string content = contentElem->GetText();
            content = regex_replace(content, reg, "");
            //计算海明指纹
            int topN = max(5, min(200, static_cast<int>(content.size()/120)));
            uint64_t hamming;
            hasher_.make(content, topN, hamming);
            
            bool flag=false;

            for(auto &ele:documents_)
            {
                if(Simhasher::isEqual(hamming,ele.hamming))
                {
                    flag=true;
                    break;
                }
            }
            if(flag==true)continue;
            
            XMLElement* titleElem = itemElem->FirstChildElement("title");
            string title = titleElem?titleElem->GetText():"";
            title = regex_replace(title, reg, "");

            XMLElement* linkElem = itemElem->FirstChildElement("link");
            string link = linkElem?linkElem->GetText():"";
            link = regex_replace(link, reg, "");

            documents_.push_back(Document{doc_id,link,title,content,hamming});
            cout<<doc_id<<endl;
            ++doc_id;
        } 
    }
}

/// 构建网页库和网页偏移库
void PageProcessor::build_pages_and_offsets(const string& pages, const string& offsets)
{
    //构建网页库 和 网页偏移库（每行: docid offset length）
    ofstream page_ofs{pages};
    if(!page_ofs){cerr << "page_ofs open file failed" << endl;return;}
    ofstream offset_ofs{offsets};
    if(!offset_ofs){cerr << "offset_ofs open file failed" << endl;return;}

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
    //keyword -> <pageid, word_appear_times_in_page>
    map<string, map<int, int>> keyword_times_in_page;
    //keyword -> <pageid, word_frequency>
    map<string, map<int, double>> keyword_w_in_page;
    //每篇文档的去重单词集合
    map<int, set<string>> page_word;   
    // 每篇文档的总词数
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
            page_word[doc.id].insert(word);
            ++total_words_in_doc[doc.id];
        }
    }

    for(auto &[word,value]:keyword_times_in_page)
    {
        for(auto &[doc_id,times]:value)
        {
            double tf = static_cast<double>(times) / total_words_in_doc[doc_id];
            double df = value.size();
            double idf = log2(static_cast<double>(documents_.size()) / (df + 1.0));
            double w = tf * idf;
            keyword_w_in_page[word][doc_id] = w;
        }
    }

    for(auto &[doc_id,words]:page_word)
    {
        double sum_of_squares=0;
        for(auto &word:words)
        {
            double w=keyword_w_in_page[word][doc_id];
            sum_of_squares+=w*w;
        }
        double RMS=sqrt(sum_of_squares);
        for(auto &word:words)
        {
            invertedIndex_[word][doc_id]=keyword_w_in_page[word][doc_id]/RMS;
        }
    }

    ofstream ofs{filename};
    if(!ofs){cerr << "ofs open file failed" << endl;return;}
    for(auto &[key,value]:invertedIndex_)
    {
        ofs<<key;
        for(auto &[id,fre]:value)
        {
            ofs<<" "<<id<<" "<<fre;
        }
        ofs << endl;
    }
    ofs.close();
}

void PageProcessor::process(const string& dir)
{
    extract_and_deduplicate_documents(dir);
    build_pages_and_offsets("data/pages.dat","data/offsets.dat");
    build_inverted_index("data/inverted_index.dat");
}
