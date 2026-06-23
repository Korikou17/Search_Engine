#include "KeywordProcessor.h"
#include "PageProcessor.h"

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

int main(int argc,char *argv[])
{
    keyword_gen();
    page_gen();
}