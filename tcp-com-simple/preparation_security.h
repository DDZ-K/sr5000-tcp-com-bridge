// Win7-compatible, pinned installer verification. Never executes a process.
#include <bcrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mscat.h>
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"wintrust.lib")
namespace preparationsecurity {
const wchar_t* const InstallerRelative=L"\\drivers\\win7-x64\\Setup_com0com_v3.0.0.0_W7_x64_signed.exe";
const BYTE InstallerHash[32]={0x26,0x48,0x6b,0x28,0x60,0x4b,0x49,0xa9,0x00,0x8c,0x54,0xfe,0xb1,0x1b,0x9e,0xce,0x00,0x08,0xa8,0x28,0x7e,0xe5,0xca,0xf0,0xbc,0xf2,0xa6,0x2f,0x43,0x17,0x12,0x8f};
const wchar_t* const Win10InstallerRelative=L"\\drivers\\win10-x64\\setup-com0com-2.2.2-x64-signed.exe";
const BYTE Win10InstallerHash[32]={0x64,0xcf,0x92,0xe5,0xb5,0x6f,0x94,0xc1,0xca,0x14,0xbb,0xbb,0xdc,0xf0,0xcb,0x38,0xb8,0x66,0x24,0x1c,0x64,0x00,0xe6,0x7b,0x5e,0x41,0xc5,0x8d,0xae,0xc3,0x9c,0x12};
const BYTE Win10CatalogHash[32]={0x0c,0xed,0xe1,0x2a,0xc6,0x35,0xcb,0xe4,0xcd,0x46,0x0d,0x42,0x0c,0xaa,0x3c,0x9c,0x41,0xfb,0x6b,0xc7,0x64,0x2f,0xd8,0x1e,0x05,0x3f,0xf6,0x7d,0xb0,0x8e,0x76,0x94};
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
bool verifyHash(LockedInstaller& pinned,LONGLONG expectedSize,const BYTE* expectedHash,std::wstring& error){
    LARGE_INTEGER zero={};if(!SetFilePointerEx(pinned.file,zero,nullptr,FILE_BEGIN)){error=L"无法读取固定发布物";return false;}
    LARGE_INTEGER length={};if(!GetFileSizeEx(pinned.file,&length)||length.QuadPart!=expectedSize){error=L"文件大小与固定发布物不符";return false;}
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;BYTE actual[32]={};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    BYTE buffer[8192];DWORD count=0;
    while(ok){if(!ReadFile(pinned.file,buffer,sizeof(buffer),&count,nullptr)){ok=false;break;}if(!count)break;ok=BCryptHashData(hash,buffer,count,0)>=0;}
    if(ok)ok=BCryptFinishHash(hash,actual,sizeof(actual),0)>=0&&memcmp(actual,expectedHash,sizeof(actual))==0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(!ok){error=L"固定SHA256不匹配或计算失败";return false;}
    SetFilePointerEx(pinned.file,zero,nullptr,FILE_BEGIN);return true;
}
bool verifyAuthenticode(const std::wstring& path,HANDLE file,std::wstring& error){
    WINTRUST_FILE_INFO fileInfo={};fileInfo.cbStruct=sizeof(fileInfo);fileInfo.pcwszFilePath=path.c_str();fileInfo.hFile=file;
    GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;WINTRUST_DATA data={};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&fileInfo;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    LONG status=WinVerifyTrust(nullptr,&policy,&data);data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&policy,&data);
    if(status!=ERROR_SUCCESS){error=L"Authenticode签名校验失败，状态 "+std::to_wstring(status)+L"；不可绕过签名，请核查证书与系统更新";return false;}
    return true;
}
bool verify(const std::wstring& path,LockedInstaller& pinned,std::wstring& error){
    error.clear();if(!pinPath(path,pinned,error))return false;
    if(!verifyHash(pinned,261544,InstallerHash,error)||!verifyAuthenticode(path,pinned.file,error)){pinned.reset();return false;}
    return true;
}
bool verifyCatalogMember(const std::wstring& catalog,const std::wstring& member,HANDLE file,std::wstring& error){
    DWORD size=0;CryptCATAdminCalcHashFromFileHandle(file,&size,nullptr,0);
    if(!size||size>128){error=L"无法计算驱动目录成员哈希";return false;}
    std::vector<BYTE> hash(size);
    if(!CryptCATAdminCalcHashFromFileHandle(file,&size,hash.data(),0)){error=L"驱动目录成员哈希计算失败";return false;}
    std::wstring tag;for(DWORD i=0;i<size;++i){tag+=L"0123456789ABCDEF"[hash[i]>>4];tag+=L"0123456789ABCDEF"[hash[i]&15];}
    WINTRUST_CATALOG_INFO catalogInfo={};catalogInfo.cbStruct=sizeof(catalogInfo);catalogInfo.pcwszCatalogFilePath=catalog.c_str();catalogInfo.pcwszMemberTag=tag.c_str();catalogInfo.pcwszMemberFilePath=member.c_str();catalogInfo.hMemberFile=file;catalogInfo.pbCalculatedFileHash=hash.data();catalogInfo.cbCalculatedFileHash=size;
    GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;WINTRUST_DATA data={};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_CATALOG;data.pCatalog=&catalogInfo;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    LONG status=WinVerifyTrust(nullptr,&policy,&data);data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&policy,&data);
    if(status!=ERROR_SUCCESS){error=L"SYS驱动目录成员签名校验失败，状态 "+std::to_wstring(status)+L"；代码52请停下反馈，不关闭签名或安全启动";return false;}
    return true;
}
bool verifyWin10(const std::wstring& path,LockedInstaller& pinned,std::wstring& error){
    error.clear();if(!pinPath(path,pinned,error))return false;
    if(!verifyHash(pinned,216423,Win10InstallerHash,error)){pinned.reset();return false;}
    // The outer NSIS installer is unsigned: its fixed hash is NOT Authenticode.
    // Keep installer, signed CAT, SYS and all ancestors locked until child exit.
    const std::wstring folder=path.substr(0,path.find_last_of(L'\\'));
    const std::wstring catalog=folder+L"\\com0com.cat",member=folder+L"\\com0com.sys";
    LockedInstaller cat,sys;
    if(!pinPath(catalog,cat,error)||!verifyHash(cat,8221,Win10CatalogHash,error)||!verifyAuthenticode(catalog,cat.file,error)||!pinPath(member,sys,error)||!verifyCatalogMember(catalog,member,sys.file,error)){pinned.reset();return false;}
    pinned.locks.insert(pinned.locks.end(),cat.locks.begin(),cat.locks.end());cat.locks.clear();
    pinned.locks.insert(pinned.locks.end(),sys.locks.begin(),sys.locks.end());sys.locks.clear();
    return true;
}
}
