#ifndef SR5000_SERIAL_FORWARD_H
#define SR5000_SERIAL_FORWARD_H
#include <windows.h>
#include <shlobj.h>
#include <aclapi.h>
#include <sddl.h>
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include "com0com_inventory.h"
#include "native_inventory.h"
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"uuid.lib")
namespace serialbridge {
using Bytes=std::vector<unsigned char>;
struct SerialOptions{bool enabled=false,sharedApproved=false;std::wstring bridge,application;DWORD baud=9600;BYTE bits=8,parity=NOPARITY,stopBits=ONESTOPBIT;};
inline bool comName(const std::wstring& name){
    if(name.size()<4||name.size()>8||_wcsnicmp(name.c_str(),L"COM",3)||name[3]==L'0')return false;
    for(size_t i=3;i<name.size();++i)if(name[i]<L'0'||name[i]>L'9')return false;return true;
}
inline bool validSerialOptions(const SerialOptions& o){
    if(!o.enabled)return true;
    return comName(o.bridge)&&comName(o.application)&&_wcsicmp(o.bridge.c_str(),o.application.c_str())!=0&&
        (o.baud==1200||o.baud==2400||o.baud==4800||o.baud==9600||o.baud==19200||o.baud==38400||o.baud==57600||o.baud==115200)&&
        o.bits>=5&&o.bits<=8&&o.parity<=SPACEPARITY&&(o.stopBits==ONESTOPBIT||o.stopBits==TWOSTOPBITS);
}
inline std::string narrow(const std::wstring& s){return std::string(s.begin(),s.end());}
inline std::wstring wide(const std::string& s){return std::wstring(s.begin(),s.end());}
bool privilegedSid(PSID sid){
    if(!sid||!IsValidSid(sid))return false;
    BYTE system[SECURITY_MAX_SID_SIZE]={},admins[SECURITY_MAX_SID_SIZE]={};DWORD systemSize=sizeof(system),adminSize=sizeof(admins);
    if(CreateWellKnownSid(WinLocalSystemSid,nullptr,system,&systemSize)&&EqualSid(sid,system))return true;
    if(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admins,&adminSize)&&EqualSid(sid,admins))return true;
    PSID installer=nullptr;bool trusted=false;
    if(ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464",&installer)){
        trusted=EqualSid(sid,installer)!=FALSE;LocalFree(installer);
    }return trusted;
}
bool protectedInstallPath(const std::wstring& path,bool directory){
    DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)||
       ((attributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=directory)return false;
    PSECURITY_DESCRIPTOR descriptor=nullptr;PSID owner=nullptr;PACL acl=nullptr;
    DWORD result=GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&descriptor);
    if(result!=ERROR_SUCCESS||!descriptor){if(descriptor)LocalFree(descriptor);return false;}
    bool trusted=privilegedSid(owner)&&acl!=nullptr;
    const DWORD writable=GENERIC_ALL|GENERIC_WRITE|WRITE_DAC|WRITE_OWNER|DELETE|
        FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|FILE_DELETE_CHILD;
    for(DWORD i=0;trusted&&i<acl->AceCount;++i){
        void* item=nullptr;if(!GetAce(acl,i,&item)){trusted=false;break;}
        ACE_HEADER* header=static_cast<ACE_HEADER*>(item);
        if(header->AceFlags&INHERIT_ONLY_ACE)continue;
        if(header->AceType==ACCESS_ALLOWED_ACE_TYPE){
            ACCESS_ALLOWED_ACE* ace=static_cast<ACCESS_ALLOWED_ACE*>(item);
            if((ace->Mask&writable)&&!privilegedSid(&ace->SidStart))trusted=false;
        }else if(header->AceType!=ACCESS_DENIED_ACE_TYPE){
            // Do not guess the semantics of conditional/object allow ACEs on an elevated executable.
            trusted=false;
        }
    }
    LocalFree(descriptor);return trusted;
}
std::wstring findSetupc(){
    // Resolve OS-known default installation locations, never inherited ProgramFiles/PATH values.
    const KNOWNFOLDERID* folders[]={&FOLDERID_ProgramFiles,&FOLDERID_ProgramFilesX86};
    for(const KNOWNFOLDERID* id:folders){
        PWSTR known=nullptr;HRESULT result=SHGetKnownFolderPath(*id,KF_FLAG_DEFAULT_PATH,nullptr,&known);
        if(FAILED(result)||!known){if(known)CoTaskMemFree(known);continue;}
        const std::wstring base=known;CoTaskMemFree(known);
        const std::wstring directory=base+L"\\com0com",candidate=directory+L"\\setupc.exe";
        if(protectedInstallPath(base,true)&&protectedInstallPath(directory,true)&&protectedInstallPath(candidate,false))return candidate;
    }return L"";
}

#ifdef TCP_TESTER_TEST
struct TestBackend{bool enabled=false;std::wstring path;std::atomic<bool> peer,failWrite;std::string inventory;TestBackend():peer(true),failWrite(false){}};
TestBackend testBackend;
#endif
inline bool readSelectedInventory(const SerialOptions& options,com0com::Inventory& inventory,std::wstring& error){
    error.clear();
#ifdef TCP_TESTER_TEST
    if(testBackend.enabled){std::string detail;if(!com0com::ParseInventory(testBackend.inventory,inventory,detail)){error=wide(detail);return false;}return true;}
#endif
    std::wstring nativeError;
    // No installer process is needed for normal already-installed COM pairs.
    if(com0com::CaptureNativeSelected(options.application,options.bridge,inventory,nativeError))return true;
    std::wstring path=findSetupc();
    if(path.empty()){error=L"系统配对检查："+nativeError+L"；未找到可信setupc用于备用核验。不会跳过配对检查。";return false;}
    auto capture=com0com::CaptureInventory(path,2000);
    if(capture.status!=com0com::CaptureStatus::Ok){error=L"系统配对检查："+nativeError+L"；备用列表："+com0com::DescribeCaptureFailure(capture);return false;}
    inventory=capture.inventory;return true;
}
inline bool inspectPair(const SerialOptions& options,std::wstring& error){
    com0com::Inventory inventory;if(!readSelectedInventory(options,inventory,error))return false;
    auto pair=com0com::ValidatePair(inventory,narrow(options.application),narrow(options.bridge));
    if(!pair.identityVerified){error=L"桥接COM和软件COM不是库存中的同一虚拟串口对，未打开串口。";return false;}
    if(!pair.pinsVerified){error=L"串口配对正确，但需设置：软件端 EmuOverrun=yes；桥接端 dsr=ropen、EmuBR=yes。请点“配对设置”核对后配置。";return false;}
    return true;
}
struct WriteOperation{
    OVERLAPPED ov;Bytes bytes;HANDLE h;
    explicit WriteOperation(const Bytes& b):ov(),bytes(b),h(INVALID_HANDLE_VALUE){}
    ~WriteOperation(){if(ov.hEvent)CloseHandle(ov.hEvent);if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
};
inline std::vector<WriteOperation*>& heldWrites(){static std::vector<WriteOperation*> held;return held;}
inline void reapWrites(){auto& held=heldWrites();for(auto it=held.begin();it!=held.end();){if(WaitForSingleObject((*it)->ov.hEvent,0)==WAIT_OBJECT_0){delete *it;it=held.erase(it);}else ++it;}}
class SerialForwarder{
    HANDLE h_=INVALID_HANDLE_VALUE;bool enabled_=false,fault_=false,hadPeer_=false;
    std::wstring error_;unsigned long long bytes_=0;
public:
    ~SerialForwarder(){close();}
    void close(){if(h_!=INVALID_HANDLE_VALUE){PurgeComm(h_,PURGE_RXABORT|PURGE_TXABORT|PURGE_RXCLEAR|PURGE_TXCLEAR);CloseHandle(h_);h_=INVALID_HANDLE_VALUE;}enabled_=false;hadPeer_=false;}
    bool open(const SerialOptions& o){
        close();reapWrites();enabled_=o.enabled;fault_=false;bytes_=0;error_.clear();if(!enabled_)return true;
        if(!heldWrites().empty()){fault_=true;error_=L"上一笔驱动写入仍未完成，禁止重新打开，先检查驱动。";return false;}
        if(!validSerialOptions(o)||!inspectPair(o,error_)){fault_=true;if(error_.empty())error_=L"串口参数无效";return false;}
#ifdef TCP_TESTER_TEST
        if(testBackend.enabled){h_=CreateFileW(testBackend.path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_FLAG_OVERLAPPED,nullptr);}
        else
#endif
        {
            std::wstring device=L"\\\\.\\"+o.bridge;h_=CreateFileW(device.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
            if(h_!=INVALID_HANDLE_VALUE){
                DCB d={};d.DCBlength=sizeof(d);bool ok=GetCommState(h_,&d)!=FALSE;
                d.BaudRate=o.baud;d.ByteSize=o.bits;d.Parity=o.parity;d.StopBits=o.stopBits;d.fBinary=TRUE;d.fParity=o.parity!=NOPARITY;
                d.fInX=FALSE;d.fOutX=FALSE;d.fNull=FALSE;d.fErrorChar=FALSE;d.fDsrSensitivity=FALSE;d.fAbortOnError=FALSE;d.fOutxCtsFlow=FALSE;d.fOutxDsrFlow=FALSE;d.fDtrControl=DTR_CONTROL_ENABLE;d.fRtsControl=RTS_CONTROL_ENABLE;
                COMMTIMEOUTS timeout={};timeout.WriteTotalTimeoutConstant=500;
                if(!ok||!SetCommState(h_,&d)||!SetCommTimeouts(h_,&timeout)||!SetupComm(h_,4096,4096)){
                    DWORD code=GetLastError();CloseHandle(h_);h_=INVALID_HANDLE_VALUE;SetLastError(code);
                }else PurgeComm(h_,PURGE_RXCLEAR|PURGE_TXCLEAR);
            }
        }
        if(h_==INVALID_HANDLE_VALUE){fault_=true;error_=L"桥接端无法打开："+o.bridge+L"，Windows错误 "+std::to_wstring(GetLastError())+L"；检查占用/驱动。";return false;}
        hadPeer_=peer();return true;
    }
    bool peer(){
        if(h_==INVALID_HANDLE_VALUE)return false;
#ifdef TCP_TESTER_TEST
        if(testBackend.enabled)return testBackend.peer.load();
#endif
        DWORD signals=0;return GetCommModemStatus(h_,&signals)&&((signals&MS_DSR_ON)!=0);
    }
    bool poll(){
        reapWrites();if(!enabled_)return true;if(fault_)return false;bool present=peer();
        if(hadPeer_&&!present){fault_=true;error_=L"目标软件关闭了串口，已锁定转发和新触发；核对产品后手动重连，不重放旧码。";return false;}
        if(present)hadPeer_=true;return true;
    }
    bool ready(){return !enabled_||(!fault_&&h_!=INVALID_HANDLE_VALUE&&peer());}
    bool write(const Bytes& bytes,HANDLE stopping){
        if(!enabled_)return true;if(fault_)return false;
        if(!ready()){fault_=true;error_=L"收到数据时目标软件串口未打开；本批不补发，核对产品后手动重连。";return false;}
#ifdef TCP_TESTER_TEST
        if(testBackend.enabled&&testBackend.failWrite){fault_=true;error_=L"模拟串口写入失败（可能部分写入），不补发";return false;}
#endif
        std::unique_ptr<WriteOperation> op(new WriteOperation(bytes));
        if(!DuplicateHandle(GetCurrentProcess(),h_,GetCurrentProcess(),&op->h,0,FALSE,DUPLICATE_SAME_ACCESS)){fault_=true;error_=L"串口句柄复制失败";return false;}
        op->ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!op->ov.hEvent){fault_=true;error_=L"串口写事件创建失败";return false;}
#ifdef TCP_TESTER_TEST
        if(testBackend.enabled){op->ov.Offset=(DWORD)bytes_;op->ov.OffsetHigh=(DWORD)(bytes_>>32);}
#endif
        DWORD written=0;BOOL ok=WriteFile(op->h,op->bytes.data(),(DWORD)op->bytes.size(),&written,&op->ov);
        if(!ok&&GetLastError()==ERROR_IO_PENDING){HANDLE events[2]={op->ov.hEvent,stopping};DWORD wait=WaitForMultipleObjects(2,events,FALSE,500);
            if(wait==WAIT_OBJECT_0)ok=GetOverlappedResult(op->h,&op->ov,&written,FALSE);
            else{CancelIoEx(op->h,&op->ov);if(WaitForSingleObject(op->ov.hEvent,250)!=WAIT_OBJECT_0){heldWrites().push_back(op.release());fault_=true;error_=L"串口写取消未完成，保留I/O资源并锁定；可能部分/延迟写入，请人工核对。";return false;}ok=FALSE;}
        }
        if(!ok||written!=bytes.size()){fault_=true;error_=L"串口写入失败或部分写入，已锁定，不补发；请核对产品后重连。";return false;}
        bytes_+=written;return true;
    }
    void discardPending(){if(h_!=INVALID_HANDLE_VALUE)PurgeComm(h_,PURGE_RXABORT|PURGE_TXABORT|PURGE_RXCLEAR|PURGE_TXCLEAR);}
    bool opened()const{return h_!=INVALID_HANDLE_VALUE;}bool fault()const{return fault_;}
    const std::wstring& error()const{return error_;}unsigned long long count()const{return bytes_;}
};
}
#endif
