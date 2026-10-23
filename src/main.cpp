#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <winrt/base.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#pragma comment(lib,"Comctl32.lib")
#pragma comment(lib,"Comdlg32.lib")
#pragma comment(lib,"WindowsApp.lib")
using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;
constexpr UINT WM_FOUND=WM_APP+1, WM_LOG=WM_APP+2, WM_VALUES=WM_APP+3;
constexpr int SCAN=101, CONNECT=102, DISCONNECT=103, SAVE=104, DEVICES=105, PROTOCOL=106;
HWND windowHandle{}, devicesList{}, valuesBox{}, logBox{}, protocolBox{};
std::mutex devicesMutex, frameMutex, logMutex;
std::map<std::wstring,uint64_t> discovered;
std::vector<uint8_t> frameBuffer, rawCapture;
std::atomic<bool> active{false};
std::atomic<unsigned> seq{0};
BluetoothLEAdvertisementWatcher watcher{nullptr};
BluetoothLEDevice device{nullptr};
GattCharacteristic rx{nullptr}, tx{nullptr};
event_token watchToken{}, notifyToken{};
const guid serviceUuid{0x0000FFE0,0x0000,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB}};
const guid charUuid{0x0000FFE1,0x0000,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB}};
void post(UINT type,std::wstring message){if(windowHandle&&IsWindow(windowHandle)) PostMessageW(windowHandle,type,0,reinterpret_cast<LPARAM>(new std::wstring(std::move(message))));}
void log(std::wstring text){post(WM_LOG,std::move(text));}
std::wstring utf8(std::string text){if(text.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,text.data(),(int)text.size(),nullptr,0);std::wstring output(n,L' ');MultiByteToWideChar(CP_UTF8,0,text.data(),(int)text.size(),output.data(),n);return output;}
void exception(char const* context){try{throw;}catch(hresult_error const& e){log(utf8(context)+L": "+std::wstring(e.message().c_str()));}catch(std::exception const& e){log(utf8(context)+L": "+utf8(e.what()));}catch(...){log(utf8(context)+L": unknown error");}}
uint16_t u16(std::vector<uint8_t> const& b,size_t p){return p+1<b.size()?uint16_t(b[p]|b[p+1]<<8):0;}
uint32_t u32(std::vector<uint8_t> const& b,size_t p){return p+3<b.size()?uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24):0;}
float f32(std::vector<uint8_t> const& b,size_t p){uint32_t bits=u32(b,p);float value{};memcpy(&value,&bits,4);return value;}
std::wstring parse(std::vector<uint8_t> const& b,int mode){
 if(b.size()<300||b[4]!=2)return L"";
 std::wostringstream os;os<<std::fixed<<std::setprecision(3);
 if(mode==2){os<<L"JK04 experimental cell decode\r\n";for(int i=0;i<24;i++){float v=f32(b,6+4*i);if(std::isfinite(v)&&v>.5f&&v<5.5f)os<<L"Cell "<<i+1<<L": "<<v<<L" V\r\n";}return os.str();}
 int delta=mode==1?-16:0;double voltage=u32(b,150+delta)/1000.0;double current=(int32_t)u32(b,154+delta)/1000.0;
 if(voltage>2000||std::abs(current)>50000)return L"Invalid frame values";
 os<<L"Pack "<<voltage<<L" V | "<<current<<L" A | "<<voltage*current<<L" W\r\n";
 os<<L"SOC "<<(int)b[173+delta]<<L"% | Charge MOS "<<(int)b[198+delta]<<L" | Discharge MOS "<<(int)b[199+delta]<<L" | Balance "<<(int)b[201+delta]<<L"\r\n";
 for(int i=0;i<(mode==1?24:32);i++){auto mv=u16(b,6+i*2);if(mv>=500&&mv<=5500)os<<L"Cell "<<i+1<<L": "<<mv/1000.0<<L" V\r\n";}
 return os.str();
}
void parseChunk(std::vector<uint8_t> const& bytes){
 {std::lock_guard lock(logMutex);rawCapture.insert(rawCapture.end(),bytes.begin(),bytes.end());}
 std::lock_guard lock(frameMutex);frameBuffer.insert(frameBuffer.end(),bytes.begin(),bytes.end());
 const uint8_t signature[]={0x55,0xAA,0xEB,0x90};
 while(true){
  auto start=std::search(frameBuffer.begin(),frameBuffer.end(),std::begin(signature),std::end(signature));
  if(start!=frameBuffer.begin())frameBuffer.erase(frameBuffer.begin(),start);
  if(frameBuffer.size()<300)break;
  auto valid=[&](size_t len){uint8_t checksum=0;for(size_t i=0;i<len-1;i++)checksum+=frameBuffer[i];return checksum==frameBuffer[len-1];};
  size_t len=300;
  if(!valid(300)){if(frameBuffer.size()<320)break;if(valid(320))len=320;else{frameBuffer.erase(frameBuffer.begin());continue;}}
  std::vector<uint8_t> frame(frameBuffer.begin(),frameBuffer.begin()+len);frameBuffer.erase(frameBuffer.begin(),frameBuffer.begin()+len);
  if(frame[4]==2){int mode=0;if(protocolBox)mode=(int)SendMessageW(protocolBox,CB_GETCURSEL,0,0);post(WM_VALUES,parse(frame,mode));}
  else if(frame[4]==1)log(L"Settings frame received: "+std::to_wstring(len)+L" bytes");
  else if(frame[4]==3)log(L"Device info received: "+std::to_wstring(len)+L" bytes");
 }
 if(frameBuffer.size()>1024)frameBuffer.clear();
}
void stopScan(){try{if(watcher&&watcher.Status()==BluetoothLEAdvertisementWatcherStatus::Started)watcher.Stop();}catch(...){}}
void scan(){
 try{
  stopScan();watcher=BluetoothLEAdvertisementWatcher();watcher.ScanningMode(BluetoothLEScanningMode::Active);
  watchToken=watcher.Received([](auto const&,BluetoothLEAdvertisementReceivedEventArgs const& e){
   auto name=std::wstring(e.Advertisement().LocalName().c_str());if(name.empty())return;
   auto address=e.BluetoothAddress();std::wostringstream os;os<<name<<L" ["<<std::hex<<std::uppercase<<std::setw(12)<<std::setfill(L'0')<<address<<L"]";
   auto label=os.str();{std::lock_guard lock(devicesMutex);discovered[label]=address;}post(WM_FOUND,label);
  });
  watcher.Start();log(L"Scanning BLE...");
 }catch(...){exception("scan");}
}
void disconnect(){
 try{active=false;stopScan();if(rx){rx.ValueChanged(notifyToken);rx=nullptr;}tx=nullptr;device=nullptr;{std::lock_guard lock(frameMutex);frameBuffer.clear();}log(L"Disconnected");}
 catch(...){exception("disconnect");}
}
void sendRead(uint8_t opcode){
 try{
  if(!tx)return;std::vector<uint8_t> frame(20,0);frame[0]=0xAA;frame[1]=0x55;frame[2]=0x90;frame[3]=0xEB;frame[4]=opcode;frame[16]=(uint8_t)seq.fetch_add(1);
  for(size_t i=0;i<19;i++)frame[19]+=frame[i];
  DataWriter writer;writer.WriteBytes(frame);
  auto properties=tx.CharacteristicProperties();
  auto opt=(properties&GattCharacteristicProperties::WriteWithoutResponse)==GattCharacteristicProperties::WriteWithoutResponse?GattWriteOption::WriteWithoutResponse:GattWriteOption::WriteWithResponse;
  auto result=tx.WriteValueAsync(writer.DetachBuffer(),opt).get();log(L"Read request "+std::to_wstring(opcode)+L", status "+std::to_wstring((int)result));
 }catch(...){exception("request");}
}
void connect(uint64_t address){
 try{
  init_apartment(apartment_type::multi_threaded);log(L"Connecting to JK BLE...");
  auto d=BluetoothLEDevice::FromBluetoothAddressAsync(address).get();if(!d){log(L"Unable to open device");return;}
  auto services=d.GetGattServicesForUuidAsync(serviceUuid,BluetoothCacheMode::Uncached).get();
  if(services.Status()!=GattCommunicationStatus::Success||services.Services().Size()==0){log(L"Service FFE0 not found");return;}
  GattCharacteristic receiver{nullptr},sender{nullptr};
  for(auto const& s:services.Services()){
   auto chars=s.GetCharacteristicsForUuidAsync(charUuid,BluetoothCacheMode::Uncached).get();
   if(chars.Status()!=GattCommunicationStatus::Success)continue;
   for(auto const& c:chars.Characteristics()){
    auto p=c.CharacteristicProperties();
    if((p&GattCharacteristicProperties::Notify)==GattCharacteristicProperties::Notify)receiver=c;
    if((p&GattCharacteristicProperties::WriteWithoutResponse)==GattCharacteristicProperties::WriteWithoutResponse||(p&GattCharacteristicProperties::Write)==GattCharacteristicProperties::Write)sender=c;
   }
  }
  if(!receiver||!sender){log(L"Read/write FFE1 characteristics unavailable");return;}
  auto token=receiver.ValueChanged([](auto const&,GattValueChangedEventArgs const& e){
   try{auto reader=DataReader::FromBuffer(e.CharacteristicValue());std::vector<uint8_t> bytes(reader.UnconsumedBufferLength());reader.ReadBytes(bytes);parseChunk(bytes);}
   catch(...){exception("notification");}
  });
  auto status=receiver.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
  if(status!=GattCommunicationStatus::Success){receiver.ValueChanged(token);log(L"Notifications unavailable");return;}
  device=d;rx=receiver;tx=sender;notifyToken=token;active=true;
  log(L"Connected, requesting configuration and device information");sendRead(0x96);sendRead(0x97);
 }catch(...){exception("connect");}
}
void save(){
 OPENFILENAMEW dialog{};wchar_t filename[MAX_PATH]=L"jk-bms-ble-raw.bin";dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=windowHandle;dialog.lpstrFile=filename;dialog.nMaxFile=MAX_PATH;dialog.lpstrFilter=L"Binary (*.bin)\0*.bin\0All (*.*)\0*.*\0";dialog.Flags=OFN_OVERWRITEPROMPT;dialog.lpstrDefExt=L"bin";
 if(GetSaveFileNameW(&dialog)){std::lock_guard lock(logMutex);std::ofstream out(std::filesystem::path(filename),std::ios::binary);out.write(reinterpret_cast<char const*>(rawCapture.data()),(std::streamsize)rawCapture.size());log(L"Saved raw capture");}
}
HWND control(wchar_t const* cls,wchar_t const* text,DWORD style,int x,int y,int w,int h,int id=0){return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,windowHandle,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);}
LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_CREATE:{
  windowHandle=h;
  control(L"BUTTON",L"Scan BLE",BS_PUSHBUTTON,12,12,100,32,SCAN);
  control(L"BUTTON",L"Connect",BS_PUSHBUTTON,120,12,100,32,CONNECT);
  control(L"BUTTON",L"Disconnect",BS_PUSHBUTTON,228,12,110,32,DISCONNECT);
  control(L"BUTTON",L"Save RAW",BS_PUSHBUTTON,346,12,100,32,SAVE);
  protocolBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,460,12,215,180,PROTOCOL);
  for(auto name:{L"JK02_32S",L"JK02_24S",L"JK04"})SendMessageW(protocolBox,CB_ADDSTRING,0,(LPARAM)name);
  SendMessageW(protocolBox,CB_SETCURSEL,0,0);
  devicesList=control(L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|LBS_NOTIFY,12,56,663,128,DEVICES);
  valuesBox=control(L"EDIT",L"Select a device and connect.\r\nRead-only experimental decoder.",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,12,194,663,283);
  logBox=control(L"EDIT",L"",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,12,486,663,150);
  control(L"STATIC",L"READ ONLY - no protection writes, MOSFET switching or reboot commands.",0,12,644,663,25);
  return 0;
 }
 case WM_COMMAND:switch(LOWORD(wp)){
  case SCAN:SendMessageW(devicesList,LB_RESETCONTENT,0,0);{std::lock_guard lock(devicesMutex);discovered.clear();}scan();break;
  case CONNECT:{
   int idx=(int)SendMessageW(devicesList,LB_GETCURSEL,0,0);if(idx==LB_ERR){log(L"Select device");break;}
   wchar_t name[512]{};SendMessageW(devicesList,LB_GETTEXT,idx,(LPARAM)name);uint64_t address{};
   {std::lock_guard lock(devicesMutex);auto it=discovered.find(name);if(it!=discovered.end())address=it->second;}
   if(address){stopScan();std::thread([address]{connect(address);}).detach();}break;
  }
  case DISCONNECT:disconnect();break;
  case SAVE:save();break;
 }return 0;
 case WM_FOUND:{std::unique_ptr<std::wstring> name(reinterpret_cast<std::wstring*>(lp));if(SendMessageW(devicesList,LB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)name->c_str())==LB_ERR)SendMessageW(devicesList,LB_ADDSTRING,0,(LPARAM)name->c_str());return 0;}
 case WM_LOG:{std::unique_ptr<std::wstring> line(reinterpret_cast<std::wstring*>(lp));int end=GetWindowTextLengthW(logBox);SendMessageW(logBox,EM_SETSEL,end,end);auto entry=*line+L"\r\n";SendMessageW(logBox,EM_REPLACESEL,FALSE,(LPARAM)entry.c_str());return 0;}
 case WM_VALUES:{std::unique_ptr<std::wstring> value(reinterpret_cast<std::wstring*>(lp));SetWindowTextW(valuesBox,value->c_str());return 0;}
 case WM_DESTROY:disconnect();windowHandle=nullptr;PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
 try{
  init_apartment(apartment_type::multi_threaded);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
  WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"JK_BMS_PC";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassExW(&cls);
  HWND hwnd=CreateWindowExW(0,cls.lpszClassName,L"JK BMS PC Studio - C++/WinRT BLE Preview",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,710,725,nullptr,nullptr,instance,nullptr);
  ShowWindow(hwnd,show);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return (int)message.wParam;
 }catch(...){MessageBoxW(nullptr,L"Windows Bluetooth initialization failed",L"JK BMS",MB_ICONERROR);return 1;}
}