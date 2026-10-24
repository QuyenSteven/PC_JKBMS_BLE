#pragma once
// JK BLE wire protocol. Pure C++20, no Windows or UI dependencies.
// Responses: 300-byte frames, prefix 55 AA EB 90, CRC8 sum at byte 299.
// Some revisions append 20 bytes after the CRC; these are NOT a second frame.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace jk {
enum class Protocol { Auto, JK02_24S, JK02_32S, JK04 };
inline const char* name(Protocol p) {
 switch(p) {case Protocol::Auto:return "AUTO";case Protocol::JK02_24S:return "JK02_24S";
 case Protocol::JK02_32S:return "JK02_32S";case Protocol::JK04:return "JK04";}return "UNKNOWN";
}
inline std::array<uint8_t,20> request(uint8_t opcode) {
 std::array<uint8_t,20> frame{};
 frame[0]=0xAA;frame[1]=0x55;frame[2]=0x90;frame[3]=0xEB;frame[4]=opcode;
 for(size_t i=0;i<19;i++)frame[19]=uint8_t(frame[19]+frame[i]);
 return frame; // Counter/reserved zero for read requests; matches JK reference driver
}
inline uint16_t u16(std::vector<uint8_t> const& f,size_t n) {
 return uint16_t(f[n])|uint16_t(f[n+1])<<8;
}
inline uint32_t u32(std::vector<uint8_t> const& f,size_t n) {
 return uint32_t(f[n])|(uint32_t(f[n+1])<<8)|(uint32_t(f[n+2])<<16)|(uint32_t(f[n+3])<<24);
}
inline float f32(std::vector<uint8_t> const& f,size_t n) {
 uint32_t value=u32(f,n);float out=0.0f;std::memcpy(&out,&value,4);return out;
}
struct Telemetry {
 Protocol protocol=Protocol::Auto;
 std::vector<double> cells;
 double voltage=0, current=0, power=0;
 int soc=-1, charge=-1, discharge=-1, balance=-1;
 std::optional<double> temperature1,temperature2,mosTemperature,temperature3,temperature4,temperature5;
 uint32_t alarms=0;
 bool packVerified=false; // JK04 pack voltage/SOC not decoded yet.
};
inline std::optional<Telemetry> decodeJk02(std::vector<uint8_t> const& f,Protocol p) {
 if(f.size()<300||f[4]!=2||(p!=Protocol::JK02_24S&&p!=Protocol::JK02_32S))return {};
 size_t off=p==Protocol::JK02_32S?32:0;
 size_t count=p==Protocol::JK02_32S?32:24;
 uint32_t mask=u32(f,54+off/2);
 Telemetry t;t.protocol=p;
 double sum=0.0;
 for(size_t i=0;i<count;i++) {
  double v=u16(f,6+2*i)*0.001;
  if(mask && !(mask&(uint32_t(1)<<i)))continue;
  if(v<1.0||v>5.2)continue;
  t.cells.push_back(v);sum+=v;
 }
 if(t.cells.size()<2)return {};
 t.voltage=u32(f,118+off)*0.001;
 t.current=static_cast<int32_t>(u32(f,126+off))*0.001;
 t.power=t.voltage*t.current;
 t.soc=f[141+off];t.charge=f[166+off];t.discharge=f[167+off];
 t.balance=f[140+off]!=0?1:0;
 auto temperature=[&](size_t location)->std::optional<double>{
  if(location+1>=f.size())return {};
  double value=static_cast<int16_t>(u16(f,location))*0.1;
  if(!std::isfinite(value)||value < -55.0||value > 145.0)return {};
  return value;
 };
 // JK02 24S and 32S share T1 and T2 at 130/132 + cell-layout offset.
 // MOS sensor differs: 24S at 134; 32S at 112 + 2*offset (=176).
 t.temperature1=temperature(130+off);
 t.temperature2=temperature(132+off);
 t.mosTemperature=temperature(p==Protocol::JK02_32S?112+2*off:134);
 if(p==Protocol::JK02_32S){
  t.temperature3=temperature(226+off);
  t.temperature4=temperature(224+off);
  t.temperature5=temperature(222+off);
 }
 t.alarms=p==Protocol::JK02_32S?u32(f,134+off):u16(f,136+off);
 if(t.voltage<5.0||t.voltage>200.0||std::abs(t.current)>3000||t.soc>100)return {};
 if(std::abs(t.voltage-sum)>std::max(1.2,sum*0.08))return {};
 t.packVerified=true;
 return t;
}
inline std::optional<Telemetry> decodeJk04(std::vector<uint8_t> const& f) {
 if(f.size()<300||f[4]!=2)return {};
 Telemetry t;t.protocol=Protocol::JK04;
 for(size_t i=0;i<24;i++) {
  float value=f32(f,6+4*i);
  if(!std::isfinite(value)||value<1.0f||value>5.2f)continue;
  t.cells.push_back(double(value));
 }
 if(t.cells.size()<2)return {};
 // JK04 has different pack/SOC offsets; do not show an unverified sum as measured pack voltage.
 return t;
}
inline std::optional<Telemetry> decode(std::vector<uint8_t> const& frame,Protocol selection) {
 if(selection==Protocol::JK02_24S||selection==Protocol::JK02_32S)return decodeJk02(frame,selection);
 if(selection==Protocol::JK04)return decodeJk04(frame);
 auto result=decodeJk02(frame,Protocol::JK02_32S);if(result)return result;
 result=decodeJk02(frame,Protocol::JK02_24S);if(result)return result;
 return decodeJk04(frame);
}
class FrameAssembler {
 std::vector<uint8_t> pending_;
 public:
 uint64_t valid=0,checksumErrors=0;
 void reset(){pending_.clear();valid=0;checksumErrors=0;}
 template<typename Callback> void add(std::vector<uint8_t> const& fragment,Callback onFrame) {
  pending_.insert(pending_.end(),fragment.begin(),fragment.end());
  const std::array<uint8_t,4> prefix{0x55,0xAA,0xEB,0x90};
  while(pending_.size()>=4) {
   auto begin=std::search(pending_.begin(),pending_.end(),prefix.begin(),prefix.end());
   if(begin==pending_.end()) {
    // Retain last 3 bytes for a possible prefix split across notifications.
    if(pending_.size()>3)pending_.erase(pending_.begin(),pending_.end()-3);
    break;
   }
   if(begin!=pending_.begin())pending_.erase(pending_.begin(),begin);
   if(pending_.size()<300)break;
   uint8_t crc=0;for(size_t i=0;i<299;i++)crc=uint8_t(crc+pending_[i]);
   if(crc!=pending_[299]) {
    checksumErrors++;
    pending_.erase(pending_.begin());
    continue;
   }
   std::vector<uint8_t> frame(pending_.begin(),pending_.begin()+300);
   pending_.erase(pending_.begin(),pending_.begin()+300);
   valid++;onFrame(frame);
  }
  if(pending_.size()>4096)pending_.clear();
 }
};
} // namespace jk
