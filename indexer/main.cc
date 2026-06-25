#include "indexer/KeywordProcessor.h"
#include "indexer/PageProcessor.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <vector>

void init_logger()
{
    // 同时输出到控制台(带颜色) 和 文件
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("offline_handler.log", true);

    vector<spdlog::sink_ptr> sinks {console_sink, file_sink};
    auto logger = make_shared<spdlog::logger>("search_engine", sinks.begin(), sinks.end());
    
    logger->set_level(spdlog::level::debug);   // debug 级别以上都输出
    spdlog::set_default_logger(logger);
}

void keyword_gen()
{
    KeyWordProcessor keyword_process;
    keyword_process.process("corpus/CN","corpus/EN");
}

void page_gen()
{
    PageProcessor pag;
    pag.process("corpus/webpages");
}

int main(int argc, char *argv[])
{
    init_logger();
    spdlog::info("搜索引擎启动");

    //关键词推荐
    //keyword_gen();

    //生成网页库和倒排索引
    page_gen();

    spdlog::info("搜索引擎运行完毕");
    return 0;
}