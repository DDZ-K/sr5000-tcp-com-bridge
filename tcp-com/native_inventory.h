#ifndef SR5000_NATIVE_INVENTORY_H
#define SR5000_NATIVE_INVENTORY_H
#include <windows.h>
#include <setupapi.h>
#include <string>
#include <vector>
#include "com0com_inventory.h"
#pragma comment(lib,"setupapi.lib")
// Read-only evidence, based on bundled com0com 3.0.0.0 include/com0com.h,
// sys/adddev.c and setup/params.cpp: PDO A/B names, parameter key and DSR bit.
namespace com0com {
inline std::wstring DiagnosticText(const std::string& value){
    if(value.empty())return L"";
    int count=MultiByteToWideChar(CP_ACP,0,value.data(),(int)value.size(),nullptr,0);
    std::wstring result((size_t)count,L' ');if(count)MultiByteToWideChar(CP_ACP,0,value.data(),(int)value.size(),&result[0],count);return result;
}
inline std::wstring DescribeCaptureFailure(const CaptureResult& r){
    std::wstring stage;
    switch(r.status){
    case CaptureStatus::InvalidInput:stage=L"参数校验失败";break;
    case CaptureStatus::Win32Error:stage=L"启动/读取列表失败";break;
    case CaptureStatus::Timeout:stage=L"读取列表超时";break;
    case CaptureStatus::OutputLimit:stage=L"列表超过长度上限";break;
    case CaptureStatus::NonzeroExit:stage=L"列表程序退出失败，退出码 "+std::to_wstring(r.exitCode);break;
    case CaptureStatus::ParseError:stage=L"列表解析失败";break;
    default:stage=L"未知列表状态";break;
    }
    if(r.win32Error){stage+=L"，Windows错误 "+std::to_wstring(r.win32Error);if(r.win32Error==ERROR_ELEVATION_REQUIRED||r.win32Error==ERROR_ACCESS_DENIED)stage+=L"（此步骤需要管理员权限）";}
    if(!r.error.empty())stage+=L"；细节："+DiagnosticText(r.error);
    if(!r.output.empty())stage+=L"；原始输出：\r\n"+DiagnosticText(r.output.substr(0,2048));
    return stage;
}
inline bool EndpointIdFromTarget(const std::wstring& target,std::wstring& id){
    id.clear();const std::wstring prefix=LR"(\Device\com0com)";
    if(target.size()<=prefix.size()+1||_wcsnicmp(target.c_str(),prefix.c_str(),prefix.size())!=0)return false;
    const wchar_t side=target[prefix.size()];if(side!=L'1'&&side!=L'2')return false;
    std::wstring number=target.substr(prefix.size()+1);
    if(number.empty()||number.size()>10||(number.size()>1&&number[0]==L'0'))return false;
    for(wchar_t c:number)if(c<L'0'||c>L'9')return false;
    if(number.size()==10&&number>L"2147483647")return false;
    id=(side==L'1'?L"CNCA":L"CNCB")+number;return true;
}
inline LONG ReadRegString(HKEY key,const wchar_t* name,std::wstring& value){
    value.clear();wchar_t buffer[1024]={};DWORD bytes=sizeof(buffer),type=0;
    LONG code=RegQueryValueExW(key,name,nullptr,&type,(BYTE*)buffer,&bytes);
    if(code!=ERROR_SUCCESS)return code;
    if(type!=REG_SZ||bytes<sizeof(wchar_t)||bytes>sizeof(buffer)||bytes%sizeof(wchar_t)||buffer[bytes/sizeof(wchar_t)-1]!=0)return ERROR_INVALID_DATA;
    value=buffer;if((value.size()+1)*sizeof(wchar_t)!=bytes)return ERROR_INVALID_DATA;return ERROR_SUCCESS;
}
inline LONG ReadRegDword(HKEY key,const wchar_t* name,DWORD fallback,DWORD& value){
    value=fallback;DWORD size=sizeof(value),type=0;LONG code=RegQueryValueExW(key,name,nullptr,&type,(BYTE*)&value,&size);
    if(code==ERROR_FILE_NOT_FOUND){value=fallback;return ERROR_SUCCESS;}
    if(code!=ERROR_SUCCESS)return code;
    return type==REG_DWORD&&size==sizeof(value)?ERROR_SUCCESS:ERROR_INVALID_DATA;
}
inline bool ConfirmClassPortAlias(const std::wstring& target,const std::wstring& port){
    HDEVINFO devices=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES);if(devices==INVALID_HANDLE_VALUE)return false;
    bool confirmed=false;SP_DEVINFO_DATA device={};device.cbSize=sizeof(device);
    for(DWORD i=0;i<16384&&SetupDiEnumDeviceInfo(devices,i,&device);++i){
        wchar_t name[1024]={};DWORD type=0;
        if(!SetupDiGetDeviceRegistryPropertyW(devices,&device,SPDRP_PHYSICAL_DEVICE_OBJECT_NAME,&type,(BYTE*)name,sizeof(name),nullptr)||type!=REG_SZ||_wcsicmp(name,target.c_str()))continue;
        HKEY key=SetupDiOpenDevRegKey(devices,&device,DICS_FLAG_GLOBAL,0,DIREG_DEV,KEY_QUERY_VALUE);
        if(key==INVALID_HANDLE_VALUE)continue;
        std::wstring actual;LONG code=ReadRegString(key,L"PortName",actual);RegCloseKey(key);
        if(code==ERROR_SUCCESS&&actual==port){confirmed=true;break;}
    }
    SetupDiDestroyDeviceInfoList(devices);return confirmed;
}
// The caller supplies live DOS-device evidence. Tests use a temporary HKCU key;
// production always supplies the fixed HKLM service key below and live aliases.
inline bool NativeEndpointFromEvidence(HKEY parameters,const std::wstring& port,const std::wstring& target,bool classAliasConfirmed,Endpoint& endpoint,std::wstring& error){
    endpoint=Endpoint{};std::wstring id;if(!IsCanonicalCom(std::string(port.begin(),port.end()))||!EndpointIdFromTarget(target,id)){error=L"所选COM不是可核验的com0com设备";return false;}
    HKEY key=nullptr;LONG code=RegOpenKeyExW(parameters,id.c_str(),0,KEY_QUERY_VALUE,&key);
    if(code!=ERROR_SUCCESS){error=L"读取 "+id+L" 参数失败，Windows错误 "+std::to_wstring(code);return false;}
    std::wstring configured;DWORD overrun=0,baud=0,dsr=2;
    code=ReadRegString(key,L"PortName",configured);
    if(code==ERROR_SUCCESS)code=ReadRegDword(key,L"EmuOverrun",0,overrun);
    if(code==ERROR_SUCCESS)code=ReadRegDword(key,L"EmuBR",0,baud);
    if(code==ERROR_SUCCESS)code=ReadRegDword(key,L"dsr",2,dsr);
    RegCloseKey(key);
    if(code!=ERROR_SUCCESS||overrun>1||baud>1){error=L"系统参数格式/读取失败，Windows错误 "+std::to_wstring(code==ERROR_SUCCESS?ERROR_INVALID_DATA:code);return false;}
    if(configured!=port&&(configured!=L"COM#"||!classAliasConfirmed)){error=L"COM名称与驱动参数/系统设备映射不一致，未接受配对";return false;}
    endpoint.id=std::string(id.begin(),id.end());endpoint.pairId=endpoint.id.substr(4);endpoint.side=endpoint.id[3];endpoint.portName=std::string(port.begin(),port.end());
    endpoint.parameters["portname"]=endpoint.portName;endpoint.parameters["emuoverrun"]=overrun?"yes":"no";endpoint.parameters["emubr"]=baud?"yes":"no";endpoint.parameters["dsr"]=dsr==0x80?"ropen":(dsr==2?"rdtr":"other");
    return true;
}
inline bool CaptureNativeSelected(const std::wstring& application,const std::wstring& bridge,Inventory& inventory,std::wstring& error){
    inventory.endpoints.clear();error.clear();HKEY parameters=nullptr;
    LONG code=RegOpenKeyExW(HKEY_LOCAL_MACHINE,LR"(SYSTEM\CurrentControlSet\Services\com0com\Parameters)",0,KEY_QUERY_VALUE|KEY_ENUMERATE_SUB_KEYS|KEY_WOW64_64KEY,&parameters);
    if(code!=ERROR_SUCCESS){error=L"读取系统com0com参数失败，Windows错误 "+std::to_wstring(code);return false;}
    bool ok=true;
    for(const auto& port:{application,bridge}){
        if(!IsCanonicalCom(std::string(port.begin(),port.end()))){error=L"COM号格式无效";ok=false;break;}
        wchar_t target[4096]={};DWORD used=QueryDosDeviceW(port.c_str(),target,4096);
        if(!used){error=L"系统不存在/不能查询 "+port+L"，Windows错误 "+std::to_wstring(GetLastError());ok=false;break;}
        std::wstring id;if(!EndpointIdFromTarget(target,id)){error=port+L" 的设备目标不属于可核验的com0com";ok=false;break;}
        HKEY key=nullptr;std::wstring configured;code=RegOpenKeyExW(parameters,id.c_str(),0,KEY_QUERY_VALUE,&key);
        if(code==ERROR_SUCCESS){code=ReadRegString(key,L"PortName",configured);RegCloseKey(key);}
        if(code!=ERROR_SUCCESS){error=L"读取 "+id+L" 名称失败，Windows错误 "+std::to_wstring(code);ok=false;break;}
        bool classAlias=configured==L"COM#"&&ConfirmClassPortAlias(target,port);
        Endpoint endpoint;if(!NativeEndpointFromEvidence(parameters,port,target,classAlias,endpoint,error)){ok=false;break;}
        inventory.endpoints.push_back(endpoint);
    }
    RegCloseKey(parameters);if(!ok)inventory.endpoints.clear();return ok;
}
}
#endif
