#pragma once
#include <windows.h>
#include <atomic>
#include <map>
#include <string>

namespace i18n {
using Catalog=std::map<std::wstring,std::wstring>;
inline Catalog english,russian;
inline std::atomic<bool> useRussian{false};
inline std::wstring Unescape(const std::wstring& input) {
    std::wstring out;
    for(size_t i=0;i<input.size();++i){if(input[i]==L'\\'&&i+1<input.size()){
        wchar_t c=input[++i];out+=c==L'n'?L'\n':c==L't'?L'\t':c==L'r'?L'\r':c;
    }else out+=input[i];}return out;
}
inline Catalog Parse(const std::string& utf8) {
    Catalog result;int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),static_cast<int>(utf8.size()),nullptr,0);
    if(!length)return result;
    std::wstring text(static_cast<size_t>(length),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),static_cast<int>(utf8.size()),text.data(),length);
    for(size_t start=0;start<text.size();) {
        size_t end=text.find(L'\n',start);if(end==std::wstring::npos)end=text.size();
        auto line=text.substr(start,end-start);if(!line.empty()&&line.back()==L'\r')line.pop_back();
        size_t tab=line.find(L'\t');if(tab!=std::wstring::npos)result.emplace(Unescape(line.substr(0,tab)),Unescape(line.substr(tab+1)));
        start=end+1;
    }return result;
}
inline Catalog Resource(int id) {
    auto module=GetModuleHandleW(nullptr);auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
    if(!resource)return {};
    auto data=LoadResource(module,resource);auto bytes=static_cast<const char*>(LockResource(data));
    return bytes?Parse(std::string(bytes,SizeofResource(module,resource))):Catalog{};
}
inline void SetLanguage(const std::wstring& language) {
    useRussian.store(language==L"ru"||(language!=L"en"&&PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_RUSSIAN));
}
inline void Initialize(const std::wstring& language=L"auto") {
    // Catalogs are immutable after startup; monitor enumeration reads them on a worker.
    english=Resource(101);russian=Resource(102);SetLanguage(language);
}
inline const wchar_t* Language(){return useRussian.load()?L"ru":L"en";}
inline const wchar_t* tr(const wchar_t* key) {
    const auto& catalog=useRussian.load()?russian:english;
    auto found=catalog.find(key);return found==catalog.end()?key:found->second.c_str();
}
}
using i18n::tr;
