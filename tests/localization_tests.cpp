#include "../src/localization.h"
#include <cstdlib>
#include <iostream>
void Check(bool value){if(!value){std::cerr<<"Localization validation failed\n";std::exit(1);}}
int main(){
    i18n::Initialize(L"en");Check(i18n::english.size()>100&&i18n::russian.size()==i18n::english.size());
    for(const auto& entry:i18n::english){Check(!entry.first.empty()&&entry.first==entry.second);auto ru=i18n::russian.find(entry.first);Check(ru!=i18n::russian.end()&&!ru->second.empty());}
    Check(std::wstring(tr(L"Brightness"))==L"Brightness");
    i18n::SetLanguage(L"ru");Check(std::wstring(tr(L"Brightness"))==L"\u042f\u0440\u043a\u043e\u0441\u0442\u044c");
    Check(std::wstring(tr(L"Enable DDC/CI in your monitor menu\nand click Refresh.")).find(L'\n')!=std::wstring::npos);
    Check(std::wstring(tr(L"Unknown fallback"))==L"Unknown fallback");
    i18n::SetLanguage(L"en");Check(std::wstring(tr(L"Cancel"))==L"Cancel");
    std::cout<<"PASS: embedded English/Russian catalogs, complete key parity, UTF-8, newlines, fallback and live switching\n";
}
