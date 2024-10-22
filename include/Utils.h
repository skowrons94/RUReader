#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include <map>
#include <bitset>

std::bitset<32> project_range(int min, int max, std::bitset<32> b);

template<typename Key, typename Value, typename Arg>
bool CheckKey(const std::map< Key, Value >& m, const Arg& value){ return m.find(value) != m.end(); }

#endif // UTILS_H_INCLUDED