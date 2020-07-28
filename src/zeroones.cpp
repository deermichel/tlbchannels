#include <filesystem>
#include <iostream>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <map>

int countOne[] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
};

int main(int argc, char **argv) {
    long long files = 0;
    std::map<std::string, std::pair<long long, long long>> map;
    try {
        for (const auto &dirEntry : std::filesystem::recursive_directory_iterator(argv[1])) {
            if (dirEntry.is_regular_file()) {
                std::ifstream stream(dirEntry.path(), std::ios::in | std::ios::binary);
                long long ones = 0;
                std::for_each(
                    std::istreambuf_iterator<char>(stream),
                    std::istreambuf_iterator<char>(),
                    [&](unsigned char byte) { ones += countOne[byte]; }
                );
                long long total = 8 * dirEntry.file_size();

                std::string ext(dirEntry.path().extension().c_str());
                auto it = map.find(ext);
                if (it != map.end()) {
                    it->second.first += ones;
                    it->second.second += total;
                } else {
                    map.insert({ ext, { ones, total } });
                }

                files++;
                printf("-- %s %lld %lld \n", dirEntry.path().c_str(), ones, total);
            } else {
                printf("%s\n", dirEntry.path().c_str());
            }
        }
    } catch(const std::exception &ex) {
        printf("aborting: %s\n", ex.what());
    }

    printf("--- results (%lld files) ---\n", files);
    for (auto const &entry : map) {
        printf("%s,%lld,%lld,%.3f\n", entry.first.c_str(), entry.second.first, entry.second.second,
            (entry.second.first / (float)entry.second.second));
    }

    return 0;
}