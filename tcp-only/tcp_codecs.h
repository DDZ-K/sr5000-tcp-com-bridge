#ifndef SR5000_TCP_CODECS_H
#define SR5000_TCP_CODECS_H
#include <string>
#include <vector>
#include <cwctype>
namespace tcpcodecs {
int digit(wchar_t c){if(c>=L'0'&&c<=L'9')return c-L'0';c=(wchar_t)towupper(c);if(c>=L'A'&&c<=L'F')return c-L'A'+10;return -1;}
bool hex(const std::wstring& s,std::vector<unsigned char>& out){
    out.clear();std::vector<unsigned char> parsed;int hi=-1;
    for(wchar_t ch:s){
        if(ch==L' '||ch==L'\t'||ch==L'-'){
            if(hi>=0)return false;
            continue;
        }
        int d=digit(ch);if(d<0)return false;
        if(hi<0)hi=d;else{parsed.push_back((unsigned char)(hi*16+d));hi=-1;}
        if(parsed.size()>64)return false;
    }
    if(hi>=0||parsed.empty())return false;
    out.swap(parsed);return true;
}
std::wstring formatHex(const std::vector<unsigned char>& bytes){
    const wchar_t* digits=L"0123456789ABCDEF";std::wstring result;
    for(unsigned char b:bytes){if(!result.empty())result+=L' ';result+=digits[b>>4];result+=digits[b&15];}return result;
}
std::wstring formatAscii(const std::vector<unsigned char>& bytes){
    const wchar_t* digits=L"0123456789ABCDEF";std::wstring result;
    for(unsigned char b:bytes){
        if(b=='\\')result+=L"\\\\";
        else if(b=='\r')result+=L"\\r";
        else if(b=='\n')result+=L"\\n";
        else if(b=='\t')result+=L"\\t";
        else if(b==0)result+=L"\\0";
        else if(b>=32&&b<=126)result+=(wchar_t)b;
        else{result+=L"\\x";result+=digits[b>>4];result+=digits[b&15];}
    }return result;
}
bool parseAscii(const std::wstring& text,std::vector<unsigned char>& out){
    out.clear();std::vector<unsigned char> parsed;
    for(size_t i=0;i<text.size();++i){
        wchar_t ch=text[i];
        if(ch==L'\\'){
            if(++i==text.size())return false;ch=text[i];
            if(ch==L'r')ch=L'\r';else if(ch==L'n')ch=L'\n';else if(ch==L't')ch=L'\t';else if(ch==L'0')ch=0;
            else if(ch==L'x'){
                if(i+2>=text.size())return false;int high=digit(text[i+1]),low=digit(text[i+2]);
                if(high<0||low<0)return false;ch=(wchar_t)(high*16+low);i+=2;
            }else if(ch!=L'\\')return false;
        }else if(ch<32||ch>126)return false;
        parsed.push_back((unsigned char)ch);if(parsed.size()>64)return false;
    }
    if(parsed.empty())return false;out.swap(parsed);return true;
}
bool parseTrigger(const std::wstring& text,bool hexMode,std::vector<unsigned char>& out){
    return hexMode?hex(text,out):parseAscii(text,out);
}
UINT keyCode(const std::wstring& name){
    if(!_wcsicmp(name.c_str(),L"Enter"))return VK_RETURN;
    if(!_wcsicmp(name.c_str(),L"Space"))return VK_SPACE;
    if(name.size()>=2&&(name[0]==L'F'||name[0]==L'f')){
        int n=0;for(size_t i=1;i<name.size();++i){if(name[i]<L'0'||name[i]>L'9')return 0;n=n*10+(name[i]-L'0');}
        if(n>=1&&n<=12)return VK_F1+n-1;
    }return 0;
}
}
#endif
