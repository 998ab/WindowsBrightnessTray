#include "localization.h"
#include "display_names.h"
#include <algorithm>
#include <cwctype>

namespace display_names {
namespace {
std::wstring Clean(std::wstring text) {
    std::wstring result;bool space=false;
    for(wchar_t c:text) {
        if(iswspace(c)||iswcntrl(c)){space=!result.empty();continue;}
        if(space)result+=L' ';space=false;result+=c;
    }
    return result;
}
bool Useful(const std::wstring& name) {
    std::wstring lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),towlower);
    return !lower.empty()&&lower.find(L"generic")==std::wstring::npos&&lower.find(L"pnp")==std::wstring::npos
        &&lower.find(L"\u0443\u043d\u0438\u0432\u0435\u0440\u0441\u0430\u043b\u044c\u043d")==std::wstring::npos&&lower.find(L"\u0441\u0442\u0430\u043d\u0434\u0430\u0440\u0442\u043d\u044b\u0439 \u043c\u043e\u043d\u0438\u0442\u043e\u0440")==std::wstring::npos;
}
}
Names Query() {
    Names result;
    for(int attempt=0;attempt<3;++attempt) {
        UINT32 pathCount=0,modeCount=0;
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount)!=ERROR_SUCCESS)return result;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        LONG status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
        if(status==ERROR_INSUFFICIENT_BUFFER)continue;
        if(status!=ERROR_SUCCESS)return result;
        paths.resize(pathCount);
        for(const auto& path:paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};source.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size=sizeof(source);source.header.adapterId=path.sourceInfo.adapterId;source.header.id=path.sourceInfo.id;
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};target.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            target.header.size=sizeof(target);target.header.adapterId=path.targetInfo.adapterId;target.header.id=path.targetInfo.id;
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS||DisplayConfigGetDeviceInfo(&target.header)!=ERROR_SUCCESS)continue;
            // Keep even empty names: a cloned source must never look like a
            // single target merely because one target did not supply EDID.
            result[source.viewGdiDeviceName].push_back(Clean(target.monitorFriendlyDeviceName));
        }
        return result;
    }
    return result;
}
std::wstring Label(const Names& names,const std::wstring& source,const std::wstring& physicalName,unsigned index,unsigned count) {
    std::wstring model=Clean(physicalName);
    auto found=names.find(source);
    if(count==1&&found!=names.end()&&found->second.size()==1&&Useful(found->second.front()))model=found->second.front();
    // For cloned/multiple physical targets there is no documented index join
    // between these APIs. Retain a specific physical name instead of guessing.
    if(!Useful(model))model=tr(L"Monitor");
    std::wstring number;
    auto start=source.find(L"DISPLAY");
    if(start!=std::wstring::npos)for(size_t i=start+7;i<source.size()&&iswdigit(source[i]);++i)number+=source[i];
    std::wstring suffix=number.empty()?tr(L"Display"):tr(L"Display ")+number;
    if(count>1)suffix+=L"."+std::to_wstring(index+1);
    return model+L" · "+suffix;
}
}
