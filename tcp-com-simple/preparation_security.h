// Win7-compatible, pinned installer verification. Never executes a process.
#include <bcrypt.h>
#include <wintrust.h>
#include <softpub.h>
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"wintrust.lib")
namespace preparationsecurity {
const wchar_t* const InstallerRelative=L"\\drivers\\win7-x64\\Setup_com0com_v3.0.0.0_W7_x64_signed.exe";
const BYTE InstallerHash[32]={0x26,0x48,0x6b,0x28,0x60,0x4b,0x49,0xa9,0x00,0x8c,0x54,0xfe,0xb1,0x1b,0x9e,0xce,0x00,0x08,0xa8,0x28,0x7e,0xe5,0xca,0xf0,0xbc,0xf2,0xa6,0x2f,0x43,0x17,0x12,0x8f};
struct LockedInstaller{
    std::vector<HANDLE> locks;HANDLE file=INVALID_HANDLE_VALUE;
    ~LockedInstaller(){reset();}
    void reset(){for(HANDLE h:locks)CloseHandle(h);locks.clear();file=INVALID_HANDLE_VALUE;}
    LockedInstaller()=default;LockedInstaller(const LockedInstaller&)=delete;LockedInstaller& operator=(const LockedInstaller&)=delete;
};
bool pinPath(const std::wstring& path,LockedInstaller& pinned,std::wstring& error){
    pinned.reset();
    if(path.size()<4||path[1]!=L':'||path[2]!=L'\\'||path.find(L'"')!=std::wstring::npos){error=L"安装器路径不是本地绝对路径";return false;}
    // Lock every ancestor against rename/delete and refuse junctions/symlinks.
    for(size_t at=3;;){
        size_t end=path.find(L'\\',at);bool directoryPart=end!=std::wstring::npos;std::wstring part=directoryPart?path.substr(0,end):path;
        HANDLE h=CreateFileW(part.c_str(),directoryPart?FILE_READ_ATTRIBUTES:GENERIC_READ,directoryPart?(FILE_SHARE_READ|FILE_SHARE_WRITE):FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directoryPart?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
        if(h==INVALID_HANDLE_VALUE){error=L"安装器或目录无法安全打开，Windows错误 "+std::to_wstring(GetLastError());pinned.reset();return false;}
        pinned.locks.push_back(h);BY_HANDLE_FILE_INFORMATION info={};
        if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=directoryPart){error=L"安装器路径包含重解析点或类型不符";pinned.reset();return false;}
        if(!directoryPart){pinned.file=h;return true;}at=end+1;
    }
}
bool verify(const std::wstring& path,LockedInstaller& pinned,std::wstring& error){
    error.clear();if(!pinPath(path,pinned,error))return false;
    LARGE_INTEGER length={};if(!GetFileSizeEx(pinned.file,&length)||length.QuadPart!=261544){error=L"安装器大小与固定发布物不符";pinned.reset();return false;}
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;BYTE actual[32]={};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    BYTE buffer[8192];DWORD count=0;
    while(ok){if(!ReadFile(pinned.file,buffer,sizeof(buffer),&count,nullptr)){ok=false;break;}if(!count)break;ok=BCryptHashData(hash,buffer,count,0)>=0;}
    if(ok)ok=BCryptFinishHash(hash,actual,sizeof(actual),0)>=0&&memcmp(actual,InstallerHash,sizeof(actual))==0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(!ok){error=L"固定SHA256不匹配或计算失败";pinned.reset();return false;}
    LARGE_INTEGER zero={};SetFilePointerEx(pinned.file,zero,nullptr,FILE_BEGIN);
    WINTRUST_FILE_INFO fileInfo={};fileInfo.cbStruct=sizeof(fileInfo);fileInfo.pcwszFilePath=path.c_str();fileInfo.hFile=pinned.file;
    GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;WINTRUST_DATA data={};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&fileInfo;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    LONG status=WinVerifyTrust(nullptr,&policy,&data);data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&policy,&data);
    if(status!=ERROR_SUCCESS){error=L"Authenticode签名校验失败，状态 "+std::to_wstring(status)+L"；不可绕过签名，请核查Win7证书与系统更新";pinned.reset();return false;}
    return true;
}
}
