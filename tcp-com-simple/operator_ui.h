#ifndef SR5000_OPERATOR_UI_H
#define SR5000_OPERATOR_UI_H
#include <windows.h>
#include <string>
#include <algorithm>
namespace operatorui {
const COLORREF Background=RGB(245,247,251),Paper=RGB(255,255,255),Ink=RGB(30,41,59),Muted=RGB(106,119,139),Line=RGB(225,231,240),Blue=RGB(42,102,241),Green=RGB(12,151,106),Red=RGB(211,62,69);
static HBITMAP logo=nullptr;static double scale=1.0;static HFONT regular=nullptr,strong=nullptr,title=nullptr,large=nullptr,mono=nullptr;static HBRUSH background=nullptr,paper=nullptr,alert=nullptr;
inline int px(int v){return (int)(v*scale+.5);}
inline HFONT makeFont(int height,int weight,const wchar_t* face=L"Microsoft YaHei UI"){return CreateFontW(-px(height),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);}
inline void release(){if(logo){DeleteObject(logo);logo=nullptr;}for(HFONT f:{regular,strong,title,large,mono})if(f)DeleteObject(f);regular=strong=title=large=mono=nullptr;for(HBRUSH b:{background,paper,alert})if(b)DeleteObject(b);background=paper=alert=nullptr;}
inline void initialize(int width,int height){
    scale=(std::min)(1.5,(std::min)(width/960.0,height/660.0));if(scale<.65)scale=.65;
    logo=(HBITMAP)LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
    regular=makeFont(16,FW_NORMAL);strong=makeFont(17,FW_SEMIBOLD);title=makeFont(24,FW_SEMIBOLD);large=makeFont(23,FW_SEMIBOLD);mono=makeFont(14,FW_NORMAL,L"Consolas");
    background=CreateSolidBrush(Background);paper=CreateSolidBrush(Paper);alert=CreateSolidBrush(RGB(255,242,242));
}
inline RECT box(int x,int y,int w,int h){RECT r={px(x),px(y),px(x+w),px(y+h)};return r;}
inline void rounded(HDC dc,int x,int y,int w,int h,COLORREF fill,COLORREF border,int radius=12){
    HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,border);HGDIOBJ oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
    RoundRect(dc,px(x),px(y),px(x+w),px(y+h),px(radius),px(radius));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
}
inline void label(HDC dc,const std::wstring& value,int x,int y,int w,int h,HFONT f,COLORREF color,UINT format=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){
    RECT r=box(x,y,w,h);HGDIOBJ old=SelectObject(dc,f);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,value.c_str(),(int)value.size(),&r,format);SelectObject(dc,old);
}
inline void dot(HDC dc,int x,int y,COLORREF color){HBRUSH b=CreateSolidBrush(color);HGDIOBJ old=SelectObject(dc,b),pen=SelectObject(dc,GetStockObject(NULL_PEN));Ellipse(dc,px(x),px(y),px(x+8),px(y+8));SelectObject(dc,pen);SelectObject(dc,old);DeleteObject(b);}
inline void paint(HDC dc,int width,int height,bool fault){
    RECT bounds={0,0,width,height};FillRect(dc,&bounds,background);RECT top=box(0,0,960,68);FillRect(dc,&top,paper);
    if(logo){HDC source=CreateCompatibleDC(dc);HGDIOBJ old=SelectObject(source,logo);SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,px(22),px(7),px(54),px(54),source,0,0,96,96,SRCCOPY);SelectObject(source,old);DeleteDC(source);}
    label(dc,L"扫码串口桥接",90,16,240,38,title,Ink);label(dc,L"SR-5000",342,20,125,32,strong,Muted);
    for(int i=0;i<2;++i)rounded(dc,24+i*468,86,444,96,Paper,Line);
    label(dc,L"TCP 连接",44,98,180,24,regular,Muted);label(dc,L"串口转发",512,98,180,24,regular,Muted);
    rounded(dc,24,192,912,28,fault?RGB(255,242,242):RGB(235,240,248),fault?RGB(255,225,225):RGB(235,240,248),8);
    rounded(dc,24,230,444,218,Paper,Line);rounded(dc,492,230,444,218,Paper,Line);
    label(dc,L"扫码枪 TCP",44,239,180,28,strong,Ink);label(dc,L"IPv4 地址",44,278,98,28,regular,Muted);
    label(dc,L"通讯端口",44,320,96,28,regular,Muted);label(dc,L"触发内容",44,362,96,28,regular,Muted);
    label(dc,L"串口输出",512,239,116,28,strong,Ink);label(dc,L"桥接端",512,278,78,28,regular,Muted);label(dc,L"软件端",710,278,78,28,regular,Muted);
    label(dc,L"波特率",512,317,104,24,regular,Muted);label(dc,L"数据位",638,317,65,24,regular,Muted);label(dc,L"校验",714,317,95,24,regular,Muted);label(dc,L"停止位",842,317,70,24,regular,Muted);
    rounded(dc,24,510,912,132,Paper,Line);label(dc,L"运行日志",44,518,180,28,strong,Ink);
}
inline bool drawButton(DRAWITEMSTRUCT* item,UINT primary){
    if(item->CtlType!=ODT_BUTTON)return false;HDC dc=item->hDC;RECT r=item->rcItem;bool main=item->CtlID==primary,disabled=(item->itemState&ODS_DISABLED)!=0,pressed=(item->itemState&ODS_SELECTED)!=0;
    COLORREF fill=disabled?RGB(238,241,246):(main?(pressed?RGB(26,79,206):Blue):(pressed?RGB(237,242,252):Paper));COLORREF border=main?fill:Line;
    HBRUSH b=CreateSolidBrush(fill);HPEN p=CreatePen(PS_SOLID,1,border);HGDIOBJ oldB=SelectObject(dc,b),oldP=SelectObject(dc,p);RoundRect(dc,r.left,r.top,r.right,r.bottom,px(9),px(9));SelectObject(dc,oldB);SelectObject(dc,oldP);DeleteObject(b);DeleteObject(p);
    wchar_t text[160]={};GetWindowTextW(item->hwndItem,text,160);HGDIOBJ oldF=SelectObject(dc,strong);SetTextColor(dc,disabled?Muted:(main?Paper:Ink));SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,oldF);
    if(item->itemState&ODS_FOCUS){InflateRect(&r,-4,-4);DrawFocusRect(dc,&r);}return true;
}
inline void position(HWND parent,UINT id,int x,int y,int w,int h){MoveWindow(GetDlgItem(parent,(int)id),px(x),px(y),px(w),px(h),TRUE);}
}
#endif
