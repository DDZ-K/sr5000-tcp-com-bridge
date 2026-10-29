#ifndef SR5000_COM0COM_INVENTORY_H
#define SR5000_COM0COM_INVENTORY_H
// Win7 x64 / C++14. No shell, installation, registry writes, or automatic elevation.
// Sources and integration contract: tests/provisioning/CONTRACT.md.
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
namespace com0com {
struct Endpoint {
    std::string id, pairId, portName;
    char side = 0;
    std::map<std::string, std::string> parameters;
};
struct Inventory { std::vector<Endpoint> endpoints; };
inline std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a'-'A'));
    return s;
}
inline bool IsCanonicalCom(const std::string& s) {
    if (s.size()<4 || s.size()>8 || s.compare(0,3,"COM") || s[3]<'1' || s[3]>'9') return false;
    for (size_t i=4;i<s.size();++i) if (s[i]<'0'||s[i]>'9') return false;
    return true;
}
inline bool ParseInventory(const std::string& text, Inventory& out, std::string& error) {
    out.endpoints.clear(); error.clear();
    Inventory parsed;
    if (text.size()>1024*1024 || text.find('\0')!=std::string::npos) { error="inventory too large or contains NUL"; return false; }
    std::istringstream input(text); std::string line;
    std::set<std::string> ids, names;
    while (std::getline(input,line)) {
        if (line.size()>4096) { error="inventory line too long"; return false; }
        if (!line.empty() && line.back()=='\r') line.pop_back();
        const size_t start=line.find_first_not_of(" \t");
        if (start==std::string::npos) continue;
        line=line.substr(start);
        const size_t sep=line.find_first_of(" \t");
        if (sep==std::string::npos) { error="missing endpoint parameters"; return false; }
        Endpoint ep; ep.id=line.substr(0,sep);
        if (ep.id.size()<5 || ep.id.size()>14 || ep.id.compare(0,3,"CNC") || (ep.id[3]!='A' && ep.id[3]!='B')) { error="invalid endpoint identity"; return false; }
        ep.side=ep.id[3]; ep.pairId=ep.id.substr(4);
        if (ep.pairId.size()>1 && ep.pairId[0]=='0') { error="noncanonical pair identity"; return false; }
        for (char c:ep.pairId) if (c<'0'||c>'9') { error="nonnumeric pair identity"; return false; }
        if (ep.pairId.size()==10 && ep.pairId>"2147483647") { error="pair identity out of range"; return false; }
        if (!ids.insert(ep.id).second) { error="duplicate endpoint identity"; return false; }
        const size_t p=line.find_first_not_of(" \t",sep);
        if (p==std::string::npos) { error="empty parameter list"; return false; }
        const std::string params=line.substr(p);
        size_t from=0;
        for (;;) {
            const size_t comma=params.find(',',from);
            const std::string item=params.substr(from,comma==std::string::npos?comma:comma-from);
            const size_t eq=item.find('=');
            if (eq==std::string::npos || eq==0 || eq+1==item.size() || item.find('=',eq+1)!=std::string::npos) { error="malformed parameter"; return false; }
            const std::string key=Lower(item.substr(0,eq)), val=item.substr(eq+1);
            for (char c:key) if (!((c>='a'&&c<='z')||(c>='0'&&c<='9'))) { error="invalid parameter key"; return false; }
            for (unsigned char c:val) if (c<=32 || c>=127) { error="non-ASCII or whitespace parameter value"; return false; }
            if (!ep.parameters.emplace(key,val).second) { error="duplicate parameter"; return false; }
            if (comma==std::string::npos) break;
            from=comma+1;
        }
        const auto n=ep.parameters.find("portname");
        if (n==ep.parameters.end()) { error="missing PortName"; return false; }
        const auto real=ep.parameters.find("realportname");
        if (n->second=="COM#") {
            if (real==ep.parameters.end() || !IsCanonicalCom(real->second)) { error="COM# lacks canonical RealPortName"; return false; }
            ep.portName=real->second;
        } else {
            if (real!=ep.parameters.end()) { error="ambiguous RealPortName"; return false; }
            ep.portName=n->second=="-"?ep.id:n->second;
            if (Lower(ep.portName).compare(0,3,"com")==0 && !IsCanonicalCom(ep.portName)) { error="noncanonical COM name"; return false; }
        }
        if (!names.insert(Lower(ep.portName)).second) { error="duplicate port name"; return false; }
        parsed.endpoints.push_back(ep);
        if (parsed.endpoints.size()>4096) { error="too many endpoints"; return false; }
    }
    out=parsed; return true;
}
struct PairCheck {
    bool identityVerified=false;
    bool pinsVerified=false; // configuration only, NOT electrical/driver/runtime verification
    std::string packagingId, bridgeId, error;
    bool ok() const { return identityVerified && pinsVerified; }
};
inline bool ParameterEquals(const Endpoint& e, const char* key, const char* value) {
    const auto i=e.parameters.find(key);
    return i!=e.parameters.end() && Lower(i->second)==value;
}
inline PairCheck ValidatePair(const Inventory& inv, const std::string& pack, const std::string& bridge) {
    PairCheck r;
    if (!IsCanonicalCom(pack)||!IsCanonicalCom(bridge)||pack==bridge) { r.error="two different canonical COM names required"; return r; }
    const Endpoint *a=nullptr,*b=nullptr;
    for (const Endpoint& ep:inv.endpoints) {
        if (ep.portName==pack) { if (a) { r.error="ambiguous packaging endpoint"; return r; } a=&ep; }
        if (ep.portName==bridge) { if (b) { r.error="ambiguous bridge endpoint"; return r; } b=&ep; }
    }
    if (!a || !b || a->pairId!=b->pairId || a->side==b->side) { r.error="selected ports are not opposite endpoints of one pair"; return r; }
    r.identityVerified=true; r.packagingId=a->id; r.bridgeId=b->id;
    r.pinsVerified=ParameterEquals(*a,"emuoverrun","yes") && ParameterEquals(*b,"emubr","yes") && ParameterEquals(*b,"dsr","ropen");
    if (!r.pinsVerified) r.error="require packaging EmuOverrun=yes, bridge EmuBR=yes and dsr=ropen explicitly";
    return r;
}

enum class CaptureStatus { Ok, InvalidInput, Win32Error, Timeout, OutputLimit, NonzeroExit, ParseError };
struct CaptureResult {
    CaptureStatus status=CaptureStatus::Win32Error;
    DWORD win32Error=ERROR_SUCCESS, exitCode=STILL_ACTIVE;
    std::string output, error;
    Inventory inventory;
};
namespace detail {
class Handle {
    HANDLE h_=nullptr;
public:
    Handle()=default;
    explicit Handle(HANDLE h):h_(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    HANDLE get() const { return h_; }
    void reset(HANDLE h=nullptr) { if (h_ && h_!=INVALID_HANDLE_VALUE) CloseHandle(h_); h_=h; }
};
class Attributes {
    LPPROC_THREAD_ATTRIBUTE_LIST p_=nullptr;
public:
    Attributes()=default;
    Attributes(const Attributes&)=delete;
    Attributes& operator=(const Attributes&)=delete;
    ~Attributes() { if (p_) { DeleteProcThreadAttributeList(p_); HeapFree(GetProcessHeap(),0,p_); } }
    bool init(HANDLE* handles, SIZE_T bytes) {
        SIZE_T size=0; InitializeProcThreadAttributeList(nullptr,1,0,&size);
        auto p=static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(),0,size));
        if (!p) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return false; }
        if (!InitializeProcThreadAttributeList(p,1,0,&size)) { DWORD e=GetLastError(); HeapFree(GetProcessHeap(),0,p); SetLastError(e); return false; }
        p_=p;
        return UpdateProcThreadAttribute(p_,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,bytes,nullptr,nullptr)!=FALSE;
    }
    LPPROC_THREAD_ATTRIBUTE_LIST get() const { return p_; }
};
inline bool AbsoluteExecutablePath(const std::wstring& path) {
    if (path.size()<7 || path.size()>32700 || !((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z')) || path[1]!=L':' || (path[2]!=L'\\'&&path[2]!=L'/')) return false;
    for (size_t i=0;i<path.size();++i) if (path[i]<32 || path[i]==L'"' || path[i]==L'*' || path[i]==L'?' || (i!=1&&path[i]==L':')) return false;
    std::wstring ext=path.substr(path.size()-4);
    for (wchar_t& c:ext) if (c>=L'A'&&c<=L'Z') c=static_cast<wchar_t>(c+L'a'-L'A');
    return ext==L".exe";
}
} // namespace detail
// Caller must supply an administrator-approved, trusted installed setupc.exe.
// This does not authenticate publisher/signature. Never discover executables via PATH.
// Fails closed if a private Job cannot be assigned (e.g. nested Win7 job).
inline CaptureResult CaptureInventory(const std::wstring& setupc, DWORD timeoutMs=5000, size_t maxBytes=1024*1024) {
    CaptureResult r;
    if (!detail::AbsoluteExecutablePath(setupc) || timeoutMs==0 || timeoutMs>30000 || maxBytes==0 || maxBytes>1024*1024) {
        r.status=CaptureStatus::InvalidInput; r.error="absolute approved .exe path and bounded limits required"; return r;
    }
    const auto fail=[&r](const char* message) { r.status=CaptureStatus::Win32Error; r.win32Error=GetLastError(); r.error=message; };
    SECURITY_ATTRIBUTES sa={sizeof(sa),nullptr,TRUE};
    HANDLE rd=nullptr,wr=nullptr;
    if (!CreatePipe(&rd,&wr,&sa,0)) { fail("CreatePipe"); return r; }
    detail::Handle read(rd), write(wr);
    if (!SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0)) { fail("pipe inheritance"); return r; }
    detail::Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if (input.get()==INVALID_HANDLE_VALUE) { fail("NUL input"); return r; }
    detail::Handle job(CreateJobObjectW(nullptr,nullptr));
    if (!job.get()) { fail("CreateJobObject"); return r; }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits))) { fail("job limit"); return r; }
    HANDLE inherited[]={wr,input.get()};
    detail::Attributes attrs;
    if (!attrs.init(inherited,sizeof(inherited))) { fail("explicit inherited handles"); return r; }
    STARTUPINFOEXW si={}; si.StartupInfo.cb=sizeof(si); si.lpAttributeList=attrs.get();
    si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput=input.get(); si.StartupInfo.hStdOutput=wr; si.StartupInfo.hStdError=wr;
    // Fixed read-only argv. No user strings are inserted other than quoted argv[0].
    std::wstring command=L"\""+setupc+L"\" --silent --detail-prms list";
    std::vector<wchar_t> commandLine(command.begin(),command.end()); commandLine.push_back(0);
    PROCESS_INFORMATION pi={};
    const size_t slash=setupc.find_last_of(L"/\\");
    const std::wstring directory=setupc.substr(0,slash+1);
    if (!CreateProcessW(setupc.c_str(),commandLine.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,directory.c_str(),&si.StartupInfo,&pi)) { fail("CreateProcessW exact executable"); return r; }
    detail::Handle process(pi.hProcess),thread(pi.hThread);
    if (!AssignProcessToJobObject(job.get(),process.get())) {
        fail("assign private job (Win7 nested jobs are unsupported)");
        TerminateProcess(process.get(),ERROR_CANCELLED); WaitForSingleObject(process.get(),1000); return r;
    }
    write.reset(); input.reset();
    if (ResumeThread(thread.get())==static_cast<DWORD>(-1)) { fail("ResumeThread"); return r; }
    thread.reset();
    const ULONGLONG started=GetTickCount64();
    for (;;) {
        if (GetTickCount64()-started>=timeoutMs) { r.status=CaptureStatus::Timeout; r.error="read-only list timed out; private job terminated"; break; }
        DWORD available=0;
        if (!PeekNamedPipe(rd,nullptr,0,nullptr,&available,nullptr)) {
            if (GetLastError()!=ERROR_BROKEN_PIPE) { fail("PeekNamedPipe"); break; }
            available=0;
        }
        if (available) {
            char buffer[4096]; DWORD got=0;
            const DWORD take=(std::min)(available,static_cast<DWORD>(sizeof(buffer)));
            if (!ReadFile(rd,buffer,take,&got,nullptr)) { fail("ReadFile"); break; }
            if (r.output.size()+got>maxBytes) { r.status=CaptureStatus::OutputLimit; r.error="list output exceeded limit"; break; }
            r.output.append(buffer,got); continue;
        }
        const DWORD wait=WaitForSingleObject(process.get(),5);
        if (wait==WAIT_TIMEOUT) continue;
        if (wait!=WAIT_OBJECT_0) { fail("process wait"); break; }
        // Drain again after exit, because bytes may have arrived between peek and wait.
        if (PeekNamedPipe(rd,nullptr,0,nullptr,&available,nullptr) && available) continue;
        if (!GetExitCodeProcess(process.get(),&r.exitCode)) { fail("process exit code"); break; }
        if (r.exitCode!=0) { r.status=CaptureStatus::NonzeroExit; r.error="setupc list returned nonzero"; break; }
        if (!ParseInventory(r.output,r.inventory,r.error)) { r.status=CaptureStatus::ParseError; break; }
        r.status=CaptureStatus::Ok; break;
    }
    // Only a read-only list process belongs to this private job. Never use this
    // termination policy for an install process. Job close also reaps descendants.
    job.reset();
    if (r.status!=CaptureStatus::Ok) { r.inventory.endpoints.clear(); WaitForSingleObject(process.get(),1000); }
    return r;
}


// Pure data preparation ONLY. This function cannot elevate or create a device.
// Caller must verify approved setupc identity/ACLs, no active bridge, both names
// unused (including reserved/offline devices), get explicit confirmation, then
// pass these separate fields to ShellExecuteExW. Never concatenate into cmd.exe.
struct AdminCreateCommand { std::wstring verb, file, parameters; };
inline bool BuildAdminCreateCommand(const std::wstring& approvedSetupc, const std::string& packaging,
    const std::string& bridge, AdminCreateCommand& out, std::string& error) {
    out=AdminCreateCommand{}; error.clear();
    const size_t slash=approvedSetupc.find_last_of(L"/\\");
    std::wstring basename=approvedSetupc.substr(slash==std::wstring::npos?0:slash+1);
    for (wchar_t& c:basename) if (c>=L'A'&&c<=L'Z') c=static_cast<wchar_t>(c+L'a'-L'A');
    if (!detail::AbsoluteExecutablePath(approvedSetupc) || basename!=L"setupc.exe") {
        error="administrator-approved absolute setupc.exe path required"; return false;
    }
    if (!IsCanonicalCom(packaging) || !IsCanonicalCom(bridge) || packaging==bridge) {
        error="two different canonical COM names required"; return false;
    }
    out.verb=L"runas"; out.file=approvedSetupc;
    out.parameters=L"install PortName="+std::wstring(packaging.begin(),packaging.end())+
        L",EmuOverrun=yes PortName="+std::wstring(bridge.begin(),bridge.end())+L",dsr=ropen,EmuBR=yes";
    return true;
}

} // namespace com0com
#endif
