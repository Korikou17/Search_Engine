#pragma once
#include <vector>
#include <string>

using namespace std;

class DirectoryScanner
{
    public:
    static vector<string> scan(const string& dir);

    private:
    DirectoryScanner() = delete;
};