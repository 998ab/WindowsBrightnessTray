#include "../src/display_names.h"
#include <iostream>
#include <fcntl.h>
#include <io.h>
int main() {
    _setmode(_fileno(stdout),_O_U8TEXT);
    auto names=display_names::Query();
    if(names.empty()){std::wcerr<<L"No active display names returned\n";return 1;}
    for(const auto& entry:names)for(const auto& model:entry.second)std::wcout<<entry.first<<L" => "<<model<<L"\n";
}
