#ifndef SR5000_STARTUP_SETTINGS_H
#define SR5000_STARTUP_SETTINGS_H
#include <windows.h>
#include <string>
#include <vector>
namespace startup {
const wchar_t* const RunKey=LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
const wchar_t* const ValueName=L"SR5000TcpComBridge";
struct Preferences{bool connect=false,hotkey=false,hidden=false,shared=false;DWORD delay=10;std::wstring signature;};
inline std::wstring CommandFor(const std::wstring& exe){
    if(exe.size()<7||exe.size()>30000||exe[1]!=L':'||(exe[2]!=L'\\'&&exe[2]!=L'/')||exe.find_first_of(L"\"\r\n")!=std::wstring::npos||_wcsicmp(exe.c_str()+exe.size()-4,L".exe"))return L"";
    return L"\""+exe+L"\" --autostart";
}
inline LONG ReadRun(std::wstring& command,const wchar_t* keyPath=RunKey){
    command.clear();HKEY key=nullptr;LONG rc=RegOpenKeyExW(HKEY_CURRENT_USER,keyPath,0,KEY_QUERY_VALUE,&key);
    if(rc==ERROR_FILE_NOT_FOUND)return ERROR_SUCCESS;if(rc!=ERROR_SUCCESS)return rc;
    DWORD bytes=0,type=0;rc=RegQueryValueExW(key,ValueName,nullptr,&type,nullptr,&bytes);
    if(rc==ERROR_FILE_NOT_FOUND){RegCloseKey(key);return ERROR_SUCCESS;}
    if(rc!=ERROR_SUCCESS||type!=REG_SZ||bytes<2||bytes>65536||bytes%2){RegCloseKey(key);return rc==ERROR_SUCCESS?ERROR_INVALID_DATA:rc;}
    std::vector<wchar_t> data(bytes/2+1,0);rc=RegQueryValueExW(key,ValueName,nullptr,&type,(BYTE*)data.data(),&bytes);RegCloseKey(key);
    if(rc!=ERROR_SUCCESS)return rc;if(data[bytes/2-1])return ERROR_INVALID_DATA;command=data.data();return ERROR_SUCCESS;
}
inline bool SetRun(bool enabled,const std::wstring& exe,bool replaceOther,std::wstring& error,const wchar_t* keyPath=RunKey){
    std::wstring wanted=CommandFor(exe),before;if(wanted.empty()){error=L"程序路径无效，未注册自启动";return false;}
    LONG rc=ReadRun(before,keyPath);if(rc!=ERROR_SUCCESS){error=L"读取自启动失败："+std::to_wstring(rc);return false;}
    if(!before.empty()&&before!=wanted&&!replaceOther){error=L"已有其他目录的本工具自启动，需明确确认后才替换/删除";return false;}
    HKEY key=nullptr;if(enabled)rc=RegCreateKeyExW(HKEY_CURRENT_USER,keyPath,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);else rc=RegOpenKeyExW(HKEY_CURRENT_USER,keyPath,0,KEY_SET_VALUE,&key);
    if(!enabled&&rc==ERROR_FILE_NOT_FOUND)return true;
    if(rc!=ERROR_SUCCESS){error=L"修改自启动失败："+std::to_wstring(rc);return false;}
    if(enabled)rc=RegSetValueExW(key,ValueName,0,REG_SZ,(const BYTE*)wanted.c_str(),(DWORD)((wanted.size()+1)*sizeof(wchar_t)));
    else{rc=RegDeleteValueW(key,ValueName);if(rc==ERROR_FILE_NOT_FOUND)rc=ERROR_SUCCESS;}
    RegCloseKey(key);if(rc!=ERROR_SUCCESS){error=L"保存自启动失败："+std::to_wstring(rc);return false;}
    std::wstring actual;rc=ReadRun(actual,keyPath);if(rc!=ERROR_SUCCESS||actual!=(enabled?wanted:L"")){error=L"自启动回读未通过，请检查注册表设置";return false;}return true;
}
inline std::wstring Read(const std::wstring& path,const wchar_t* name,const wchar_t* fallback=L"0"){
    wchar_t b[2048]={};GetPrivateProfileStringW(L"Startup",name,fallback,b,2048,path.c_str());return b;
}
inline Preferences Load(const std::wstring& path){
    Preferences p;p.connect=Read(path,L"Connect")==L"1";p.hotkey=Read(path,L"Hotkey")==L"1";p.hidden=Read(path,L"Hidden")==L"1";p.shared=Read(path,L"Shared")==L"1";
    std::wstring delay=Read(path,L"Delay",L"10");if(delay==L"0"||delay==L"5"||delay==L"10"||delay==L"20"||delay==L"30")p.delay=(DWORD)_wtoi(delay.c_str());p.signature=Read(path,L"Signature",L"");return p;
}
inline bool Save(const std::wstring& folder,const std::wstring& path,const Preferences& p){
    wchar_t temporary[MAX_PATH]={};if(!GetTempFileNameW(folder.c_str(),L"AUT",0,temporary))return false;
    std::wstring doc=L"[Startup]\r\nConnect="+std::wstring(p.connect?L"1":L"0")+L"\r\nHotkey="+(p.hotkey?L"1":L"0")+L"\r\nHidden="+(p.hidden?L"1":L"0")+L"\r\nShared="+(p.shared?L"1":L"0")+L"\r\nDelay="+std::to_wstring(p.delay)+L"\r\nSignature="+p.signature+L"\r\n";
    HANDLE h=CreateFileW(temporary,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);bool ok=h!=INVALID_HANDLE_VALUE;
    if(ok){DWORD written=0;wchar_t bom=0xfeff;ok=WriteFile(h,&bom,2,&written,nullptr)&&written==2;DWORD count=(DWORD)(doc.size()*sizeof(wchar_t));if(ok)ok=WriteFile(h,doc.data(),count,&written,nullptr)&&written==count;if(ok)ok=FlushFileBuffers(h)!=FALSE;CloseHandle(h);}
    if(ok)ok=MoveFileExW(temporary,path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temporary);WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());return ok;
}
inline bool SharedConsentMatches(const Preferences& p,const std::wstring& signature){return p.shared&&!p.signature.empty()&&p.signature==signature;}
}
#endif
