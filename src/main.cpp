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
#include "jk_settings.h"
#include <optional>
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
struct Command {std::wstring action;uint64_t address=0;std::wstring payload;};
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
std::mutex settingsMutex;
std::vector<uint8_t> latestSettings;
ULONGLONG lastSettingsMs=0;
std::wstring activeModel,activeHardware,activeSoftware,activeSerial,activeMac;
std::atomic<jk::Protocol> observedProtocol{jk::Protocol::Auto};
std::atomic<int> observedCells{0};
struct PendingWrite{
 std::string key;double value=0;
 uint32_t raw=0; jk::Protocol protocol=jk::Protocol::Auto;
 ULONGLONG deadline=0,nextPoll=0,started=0;int confirmations=0;
};
std::optional<PendingWrite> pendingWrite;
std::atomic<bool> expectingRestart{false};
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
std::wstring fromUtf8(std::string const& str){
 if(str.empty())return {};
 auto n=MultiByteToWideChar(CP_UTF8,0,str.data(),static_cast<int>(str.size()),nullptr,0);
 if(n<=0)return {};
 std::wstring out(static_cast<size_t>(n),L' ');
 MultiByteToWideChar(CP_UTF8,0,str.data(),static_cast<int>(str.size()),out.data(),n);
 return out;
}
std::string narrowAscii(std::wstring const& text){
 std::string out;for(wchar_t c:text){if(c<32||c>126)return {};out.push_back(static_cast<char>(c));}return out;
}
jk::Protocol effectiveProtocol(){
 auto observed=observedProtocol.load(),requested=protocol.load();
 if(requested==jk::Protocol::Auto)return observed;
 if(observed!=jk::Protocol::Auto && observed!=requested)return jk::Protocol::Auto;
 return requested;
}
void settingStatus(std::string const& id,std::wstring state,std::wstring detail){
 emit(L"{\"type\":\"settingWrite\",\"key\":\""+fromUtf8(id)+L"\",\"state\":\""+state+L"\",\"message\":\""+jsonEscape(detail)+L"\"}");
}
std::wstring parseAscii(std::vector<uint8_t> const& b,size_t offset,size_t len){
 std::wstring out;
 for(size_t i=offset;i<offset+len&&i<b.size();i++){
  uint8_t c=b[i];if(c==0)break;
  if(c>=32 && c<127)out.push_back(static_cast<wchar_t>(c));
 }
 return out;
}
void emitSettings(){
 std::vector<uint8_t> frame;std::wstring model,software,serial,mac;
 {
  std::lock_guard lock(settingsMutex);
  frame=latestSettings;model=activeModel;software=activeSoftware;serial=activeSerial;mac=activeMac;
 }
 auto p=effectiveProtocol();
 if(frame.size()!=300||p==jk::Protocol::Auto)return;
 std::wostringstream stream;
 stream<<L"{\"type\":\"settings\",\"protocol\":\""<<jk::name(p)
       <<L"\",\"model\":\""<<jsonEscape(model)
       <<L"\",\"firmware\":\""<<jsonEscape(software)
       <<L"\",\"serial\":\""<<jsonEscape(serial)
       <<L"\",\"mac\":\""<<jsonEscape(mac)
       <<L"\",\"ready\":"<<((observedProtocol.load()==p && !model.empty())?L"true":L"false")
       <<L",\"fields\":[";
 bool first=true;
 for(auto const& descriptor:jk::settings()){
  auto value=jk::readSetting(frame,descriptor,p);
  if(!value||!std::isfinite(*value))continue;
  if(!first)stream<<L",";
  first=false;
  stream<<L"{\"id\":\""<<fromUtf8(descriptor.id)
        <<L"\",\"group\":\""<<jsonEscape(fromUtf8(descriptor.group))
        <<L"\",\"label\":\""<<jsonEscape(fromUtf8(descriptor.label))
        <<L"\",\"unit\":\""<<jsonEscape(fromUtf8(descriptor.unit))
        <<L"\",\"value\":"<<std::fixed<<std::setprecision(5)<<*value
        <<L",\"min\":"<<descriptor.min<<L",\"max\":"<<descriptor.max
        <<L",\"precision\":"<<static_cast<int>(descriptor.precision)
        <<L",\"writable\":"<<(jk::canWrite(descriptor,p)?L"true":L"false")
        <<L",\"register\":"<<static_cast<unsigned>(jk::reg(descriptor,p))<<L"}";
 }
 stream<<L"]}";
 emit(stream.str());
}
std::wstring winrtError(){try{throw;}catch(winrt::hresult_error const& e){return e.message().c_str();}catch(std::exception const& e){std::wstring w;std::string s=e.what();w.assign(s.begin(),s.end());return w;}catch(...){return L"Unexpected exception";}}
void enqueue(std::wstring action,uint64_t address=0,std::wstring payload=L""){{std::lock_guard lock(queueMutex);commands.push_back({std::move(action),address,std::move(payload)});}queueReady.notify_one();}
void stopScanner(){
 if(watcher){try{watcher.Stop();}catch(...){}try{watcher.Received(watchToken);}catch(...){}watcher=nullptr;}
}
void disconnectBms(){
 bool wasConnected=(bool)bms;
 {
  std::lock_guard lock(settingsMutex);
  if(pendingWrite)settingStatus(pendingWrite->key,L"failed",L"Ngắt BLE trước khi xác minh ghi.");
  pendingWrite.reset();latestSettings.clear();lastSettingsMs=0;
  activeModel.clear();activeHardware.clear();activeSoftware.clear();activeSerial.clear();activeMac.clear();
 }
 observedProtocol=jk::Protocol::Auto;observedCells=0;
 emit(L"{\"type\":\"settingsClear\"}");
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
 if(type==3){
  deviceInfoReceived=true;
  {
   std::lock_guard lock(settingsMutex);
   activeModel=parseAscii(b,6,16);
   activeHardware=parseAscii(b,22,8);
   activeSoftware=parseAscii(b,30,8);
   activeSerial=parseAscii(b,86,11);
  }
  emit(L"{\"type\":\"deviceInfo\",\"deviceName\":\""+jsonEscape(parseAscii(b,46,16))+L"\",\"serial\":\""+jsonEscape(parseAscii(b,86,11))+L"\",\"model\":\""+jsonEscape(parseAscii(b,6,16))+
       L"\",\"hardware\":\""+jsonEscape(parseAscii(b,22,8))+
       L"\",\"firmware\":\""+jsonEscape(parseAscii(b,30,8))+L"\"}");
  emitSettings();
  enqueue(L"read_status");return;
 }
 if(type==1){
  bool verified=false;std::string id;std::wstring message;
  {
   std::lock_guard lock(settingsMutex);
   latestSettings=b;
   lastSettingsMs=GetTickCount64();
   if(pendingWrite) {
    auto const* spec=jk::findSetting(pendingWrite->key);
    if(spec && pendingWrite->started && lastSettingsMs>=pendingWrite->started){
     auto actual=jk::readSetting(b,*spec,pendingWrite->protocol);
     auto expected=jk::encodeValue(*spec,pendingWrite->protocol,pendingWrite->value);
     auto rawActual=actual?jk::encodeValue(*spec,pendingWrite->protocol,*actual):std::optional<uint32_t>{};
     // A matching value must be seen in two subsequent Settings responses;
     // a successful BLE write alone is never an ACK for persistence.
     if(rawActual&&expected&&*rawActual==*expected) {
      pendingWrite->confirmations++;
      if(pendingWrite->confirmations>=2){
       id=pendingWrite->key;verified=true;pendingWrite.reset();
      }else id=pendingWrite->key;
     }else{
      pendingWrite->confirmations=0;
      id=pendingWrite->key;message=L"Readback chưa trùng, đang thử lại.";
     }
    }
   }
  }
  if(verified)settingStatus(id,L"verified",L"Đã đối chiếu hai lần đọc lại từ BMS.");
  else if(!id.empty())settingStatus(id,L"pending",message.empty()?L"Đã đối chiếu lần một, đang kiểm tra lại.":message);
  emitSettings();
  return;
 }
 if(type!=2)return;
 lastTelemetryMs=GetTickCount64();
 auto decoded=jk::decode(b,protocol.load());
 if(!decoded){log(L"RX 0x02 hợp lệ CRC, chưa giải mã được model đang chọn.");return;}
 auto& t=*decoded;
 observedCells=static_cast<int>(t.cells.size());
 if(observedProtocol!=t.protocol){observedProtocol=t.protocol;emitSettings();}
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
 auto addTemperature=[&](wchar_t const* name,std::optional<double> const& v){
  json<<L",\""<<name<<L"\":";
  if(v&&std::isfinite(*v))json<<std::fixed<<std::setprecision(1)<<*v;
  else json<<L"null";
 };
 addTemperature(L"temperature1",t.temperature1);
 addTemperature(L"temperature2",t.temperature2);
 addTemperature(L"mosTemperature",t.mosTemperature);
 addTemperature(L"temperature3",t.temperature3);
 addTemperature(L"temperature4",t.temperature4);
 addTemperature(L"temperature5",t.temperature5);
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
 disconnectBms();
 {std::lock_guard lock(settingsMutex);activeMac=hex(address);}
 status=L"connecting";field(L"status",status);
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
bool sendPayload(std::array<uint8_t,20> const& frame) {
 if(!writeChar)return false;
 DataWriter writer;writer.WriteBytes(frame);
 auto props=static_cast<uint32_t>(writeChar.CharacteristicProperties());
 auto option=(props&static_cast<uint32_t>(GattCharacteristicProperties::WriteWithoutResponse))?
  GattWriteOption::WriteWithoutResponse:GattWriteOption::WriteWithResponse;
 return writeChar.WriteValueAsync(writer.DetachBuffer(),option).get()==GattCommunicationStatus::Success;
}
void changeSetting(std::wstring const& payload){
 auto split=payload.find(L':');if(split==std::wstring::npos){log(L"Invalid setting command");return;}
 auto key=narrowAscii(payload.substr(0,split));
 std::wstring number=payload.substr(split+1);
 wchar_t* tail=nullptr;double value=wcstod(number.c_str(),&tail);
 auto const* spec=jk::findSetting(key);
 if(!spec||!tail||tail==number.c_str()||*tail!=0||!std::isfinite(value)){
  settingStatus(key,L"failed",L"Giá trị hoặc khóa tham số không hợp lệ.");return;
 }
 auto p=effectiveProtocol();
 std::vector<uint8_t> baseline;ULONGLONG age=0;std::wstring model;
 {
  std::lock_guard lock(settingsMutex);
  if(pendingWrite){settingStatus(key,L"failed",L"Đang xác minh một thay đổi khác.");return;}
  baseline=latestSettings;age=lastSettingsMs;model=activeModel;
 }
 if(!writeChar||p==jk::Protocol::Auto||observedProtocol.load()!=p||model.empty()||
    baseline.size()!=300||!age||GetTickCount64()-age>30000){
  settingStatus(key,L"failed",L"Cần kết nối, nhận dạng firmware và đọc Settings mới trong 30 giây trước khi ghi.");return;
 }
 if(key=="cell_count" && (observedCells.load()<2||
    (p==jk::Protocol::JK02_24S&&value>24)||
    static_cast<int>(value)!=observedCells.load())){
  settingStatus(key,L"failed",L"Số cell yêu cầu phải khớp số cell đo được từ telemetry; không thể khai báo sai cấu hình phần cứng.");return;
 }
 if(!jk::canWrite(*spec,p)||!jk::validSetting(*spec,value)||!jk::crossCheck(key,value,baseline,p)){
  settingStatus(key,L"failed",L"Model không hỗ trợ hoặc giá trị vượt giới hạn / xung đột bảo vệ.");return;
 }
 auto initial=jk::readSetting(baseline,*spec,p);
 auto encoded=jk::encodeValue(*spec,p,value);
 if(!initial||!encoded){
  settingStatus(key,L"failed",L"Không xác thực được dữ liệu hiện tại hoặc mã hóa giá trị.");return;
 }
 auto oldRaw=jk::encodeValue(*spec,p,*initial);
 if(oldRaw&&*oldRaw==*encoded){settingStatus(key,L"verified",L"Giá trị đã trùng với thiết bị, không cần ghi.");return;}
 auto frame=jk::settingWriteFrame(*spec,p,value);
 if(frame[0]!=0xAA){settingStatus(key,L"failed",L"Không tạo được khung ghi.");return;}
 {
  std::lock_guard lock(settingsMutex);
  pendingWrite=PendingWrite{key,value,*encoded,p,GetTickCount64()+12000,GetTickCount64()+400,GetTickCount64(),0};
 }
 settingStatus(key,L"pending",L"Đã gửi yêu cầu ghi; đang chờ BMS đọc lại hai lần.");
 if(!sendPayload(frame)){
  std::lock_guard lock(settingsMutex);pendingWrite.reset();
  settingStatus(key,L"failed",L"Windows BLE từ chối ghi khung.");return;
 }
 log(L"TX SET 0x"+hex(frame[4]).substr(10)+L" target "+fromUtf8(key)+L" (awaiting fresh 0x01 readback)");
}
void restartBms(){
 if(!writeChar||effectiveProtocol()==jk::Protocol::Auto||observedProtocol.load()!=effectiveProtocol()){
  settingStatus("restart",L"failed",L"Chưa nhận dạng BMS; không gửi restart.");return;
 }
 {
  std::lock_guard lock(settingsMutex);
  if(activeModel.empty()||latestSettings.size()!=300||!lastSettingsMs||
     GetTickCount64()-lastSettingsMs>30000||pendingWrite){
   settingStatus("restart",L"failed",L"Chưa có Settings mới hoặc đang ghi cấu hình.");return;
  }
 }
 if(!sendPayload(jk::request(0x66))){settingStatus("restart",L"failed",L"BLE từ chối gửi lệnh reboot.");return;}
 settingStatus("restart",L"sent",L"Đã gửi 0x66; BMS có thể ngắt kết nối. Chưa thể xác minh khởi động lại qua BLE.");
 log(L"TX REBOOT 0x66 (explicit user command)");
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
     else if(cmd.action==L"refresh_settings"){if(writeChar)sendRead(0x96);}
     else if(cmd.action==L"change_setting")changeSetting(cmd.payload);
     else if(cmd.action==L"restart")restartBms();
     else if(cmd.action==L"read_status" && writeChar){
      sendRead(0x96);lastStatusRequestMs=GetTickCount64();
     }
    }
    {
     std::string failed;
     bool doRead=false;
     {
      std::lock_guard lock(settingsMutex);
      if(pendingWrite){
       if(GetTickCount64()>pendingWrite->deadline){
        failed=pendingWrite->key;pendingWrite.reset();
       }else if(GetTickCount64()>=pendingWrite->nextPoll){
        doRead=true;pendingWrite->nextPoll=GetTickCount64()+1600;
       }
      }
     }
     if(!failed.empty())settingStatus(failed,L"failed",L"Hết thời gian xác minh: BMS chưa trả về giá trị mong đợi.");
     if(doRead){sendRead(0x96);lastStatusRequestMs=GetTickCount64();}
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
       else if(cmd==L"settings:read")enqueue(L"refresh_settings");
       else if(cmd==L"bms:restart")enqueue(L"restart");
       else if(cmd.rfind(L"settings:write:",0)==0)enqueue(L"change_setting",0,cmd.substr(15));
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