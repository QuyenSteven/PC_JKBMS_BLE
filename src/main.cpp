#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <wrl.h>
#include <WebView2.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <stdexcept>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "jk_protocol.h"
#pragma comment(lib,"WindowsApp.lib")
#pragma comment(lib,"Ole32.lib")
#pragma comment(lib,"Shell32.lib")
using namespace winrt;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Storage::Streams;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
constexpr UINT WM_EVENT=WM_APP+42;
struct UiEvent {std::wstring json;};
HWND hwnd{};
ComPtr<ICoreWebView2Controller> controller;
ComPtr<ICoreWebView2> browser;
std::atomic<bool> running{true};
std::mutex queueMutex,deviceMutex,rawMutex;
std::condition_variable queueReady;
struct Command {std::wstring action;uint64_t address=0;};
std::deque<Command> commands;
std::map<uint64_t,std::wstring> addresses;
std::vector<uint8_t> rawBytes;
std::mutex frameMutex;
jk::FrameAssembler frames;
std::atomic<ULONGLONG> lastTelemetryMs{0};
std::atomic<bool> deviceInfoReceived{false};
std::atomic<uint64_t> receivedChunks{0};
std::atomic<uint64_t> receivedFrames{0};
std::atomic<jk::Protocol> protocol{jk::Protocol::Auto};
ULONGLONG lastStatusRequestMs=0,connectedAtMs=0;
BluetoothLEAdvertisementWatcher watcher{nullptr};
BluetoothLEDevice bms{nullptr};
GattCharacteristic notifyChar{nullptr},writeChar{nullptr};
winrt::event_token notifyToken{},watchToken{};
std::wstring status=L"disconnected";
std::wstring jsonEscape(std::wstring const& input){
 std::wstring out;out.reserve(input.size()+16);
 for(auto ch:input){switch(ch){
 case L'\\':out+=L"\\\\";break;case L'"':out+=L"\\\"";break;case L'\n':out+=L"\\n";break;
 case L'\r':out+=L"\\r";break;case L'\t':out+=L"\\t";break;
 default:if(ch>=32)out+=ch;break;
 }}return out;
}
void emit(std::wstring json){auto ptr=new UiEvent{std::move(json)};if(!hwnd||!PostMessageW(hwnd,WM_EVENT,0,(LPARAM)ptr))delete ptr;}
void field(std::wstring const& type,std::wstring const& value){emit(L"{\"type\":\""+type+L"\",\"message\":\""+jsonEscape(value)+L"\"}");}
void log(std::wstring text){field(L"log",text);}
std::wstring hex(uint64_t v){std::wstringstream s;s<<std::hex<<std::uppercase<<std::setw(12)<<std::setfill(L'0')<<v;return s.str();}
std::wstring winrtError(){try{throw;}catch(winrt::hresult_error const& e){return e.message().c_str();}catch(std::exception const& e){std::wstring w;std::string s=e.what();w.assign(s.begin(),s.end());return w;}catch(...){return L"Unexpected exception";}}
void enqueue(std::wstring action,uint64_t address=0){{std::lock_guard lock(queueMutex);commands.push_back({std::move(action),address});}queueReady.notify_one();}
void stopScanner(){
 if(watcher){try{watcher.Stop();}catch(...){}try{watcher.Received(watchToken);}catch(...){}watcher=nullptr;}
}
void disconnectBms(){
 bool wasConnected=(bool)bms;
 stopScanner();
 {std::lock_guard lock(frameMutex);frames.reset();}
 lastTelemetryMs=0;deviceInfoReceived=false;lastStatusRequestMs=0;connectedAtMs=0;receivedChunks=0;receivedFrames=0;
 if(notifyChar){try{notifyChar.ValueChanged(notifyToken);}catch(...){}}
 notifyChar=nullptr;writeChar=nullptr;bms=nullptr;
 status=L"disconnected";field(L"status",status);if(wasConnected)log(L"Disconnected");
}
void scanBms(){
 stopScanner();{
  std::lock_guard lock(deviceMutex);addresses.clear();
 }
 emit(L"{\"type\":\"clearDevices\"}");
 watcher=BluetoothLEAdvertisementWatcher();
 watcher.ScanningMode(BluetoothLEScanningMode::Active);
 watchToken=watcher.Received([](auto const&,BluetoothLEAdvertisementReceivedEventArgs const& args){
  try{
   std::wstring name(args.Advertisement().LocalName().c_str());if(name.empty())return;
   uint64_t address=args.BluetoothAddress();
   {std::lock_guard lock(deviceMutex);addresses[address]=name;}
   emit(L"{\"type\":\"device\",\"address\":\""+std::to_wstring(address)+L"\",\"name\":\""+jsonEscape(name)+L"\",\"mac\":\""+hex(address)+L"\"}");
  }catch(...){}
 });
 watcher.Start();log(L"Scanning BLE advertisements...");
}
const winrt::guid service{0x0000FFE0,0,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB}};
const winrt::guid characteristic{0x0000FFE1,0,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB}};
void parseFrame(std::vector<uint8_t> const& b){
 uint8_t type=b[4];
 receivedFrames++;
 field(L"frame",L"0x0"+std::to_wstring(type)+L" ("+std::to_wstring(b.size())+L" bytes)");
 if(type==3) {deviceInfoReceived=true;enqueue(L"read_status");return;}
 if(type!=2)return;
 lastTelemetryMs=GetTickCount64();
 auto decoded=jk::decode(b,protocol.load());
 if(!decoded){log(L"RX 0x02 valid CRC, but selected protocol could not decode plausible measurements. Try AUTO / JK04 / JK02_24S.");return;}
 auto& t=*decoded;
 std::wstringstream json;
 json<<L"{\"type\":\"telemetry\",\"protocol\":\"";
 json<<jk::name(t.protocol)<<L"\",\"cells\":[";
 for(size_t i=0;i<t.cells.size();i++){if(i)json<<L",";json<<std::fixed<<std::setprecision(3)<<t.cells[i];}
 json<<L"],\"count\":"<<t.cells.size();
 if(t.packVerified) {
  json<<L",\"voltage\":"<<std::fixed<<std::setprecision(3)<<t.voltage
      <<L",\"current\":"<<t.current<<L",\"power\":"<<t.power
      <<L",\"soc\":"<<t.soc<<L",\"charge\":"<<t.charge
      <<L",\"discharge\":"<<t.discharge<<L",\"balance\":"<<t.balance;
 }
 json<<L",\"packVerified\":"<<(t.packVerified?L"true":L"false")<<L"}";
 emit(json.str());
}
void feed(std::vector<uint8_t> const& chunk){
 receivedChunks++;
 {std::lock_guard lock(rawMutex);rawBytes.insert(rawBytes.end(),chunk.begin(),chunk.end());}
 std::lock_guard lock(frameMutex);
 uint64_t badBefore=frames.checksumErrors;
 frames.add(chunk,[](std::vector<uint8_t> const& frame){parseFrame(frame);});
 if(frames.checksumErrors!=badBefore)
  log(L"BLE CRC failure: count="+std::to_wstring(frames.checksumErrors)+L", valid="+std::to_wstring(frames.valid));
}
void sendRead(uint8_t command){
 if(!writeChar)return;
 auto bytes=jk::request(command);
 DataWriter writer;writer.WriteBytes(bytes);
 auto props=static_cast<uint32_t>(writeChar.CharacteristicProperties());
 auto option=(props&static_cast<uint32_t>(GattCharacteristicProperties::WriteWithoutResponse))?
  GattWriteOption::WriteWithoutResponse:GattWriteOption::WriteWithResponse;
 auto result=writeChar.WriteValueAsync(writer.DetachBuffer(),option).get();
 wchar_t cmd[12]{};swprintf_s(cmd,L"%02X",(unsigned)command);
 log(L"TX 0x"+std::wstring(cmd)+L" result "+std::to_wstring(static_cast<int>(result)));
}
void connectBms(uint64_t address){
 disconnectBms();status=L"connecting";field(L"status",status);
 log(L"Connecting to "+hex(address));
 auto target=BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
 if(!target)throw std::runtime_error("BluetoothLEDevice not found");
 auto services=target.GetGattServicesForUuidAsync(service,BluetoothCacheMode::Uncached).get();
 if(services.Status()!=GattCommunicationStatus::Success||services.Services().Size()==0)throw std::runtime_error("BLE service FFE0 not found");
 GattCharacteristic recv{nullptr},send{nullptr};
 for(auto const& svc:services.Services()){
  auto chars=svc.GetCharacteristicsForUuidAsync(characteristic,BluetoothCacheMode::Uncached).get();
  if(chars.Status()!=GattCommunicationStatus::Success)continue;
  for(auto const& c:chars.Characteristics()){
   auto flags=static_cast<uint32_t>(c.CharacteristicProperties());
   if(flags&static_cast<uint32_t>(GattCharacteristicProperties::Notify))recv=c;
   if(flags&(static_cast<uint32_t>(GattCharacteristicProperties::Write)|static_cast<uint32_t>(GattCharacteristicProperties::WriteWithoutResponse)))send=c;
  }
 }
 if(!recv||!send)throw std::runtime_error("BLE FFE1 GATT notification/write channels missing");
 // Device, write and callback references all stay alive on the BLE worker.
 bms=target;notifyChar=recv;writeChar=send;
 notifyToken=notifyChar.ValueChanged([](auto const&,GattValueChangedEventArgs const& args){
  try{
   auto buffer=args.CharacteristicValue();
   DataReader reader=DataReader::FromBuffer(buffer);
   std::vector<uint8_t> chunk(reader.UnconsumedBufferLength());
   reader.ReadBytes(chunk);feed(chunk);
  }catch(...){log(L"BLE RX exception: "+winrtError());}
 });
 auto result=notifyChar.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
 if(result!=GattCommunicationStatus::Success)throw std::runtime_error("Failed to enable BLE notifications");
 status=L"connected";field(L"status",status);log(L"BLE notifications enabled");
 connectedAtMs=GetTickCount64();
 sendRead(0x97); // Request device info first; response 0x03 triggers status poll 0x96
 log(L"Device info requested; status 0x96 will follow after device response.");
}
void saveRaw(){
 wchar_t path[MAX_PATH]{};
 PWSTR local=nullptr;
 if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&local))){
  std::wstring p(local);CoTaskMemFree(local);p+=L"\\jk-bms-capture.bin";wcsncpy_s(path,MAX_PATH,p.c_str(),_TRUNCATE);
 }else wcscpy_s(path,MAX_PATH,L"jk-bms-capture.bin");
 std::lock_guard lock(rawMutex);
 std::ofstream file(fs::path(path),std::ios::binary);
 if(!file){log(L"Cannot open capture output");return;}
 file.write(reinterpret_cast<char const*>(rawBytes.data()),static_cast<std::streamsize>(rawBytes.size()));
 log(L"RAW saved: "+std::wstring(path));
}
void bleWorker(){
 try{
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  while(running){
   Command cmd;bool hasCommand=false;
   {
    std::unique_lock lock(queueMutex);
    queueReady.wait_for(lock,std::chrono::milliseconds(1000),[]{return !running||!commands.empty();});
    if(!running)break;
    if(!commands.empty()){cmd=commands.front();commands.pop_front();hasCommand=true;}
   }
   try{
    if(hasCommand) {
     if(cmd.action==L"scan")scanBms();
     else if(cmd.action==L"connect")connectBms(cmd.address);
     else if(cmd.action==L"disconnect")disconnectBms();
     else if(cmd.action==L"save")saveRaw();
     else if(cmd.action==L"read_status" && writeChar){
      sendRead(0x96);lastStatusRequestMs=GetTickCount64();
     }
    }
    // JK reference integration polls 0x96 periodically until frame 0x02 is received.
    // Once streaming, retry only when status becomes stale. No configuration writes.
    ULONGLONG now=GetTickCount64();
    if(writeChar && connectedAtMs && now-connectedAtMs>=1000 &&
       now-lastStatusRequestMs>=5000 &&
       (lastTelemetryMs==0||now-lastTelemetryMs.load()>=10000)){
      log(L"No recent 0x02 telemetry: sending read 0x96 (RX chunks="+
         std::to_wstring(receivedChunks.load())+L", frames="+
         std::to_wstring(receivedFrames.load())+L")");
      sendRead(0x96);lastStatusRequestMs=now;
    }
   }catch(...){log(L"BLE: "+winrtError());if(hasCommand&&cmd.action==L"connect")disconnectBms();}
  }
  disconnectBms();
 }catch(...){log(L"BLE worker failed: "+winrtError());}
}
std::thread bleThread;
void resize(){
 if(!controller||!hwnd)return;RECT rect{};GetClientRect(hwnd,&rect);controller->put_Bounds(rect);
}
void initBrowser(){
 fs::path executable;
 wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);
 executable=fs::path(exe).parent_path();
 fs::path page=executable/L"web"/L"index.html";
 if(!fs::exists(page))page=executable.parent_path().parent_path()/L"web"/L"index.html";
 if(!fs::exists(page)){MessageBoxW(hwnd,L"web/index.html not found next to executable. Copy the web directory next to the exe.",L"JK BMS",MB_ICONERROR);return;}
 auto htmlPath=page.wstring();
 auto cb=Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
  [htmlPath](HRESULT hr,ICoreWebView2Environment* env)->HRESULT{
   if(FAILED(hr)||!env){MessageBoxW(hwnd,L"WebView2 Runtime unavailable.",L"JK BMS",MB_ICONERROR);return FAILED(hr)?hr:E_FAIL;}
   return env->CreateCoreWebView2Controller(hwnd,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
    [htmlPath](HRESULT result,ICoreWebView2Controller* created)->HRESULT{
     if(FAILED(result)||!created)return result;
     controller=created;controller->get_CoreWebView2(&browser);resize();
     EventRegistrationToken webMessageToken{};
     browser->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
      [](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* event)->HRESULT{
       LPWSTR str=nullptr;if(FAILED(event->TryGetWebMessageAsString(&str)))return S_OK;
       std::wstring cmd(str);CoTaskMemFree(str);
       if(cmd==L"scan"||cmd==L"disconnect"||cmd==L"save")enqueue(cmd);
       else if(cmd.rfind(L"protocol:",0)==0){
        auto p=cmd.substr(9);
        if(p==L"JK02_24S")protocol=jk::Protocol::JK02_24S;
        else if(p==L"JK02_32S")protocol=jk::Protocol::JK02_32S;
        else if(p==L"JK04")protocol=jk::Protocol::JK04;
        else protocol=jk::Protocol::Auto;
        log(L"BLE decoder selected: "+p);
       }
       else if(cmd.rfind(L"connect:",0)==0){
        try{enqueue(L"connect",std::stoull(cmd.substr(8)));}catch(...){log(L"Invalid BLE address");}
       }
       return S_OK;
      }).Get(),&webMessageToken);
     std::wstring uri=htmlPath;std::replace(uri.begin(),uri.end(),L'\\',L'/');
     browser->Navigate((L"file:///"+uri).c_str());
     return S_OK;
    }).Get());
  });
 HRESULT result=CreateCoreWebView2EnvironmentWithOptions(nullptr,nullptr,nullptr,cb.Get());
 if(FAILED(result))MessageBoxW(hwnd,L"Failed to initialize WebView2 Runtime.",L"JK BMS",MB_ICONERROR);
}
LRESULT CALLBACK wndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_CREATE:hwnd=w;initBrowser();return 0;
 case WM_SIZE:resize();return 0;
 case WM_EVENT:{
  std::unique_ptr<UiEvent> ev(reinterpret_cast<UiEvent*>(lp));
  if(browser)browser->PostWebMessageAsJson(ev->json.c_str());
  return 0;
 }
 case WM_DESTROY:
  running=false;queueReady.notify_all();
  // Join before destroying global BLE/WinRT state to prevent use-after-free.
  if(bleThread.joinable())bleThread.join();
  browser.Reset();controller.Reset();hwnd=nullptr;PostQuitMessage(0);return 0;
 }return DefWindowProcW(w,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 WNDCLASSW cls{};cls.lpfnWndProc=wndProc;cls.hInstance=instance;cls.lpszClassName=L"JKBMS_WebView_Host";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
 bleThread=std::thread(bleWorker);
 HWND w=CreateWindowExW(0,cls.lpszClassName,L"JK BMS Studio",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1380,900,nullptr,nullptr,instance,nullptr);
 ShowWindow(w,show);
 MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
 CoUninitialize();return (int)msg.wParam;
}