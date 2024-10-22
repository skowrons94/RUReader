#include <map>
#include <bitset>

#include "Utils.h"

std::bitset<32> project_range(int min, int max, std::bitset<32> b)
{
    b = b << ( 32 - max - 1);           // drop R rightmost bits
    b = b >> (min + (32 - max - 1));    // drop L-1 leftmost bits
    return b;
};
