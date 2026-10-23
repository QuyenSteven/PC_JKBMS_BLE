#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwchar>

namespace dashboard {
constexpr int ID_DASH=2101, ID_CELLS=2102, ID_CONTROL=2103, ID_SETTINGS=2104, ID_LOG=2105;
enum Page { Overview, Cells, Control, Settings, Log };
inline Page page=Overview;
inline HWND host{}, title{}, subtitle{}, statVoltage{}, statCurrent{}, statPower{}, statSoc{}, statMin{}, statMax{}, statDelta{}, statCharge{}, statDischarge{}, statBalance{}, notice{}, cellSummary{};
inline HWND nav[5]{};
inline HFONT fontSmall{}, fontMedium{}, fontLarge{}, fontHeader{};
inline HBRUSH darkBrush{}, panelBrush{}, inputBrush{};
inline std::wstring telemetry=L"No telemetry received yet";
inline std::vector<double> cellVoltages;
inline COLORREF bg=RGB(14,20,33), panel=RGB(24,34,52), fg=RGB(226,235,247), muted=RGB(142,164,191), accent=RGB(38,207,173);
inline HFONT makeFont(int px,int weight){return CreateFontW(-px,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");}
inline void setFont(HWND h,HFONT f){if(h)SendMessageW(h,WM_SETFONT,(WPARAM)f,TRUE);}
inline HWND make(HWND parent,wchar_t const* klass,wchar_t const* text,DWORD style,int id,HFONT font){
 HWND h=CreateWindowExW(0,klass,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
 setFont(h,font);return h;
}
inline void place(HWND h,int x,int y,int w,int height,bool visible=true){
 if(!h)return;ShowWindow(h,visible?SW_SHOW:SW_HIDE);if(visible)MoveWindow(h,x,y,std::max(1,w),std::max(1,height),TRUE);
}
inline void setup(HWND parent,HWND& devices,HWND& values,HWND& logs,HWND& proto){
 host=parent;
 darkBrush=CreateSolidBrush(bg);panelBrush=CreateSolidBrush(panel);inputBrush=CreateSolidBrush(RGB(20,29,45));
 fontSmall=makeFont(14,FW_NORMAL);fontMedium=makeFont(17,FW_SEMIBOLD);fontLarge=makeFont(26,FW_BOLD);fontHeader=makeFont(30,FW_BOLD);
 title=make(parent,L"STATIC",L"JK BMS STUDIO",SS_LEFT,0,fontHeader);
 subtitle=make(parent,L"STATIC",L"Desktop battery management  /  Bluetooth Low Energy",SS_LEFT,0,fontSmall);
 wchar_t const* labels[]={L"Overview",L"Cells",L"Control",L"Protection",L"BLE Logs"};
 for(int i=0;i<5;i++)nav[i]=make(parent,L"BUTTON",labels[i],BS_PUSHBUTTON,ID_DASH+i,fontMedium);
 statVoltage=make(parent,L"STATIC",L"--.- V",SS_CENTER,0,fontLarge);
 statCurrent=make(parent,L"STATIC",L"--.- A",SS_CENTER,0,fontLarge);
 statPower=make(parent,L"STATIC",L"-- W",SS_CENTER,0,fontLarge);
 statSoc=make(parent,L"STATIC",L"-- %",SS_CENTER,0,fontLarge);
 statMin=make(parent,L"STATIC",L"Min: --",0,0,fontMedium);
 statMax=make(parent,L"STATIC",L"Max: --",0,0,fontMedium);
 statDelta=make(parent,L"STATIC",L"Delta: --",0,0,fontMedium);
 statCharge=make(parent,L"STATIC",L"Charge MOS: --",0,0,fontMedium);
 statDischarge=make(parent,L"STATIC",L"Discharge MOS: --",0,0,fontMedium);
 statBalance=make(parent,L"STATIC",L"Balancing: --",0,0,fontMedium);
 notice=make(parent,L"STATIC",L"READ ONLY  |  Protection writes, MOSFET controls and reboot require verified device protocol.",SS_LEFT,0,fontSmall);
 cellSummary=make(parent,L"STATIC",L"Waiting for cell data...",0,0,fontMedium);
 devices=make(parent,L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|LBS_NOTIFY,105,fontSmall);
 values=make(parent,L"EDIT",L"Connect to JK BMS to begin live monitoring.",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,fontSmall);
 logs=make(parent,L"EDIT",L"",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,fontSmall);
 proto=make(parent,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,106,fontSmall);
 for(auto p:{L"JK02_32S",L"JK02_24S",L"JK04"})SendMessageW(proto,CB_ADDSTRING,0,(LPARAM)p);
 SendMessageW(proto,CB_SETCURSEL,0,0);
}
inline void layout(int w,int h,HWND devices,HWND values,HWND logs,HWND proto){
 if(w<1||h<1)return;
 const int left=190, x=left+28, area=std::max(300,w-x-24);
 place(title,24,22,165,42);place(subtitle,x,29,area,26);
 for(int i=0;i<5;i++)place(nav[i],16,104+i*54,158,44);
 place(GetDlgItem(host,101),x,78,104,36);
 place(GetDlgItem(host,102),x+112,78,108,36);
 place(GetDlgItem(host,103),x+228,78,112,36);
 place(GetDlgItem(host,104),x+348,78,106,36);
 place(proto,x+462,78,std::max(120,area-462),180);
 const bool showOverview=page==Overview, showCells=page==Cells, showControl=page==Control, showSettings=page==Settings, showLogs=page==Log;
 place(devices,x,132,area,93,true);
 int top=254, gap=12, quarter=(area-3*gap)/4;
 place(statVoltage,x+8,top+36,quarter-16,60,showOverview);
 place(statCurrent,x+quarter+gap+8,top+36,quarter-16,60,showOverview);
 place(statPower,x+(quarter+gap)*2+8,top+36,quarter-16,60,showOverview);
 place(statSoc,x+(quarter+gap)*3+8,top+36,quarter-16,60,showOverview);
 place(statMin,x+20,top+155,area/3-20,30,showOverview||showCells);
 place(statMax,x+area/3+20,top+155,area/3-20,30,showOverview||showCells);
 place(statDelta,x+area*2/3+20,top+155,area/3-30,30,showOverview||showCells);
 place(statCharge,x+20,top+212,area/3-20,32,showOverview||showControl);
 place(statDischarge,x+area/3+20,top+212,area/3-20,32,showOverview||showControl);
 place(statBalance,x+area*2/3+20,top+212,area/3-30,32,showOverview||showControl);
 place(cellSummary,x+16,264,area-32,30,showCells);
 place(values,x+6,showSettings?258:showControl?350:showCells?std::min(h-190,650):showOverview?560:258,area-12,showOverview?std::max(75,h-650):showCells?std::max(80,h-730):showControl?std::max(120,h-440):std::max(130,h-350),showOverview||showCells||showControl||showSettings);
 place(logs,x+6,258,area-12,std::max(160,h-350),showLogs);
 place(notice,x+4,h-47,area-8,28,true);
 InvalidateRect(host,nullptr,TRUE);
}
inline std::wstring field(std::wstring const& input,std::wstring const& key){
 auto start=input.find(key);if(start==std::wstring::npos)return L"--";
 start+=key.size();auto end=input.find_first_of(L"\r\n|",start);
 auto v=input.substr(start,end==std::wstring::npos?std::wstring::npos:end-start);
 while(!v.empty()&&v.front()==L' ')v.erase(v.begin());
 while(!v.empty()&&v.back()==L' ')v.pop_back();
 return v.empty()?L"--":v;
}
inline void update(std::wstring const& text){
 telemetry=text;cellVoltages.clear();
 for(size_t p=0;(p=text.find(L"Cell ",p))!=std::wstring::npos;){
  auto colon=text.find(L": ",p);if(colon==std::wstring::npos)break;
  double v=0;if(swscanf_s(text.c_str()+colon+2,L"%lf",&v)==1&&v>=0.5&&v<=5.5)cellVoltages.push_back(v);
  p=colon+2;
 }
 auto volts=field(text,L"Pack "), amps=field(text,L" | "), soc=field(text,L"SOC ");
 if(volts!=L"--"){auto end=volts.find(L" V");if(end!=std::wstring::npos)volts.resize(end+2);}
 auto currentStart=text.find(L" V | ");
 if(currentStart!=std::wstring::npos){currentStart+=5;auto end=text.find(L" | ",currentStart);amps=text.substr(currentStart,end-currentStart);}
 double v=0,a=0;swscanf_s(volts.c_str(),L"%lf",&v);swscanf_s(amps.c_str(),L"%lf",&a);
 wchar_t buffer[96]{};
 SetWindowTextW(statVoltage,volts.c_str());SetWindowTextW(statCurrent,amps.c_str());
 swprintf_s(buffer,L"%.1f W",v*a);SetWindowTextW(statPower,buffer);
 SetWindowTextW(statSoc,soc.c_str());
 SetWindowTextW(statCharge,(L"Charge MOS: "+field(text,L"Charge MOS ")).c_str());
 SetWindowTextW(statDischarge,(L"Discharge MOS: "+field(text,L"Discharge MOS ")).c_str());
 SetWindowTextW(statBalance,(L"Balancing: "+field(text,L"Balance ")).c_str());
 if(!cellVoltages.empty()){
  auto mm=std::minmax_element(cellVoltages.begin(),cellVoltages.end());
  swprintf_s(buffer,L"Min: %.3f V",*mm.first);SetWindowTextW(statMin,buffer);
  swprintf_s(buffer,L"Max: %.3f V",*mm.second);SetWindowTextW(statMax,buffer);
  swprintf_s(buffer,L"Delta: %.0f mV",(*mm.second-*mm.first)*1000);SetWindowTextW(statDelta,buffer);
  swprintf_s(buffer,L"%zu cells detected | Live voltages",cellVoltages.size());SetWindowTextW(cellSummary,buffer);
 }
 InvalidateRect(host,nullptr,FALSE);
}
inline void fillRound(HDC dc,RECT r,COLORREF color,int radius=16){
 HBRUSH brush=CreateSolidBrush(color);HPEN pen=CreatePen(PS_SOLID,1,color);
 auto oldB=SelectObject(dc,brush);auto oldP=SelectObject(dc,pen);
 RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);SelectObject(dc,oldP);SelectObject(dc,oldB);
 DeleteObject(pen);DeleteObject(brush);
}
inline void text(HDC dc,int x,int y,wchar_t const* str,COLORREF color,HFONT font){
 SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);SelectObject(dc,font);TextOutW(dc,x,y,str,(int)wcslen(str));
}
inline void paint(HDC dc,RECT client){
 FillRect(dc,&client,darkBrush);
 fillRound(dc,{0,0,190,client.bottom},RGB(18,28,44),0);
 text(dc,24,78,L"BATTERY MANAGER",accent,fontSmall);
 for(int i=0;i<5;i++)if(static_cast<int>(page)==i)fillRound(dc,{10,101+i*54,180,149+i*54},RGB(28,75,85),10);
 int x=218,w=client.right-x-24;
 if(w<300)return;
 fillRound(dc,{x,122,x+w,233},panel);
 if(page==Overview){
  text(dc,x+18,239,L"LIVE BATTERY STATUS",muted,fontSmall);
  int gap=12,q=(w-3*gap)/4;
  for(int i=0;i<4;i++)fillRound(dc,{x+i*(q+gap),254,x+i*(q+gap)+q,373},panel);
  wchar_t const* names[]={L"PACK VOLTAGE",L"CURRENT",L"POWER",L"STATE OF CHARGE"};
  for(int i=0;i<4;i++)text(dc,x+i*(q+gap)+14,265,names[i],muted,fontSmall);
  fillRound(dc,{x,397,x+w,540},panel);
  text(dc,x+14,408,L"CELL BALANCE / MOSFET STATUS",muted,fontSmall);
  text(dc,x+6,541,L"DEVICE TELEMETRY",muted,fontSmall);
 } else if(page==Cells){
  text(dc,x+12,234,L"CELL VOLTAGE COMPARISON",muted,fontMedium);
  fillRound(dc,{x,298,x+w,std::min(static_cast<int>(client.bottom)-184,638)},panel);
  int baseline=std::min(static_cast<int>(client.bottom)-203,610), graphH=220;
  if(!cellVoltages.empty()&&baseline>345){
   auto mm=std::minmax_element(cellVoltages.begin(),cellVoltages.end());double lo=std::max(0.0,*mm.first-0.06),hi=*mm.second+0.06;
   int barWidth=std::max(4,(w-40)/(int)cellVoltages.size()-5);
   for(size_t i=0;i<cellVoltages.size();i++){
    int bx=x+20+(int)(i*(w-40)/cellVoltages.size()),bh=(int)((cellVoltages[i]-lo)/(hi-lo)*graphH);
    fillRound(dc,{bx,baseline-bh,bx+barWidth,baseline},cellVoltages[i]==*mm.first?RGB(248,171,85):cellVoltages[i]==*mm.second?RGB(236,111,114):accent,5);
    if(cellVoltages.size()<=24){wchar_t buf[12];swprintf_s(buf,L"%zu",i+1);text(dc,bx,baseline+7,buf,muted,fontSmall);}
   }
  } else text(dc,x+18,336,L"No valid cell measurements yet. Connect and select the correct JK protocol.",muted,fontMedium);
 } else if(page==Control || page==Settings){
  fillRound(dc,{x,243,x+w,328},panel);
  text(dc,x+18,258,page==Control?L"MOSFET / BALANCING CONTROLS":L"PROTECTION / CONFIGURATION",fg,fontMedium);
  text(dc,x+18,290,L"Read-only mode: write commands disabled until verified against model and firmware.",RGB(242,177,91),fontSmall);
 }else{
  fillRound(dc,{x,240,x+w,client.bottom-66},panel);
  text(dc,x+16,241,L"BLE CONNECTION LOG / DIAGNOSTICS",muted,fontSmall);
 }
}
inline HBRUSH brushFor(UINT msg){return msg==WM_CTLCOLOREDIT?inputBrush:darkBrush;}
inline void shutdown(){
 for(auto f:{fontSmall,fontMedium,fontLarge,fontHeader})if(f)DeleteObject(f);
 for(auto b:{darkBrush,panelBrush,inputBrush})if(b)DeleteObject(b);
}
}
