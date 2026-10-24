#pragma once
// JK BMS BLE Settings (0x01) + register commands.
// Field offsets and write addresses verified against syssi/esphome-jk-bms
// components/jk_bms_ble/jk_bms_ble.cpp and number/switch mappings.
// Never infer JK04 settings from JK02. JK04 exposes only confirmed fields.
#include "jk_protocol.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
namespace jk {
enum class Encoding { UInt32, Int32, UInt8, Float32, UInt16 };
struct Setting {
 const char* id;
 const char* group;
 const char* label;
 const char* unit;
 int offset02;
 int offset04;
 uint8_t reg24,reg32,reg04;
 uint8_t len02,len04;
 double factor02,factor04;
 Encoding format02,format04;
 double min,max;
 uint8_t precision;
 bool editable=true;
};
inline std::vector<Setting> const& settings(){
 static const std::vector<Setting> fields={
 // id, section, caption, unit, offsets 02/04, holding registers 24/32/04,
 // command lengths 02/04, scale 02/04, data types, conservative UI limits
 {"cell_uvp","Điện áp cell","Cell UVP","V",10,-1,0x02,0x02,0,4,0,1000,1,Encoding::UInt32,Encoding::Float32,1.2,4.35,3},
 {"cell_uvpr","Điện áp cell","Cell UVP recovery","V",14,-1,0x03,0x03,0,4,0,1000,1,Encoding::UInt32,Encoding::Float32,1.2,4.35,3},
 {"cell_ovp","Điện áp cell","Cell OVP","V",18,-1,0x04,0x04,0,4,0,1000,1,Encoding::UInt32,Encoding::Float32,1.2,4.35,3},
 {"cell_ovpr","Điện áp cell","Cell OVP recovery","V",22,-1,0x05,0x05,0,4,0,1000,1,Encoding::UInt32,Encoding::Float32,1.2,4.35,3},
 {"balance_delta","Cân bằng","Delta bắt đầu cân bằng","V",26,106,0x06,0x06,0x64,4,4,1000,1,Encoding::UInt32,Encoding::Float32,0.002,1.0,3},
 {"soc100_voltage","SOC","Cell SOC 100%","V",30,-1,0x07,0x07,0,4,0,1000,1,Encoding::UInt32,Encoding::Float32,1.5,4.35,3},
 {"soc0_voltage","SOC","Cell SOC 0%","V",34,-1,0x08,0x08,0,4,0,1000,1,Encoding::UInt32,Encoding::Float32,1.5,4.35,3},
 {"power_off_voltage","Điện áp cell","Ngắt nguồn điện áp cell","V",46,38,0x0B,0x0B,0x20,4,4,1000,1,Encoding::UInt32,Encoding::Float32,1.2,4.35,3},
 {"max_charge_current","Dòng điện","Giới hạn dòng sạc","A",50,-1,0x0C,0x0C,0,4,0,1000,1,Encoding::UInt32,Encoding::Float32,1.0,600.0,1},
 {"charge_ocp_delay","Dòng điện","Trễ quá dòng sạc","s",54,-1,0x0D,0x0D,0,4,0,1,1,Encoding::UInt32,Encoding::Float32,2,600,0},
 {"charge_ocp_recovery","Dòng điện","Khôi phục quá dòng sạc","s",58,-1,0x0E,0x0E,0,4,0,1,1,Encoding::UInt32,Encoding::Float32,2,600,0},
 {"max_discharge_current","Dòng điện","Giới hạn dòng xả","A",62,-1,0x0F,0x0F,0,4,0,1000,1,Encoding::UInt32,Encoding::Float32,1,1200,1},
 {"discharge_ocp_delay","Dòng điện","Trễ quá dòng xả","s",66,-1,0x10,0x10,0,4,0,1,1,Encoding::UInt32,Encoding::Float32,2,600,0},
 {"discharge_ocp_recovery","Dòng điện","Khôi phục quá dòng xả","s",70,-1,0x11,0x11,0,4,0,1,1,Encoding::UInt32,Encoding::Float32,2,600,0},
 {"short_recovery","Ngắn mạch","Thời gian hồi phục SCP","s",74,-1,0x12,0x12,0,4,0,1,1,Encoding::UInt32,Encoding::Float32,0,600,0},
 {"max_balance_current","Cân bằng","Dòng cân bằng tối đa","A",78,110,0x13,0x13,0x68,4,4,1000,1,Encoding::UInt32,Encoding::Float32,0.1,15,2},
 {"charge_otp","Nhiệt độ","Nhiệt độ ngắt sạc (OTP)","°C",82,-1,0x14,0x14,0,4,0,10,1,Encoding::UInt32,Encoding::Float32,30,100,1},
 {"charge_otp_recovery","Nhiệt độ","Hồi phục nhiệt sạc","°C",86,-1,0x15,0x15,0,4,0,10,1,Encoding::UInt32,Encoding::Float32,0,100,1},
 {"discharge_otp","Nhiệt độ","Nhiệt độ ngắt xả (OTP)","°C",90,-1,0x16,0x16,0,4,0,10,1,Encoding::UInt32,Encoding::Float32,30,100,1},
 {"discharge_otp_recovery","Nhiệt độ","Hồi phục nhiệt xả","°C",94,-1,0x17,0x17,0,4,0,10,1,Encoding::UInt32,Encoding::Float32,0,100,1},
 {"charge_utp","Nhiệt độ","Nhiệt độ thấp ngắt sạc","°C",98,-1,0x18,0x18,0,4,0,10,1,Encoding::Int32,Encoding::Float32,-45,25,1},
 {"charge_utp_recovery","Nhiệt độ","Hồi phục nhiệt thấp sạc","°C",102,-1,0x19,0x19,0,4,0,10,1,Encoding::Int32,Encoding::Float32,-45,40,1},
 {"mos_otp","Nhiệt độ","MOSFET quá nhiệt","°C",106,-1,0x1A,0x1A,0,4,0,10,1,Encoding::Int32,Encoding::Float32,40,130,1},
 {"mos_otp_recovery","Nhiệt độ","MOSFET hồi phục nhiệt","°C",110,-1,0x1B,0x1B,0,4,0,10,1,Encoding::Int32,Encoding::Float32,20,120,1},
 {"cell_count","Pin","Số cell thiết lập","cell",114,34,0x1C,0x1C,0x1C,4,2,1,1,Encoding::UInt32,Encoding::UInt16,2,32,0},
 {"charge_mos","MOSFET","Charge MOS","",118,-1,0x1D,0x1D,0,4,0,1,1,Encoding::UInt32,Encoding::UInt8,0,1,0},
 {"discharge_mos","MOSFET","Discharge MOS","",122,-1,0x1E,0x1E,0,4,0,1,1,Encoding::UInt32,Encoding::UInt8,0,1,0},
 {"balance_switch","Cân bằng","Bật cân bằng","",126,114,0x1F,0x1F,0x6C,4,1,1,1,Encoding::UInt32,Encoding::UInt8,0,1,0},
 {"capacity_ah","Pin","Dung lượng danh định","Ah",130,-1,0x20,0x20,0,4,0,1000,1,Encoding::UInt32,Encoding::Float32,2,20000,2},
 {"short_delay","Ngắn mạch","Trễ bảo vệ SCP","µs",134,-1,0x25,0x21,0,4,0,1,1,Encoding::UInt32,Encoding::Float32,0,1000000,0},
 {"balance_start","Cân bằng","Điện áp bắt đầu cân bằng","V",138,98,0x26,0x22,0x58,4,4,1000,1,Encoding::UInt32,Encoding::Float32,1.2,4.25,3}
 };
 return fields;
}
inline Setting const* findSetting(std::string const& id){
 auto const& all=settings();
 auto item=std::find_if(all.begin(),all.end(),[&](Setting const& s){return id==s.id;});
 return item==all.end()?nullptr:&*item;
}
inline uint8_t reg(Setting const& s,Protocol p){
 switch(p){case Protocol::JK02_24S:return s.reg24;case Protocol::JK02_32S:return s.reg32;case Protocol::JK04:return s.reg04;default:return 0;}
}
inline uint8_t commandLen(Setting const& s,Protocol p) {return p==Protocol::JK04?s.len04:s.len02;}
inline bool canWrite(Setting const& s,Protocol p){return s.editable&&reg(s,p)!=0&&commandLen(s,p)!=0;}
inline std::optional<double> readSetting(std::vector<uint8_t> const& frame,Setting const& s,Protocol p){
 if(frame.size()<300||frame[4]!=1)return {};
 int offset=p==Protocol::JK04?s.offset04:(p==Protocol::JK02_24S||p==Protocol::JK02_32S)?s.offset02:-1;
 if(offset<6||offset>295)return {};
 size_t n=static_cast<size_t>(offset);
 Encoding kind=p==Protocol::JK04?s.format04:s.format02;
 double scale=p==Protocol::JK04?s.factor04:s.factor02;
 if(!std::isfinite(scale)||scale==0)return {};
 switch(kind){
 case Encoding::UInt8:return double(frame[n])/scale;
 case Encoding::UInt16:return double(u16(frame,n))/scale;
 case Encoding::UInt32:return double(u32(frame,n))/scale;
 case Encoding::Int32:return double(static_cast<int32_t>(u32(frame,n)))/scale;
 case Encoding::Float32:{
  double v=f32(frame,n);if(!std::isfinite(v))return {};return v/scale;
 }
 }return {};
}
inline bool validSetting(Setting const& s,double value){
 if(!std::isfinite(value)||value<s.min-1e-7||value>s.max+1e-7)return false;
 return true;
}
inline std::optional<uint32_t> encodeValue(Setting const& s,Protocol p,double value){
 if(!canWrite(s,p)||!validSetting(s,value))return {};
 Encoding kind=p==Protocol::JK04?s.format04:s.format02;
 double scaled=value*(p==Protocol::JK04?s.factor04:s.factor02);
 if(kind==Encoding::Float32){
  float f=static_cast<float>(scaled);uint32_t bits=0;std::memcpy(&bits,&f,4);return bits;
 }
 if(kind==Encoding::Int32){
  if(scaled<-2147483648.||scaled>2147483647.)return {};
  return static_cast<uint32_t>(static_cast<int32_t>(std::llround(scaled)));
 }
 if(scaled<0||scaled>4294967295.)return {};
 uint32_t n=static_cast<uint32_t>(std::llround(scaled));
 if(kind==Encoding::UInt8 && n>255)return {};
 if(kind==Encoding::UInt16 && n>65535)return {};
 return n;
}
inline std::array<uint8_t,20> settingWriteFrame(Setting const& s,Protocol p,double value){
 auto result=encodeValue(s,p,value);std::array<uint8_t,20> frame{};
 if(!result)return frame;
 frame[0]=0xAA;frame[1]=0x55;frame[2]=0x90;frame[3]=0xEB;
 frame[4]=reg(s,p);frame[5]=commandLen(s,p);
 frame[6]=uint8_t(*result);frame[7]=uint8_t(*result>>8);
 frame[8]=uint8_t(*result>>16);frame[9]=uint8_t(*result>>24);
 for(size_t i=0;i<19;i++)frame[19]=uint8_t(frame[19]+frame[i]);
 return frame;
}
inline bool crossCheck(std::string const& id,double value,std::vector<uint8_t> const& baseline,Protocol p){
 auto get=[&](char const* key)->std::optional<double>{
  auto spec=findSetting(key);if(!spec)return {};return readSetting(baseline,*spec,p);
 };
 auto checkAbove=[&](char const* key){auto v=get(key);return !v||value > *v;};
 auto checkBelow=[&](char const* key){auto v=get(key);return !v||value < *v;};
 if(id=="soc100_voltage")return checkAbove("soc0_voltage");
 if(id=="soc0_voltage")return checkBelow("soc100_voltage");
 if(id=="cell_ovp")return checkAbove("cell_ovpr");
 if(id=="cell_ovpr")return checkBelow("cell_ovp");
 if(id=="cell_uvp")return checkBelow("cell_uvpr");
 if(id=="cell_uvpr")return checkAbove("cell_uvp");
 if(id=="charge_otp")return checkAbove("charge_otp_recovery");
 if(id=="charge_otp_recovery")return checkBelow("charge_otp");
 if(id=="discharge_otp")return checkAbove("discharge_otp_recovery");
 if(id=="discharge_otp_recovery")return checkBelow("discharge_otp");
 if(id=="mos_otp")return checkAbove("mos_otp_recovery");
 if(id=="mos_otp_recovery")return checkBelow("mos_otp");
 if(id=="charge_utp")return checkBelow("charge_utp_recovery");
 if(id=="charge_utp_recovery")return checkAbove("charge_utp");
 return true;
}
} // namespace jk
