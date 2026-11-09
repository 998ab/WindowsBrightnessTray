#pragma once
#include <windows.h>
#include <map>
#include <string>
#include <vector>

namespace display_names {
using Names = std::map<std::wstring,std::vector<std::wstring>>;
Names Query();
std::wstring Label(const Names& names,const std::wstring& source,const std::wstring& physicalName,unsigned index,unsigned count);
}
