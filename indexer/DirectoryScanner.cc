#include "DirectoryScanner.h"
#include "common.h"
#include <string>
#include <spdlog/spdlog.h>

vector<string> DirectoryScanner::scan(const string& dir)
{
    vector<string> result;
    DIR *dirp = opendir(dir.c_str());
    if (!dirp) { spdlog::error("无法打开目录: {}", dir); return result; }

    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        result.push_back(entry->d_name);
    }

    closedir(dirp);
    return result;
}