#include "../src/jk_protocol.h"
#include "../src/jk_settings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
static void put16(std::vector<uint8_t>& b,size_t i,uint16_t n){b[i]=uint8_t(n);b[i+1]=uint8_t(n>>8);}
static void put32(std::vector<uint8_t>& b,size_t i,uint32_t n){for(int k=0;k<4;k++)b[i+k]=uint8_t(n>>(k*8));}
static void crc(std::vector<uint8_t>& b){uint8_t value=0;for(int i=0;i<299;i++)value=uint8_t(value+b[i]);b[299]=value;}
static std::vector<uint8_t> jk02(jk::Protocol p){
 std::vector<uint8_t> b(300);b[0]=0x55;b[1]=0xAA;b[2]=0xEB;b[3]=0x90;b[4]=2;
 size_t off=p==jk::Protocol::JK02_32S?32:0;
 double total=0;
 for(int i=0;i<8;i++){int millivolts=3300+i;put16(b,6+i*2,millivolts);total+=millivolts;}
 put32(b,54+off/2,0xFF);
 put32(b,118+off,(uint32_t)total);
 put32(b,126+off,(uint32_t)(int32_t)-2500);
 b[141+off]=78;b[166+off]=1;b[167+off]=0;b[140+off]=1;
 crc(b);return b;
}
int main(){
 auto req=jk::request(0x96);
 assert(req.size()==20&&req[4]==0x96&&req[16]==0);
 uint8_t sum=0;for(int i=0;i<19;i++)sum=uint8_t(sum+req[i]);assert(sum==req[19]);
 for(auto p:{jk::Protocol::JK02_24S,jk::Protocol::JK02_32S}){
  auto frame=jk02(p);
  auto t=jk::decode(frame,jk::Protocol::Auto);
  assert(t&&t->protocol==p&&t->packVerified);
  assert(t->cells.size()==8 && std::abs(t->current+2.5)<0.01);
  assert(t->soc==78 && t->charge==1 && t->discharge==0 && t->balance==1);
  assert(std::abs(t->voltage-26.428)<0.001);
  jk::FrameAssembler assembler;int frames=0;
  assembler.add(std::vector<uint8_t>(frame.begin(),frame.begin()+3),[&](auto const&){frames++;});
  assembler.add(std::vector<uint8_t>(frame.begin()+3,frame.begin()+101),[&](auto const&){frames++;});
  assembler.add(std::vector<uint8_t>(frame.begin()+101,frame.end()),[&](auto const&){frames++;});
  assert(frames==1&&assembler.valid==1&&assembler.checksumErrors==0);
  auto damaged=frame;damaged[135]^=0xFF;
  assembler.add(damaged,[&](auto const&){frames++;});
  assert(frames==1&&assembler.checksumErrors==1);
 }
 std::vector<uint8_t> old(300);old[0]=0x55;old[1]=0xAA;old[2]=0xEB;old[3]=0x90;old[4]=2;
 for(int i=0;i<8;i++){float v=3.30f+i*0.001f;uint32_t bits=0;std::memcpy(&bits,&v,4);put32(old,6+4*i,bits);}
 crc(old);
 auto t=jk::decode(old,jk::Protocol::Auto);
 assert(t&&t->protocol==jk::Protocol::JK04&&t->cells.size()==8&&!t->packVerified);
 // Settings reference mappings (JK02_24S + JK02_32S share the first 142 offsets).
 std::vector<uint8_t> settings(300);
 settings[0]=0x55;settings[1]=0xAA;settings[2]=0xEB;settings[3]=0x90;settings[4]=1;
 put32(settings,10,2900);put32(settings,14,3200);put32(settings,18,3650);put32(settings,22,3550);
 put32(settings,26,10);put32(settings,50,30000);put32(settings,62,50000);
 put32(settings,82,600);put32(settings,86,550);
 put32(settings,98,static_cast<uint32_t>(int32_t(-200)));
 put32(settings,102,static_cast<uint32_t>(int32_t(-100)));
 put32(settings,118,1);put32(settings,122,1);put32(settings,126,1);
 put32(settings,138,3400);crc(settings);
 assert(jk::settings().size()>=30);
 auto uvp=jk::findSetting("cell_uvp"),ovp=jk::findSetting("cell_ovp"),balance=jk::findSetting("balance_start");
 assert(uvp&&ovp&&balance);
 for(auto p:{jk::Protocol::JK02_24S,jk::Protocol::JK02_32S}){
  auto v=jk::readSetting(settings,*uvp,p);
  assert(v&&std::abs(*v-2.9)<0.001);
  auto cmd=jk::settingWriteFrame(*uvp,p,2.95);
  assert(cmd[0]==0xAA&&cmd[4]==0x02&&cmd[5]==4);
  uint8_t crc8=0;for(int i=0;i<19;i++)crc8=uint8_t(crc8+cmd[i]);assert(crc8==cmd[19]);
  assert(uint32_t(cmd[6])+(uint32_t(cmd[7])<<8)==2950);
  auto balanceCommand=jk::settingWriteFrame(*balance,p,3.4);
  assert(balanceCommand[4]==(p==jk::Protocol::JK02_24S?0x26:0x22));
  assert(!jk::crossCheck("cell_ovp",3.4,settings,p)); // < 3.55 OVPR
  assert(!jk::crossCheck("cell_uvp",3.4,settings,p)); // > 3.2 UVPR
  assert(jk::crossCheck("cell_uvp",2.95,settings,p));
  assert(!jk::encodeValue(*uvp,p,0.5));
  assert(jk::encodeValue(*uvp,p,2.95));
  assert(jk::findSetting("charge_mos")&&jk::canWrite(*jk::findSetting("charge_mos"),p));
 }
 std::vector<uint8_t> settings04(300);
 settings04[4]=1;put32(settings04,34,16);
 auto putFloat=[&](size_t off,float v){uint32_t bits=0;std::memcpy(&bits,&v,4);put32(settings04,off,bits);};
 putFloat(98,3.4f);putFloat(106,0.012f);putFloat(110,2.0f);put32(settings04,114,1);
 auto balStart=jk::findSetting("balance_start");
 assert(balStart&&jk::readSetting(settings04,*balStart,jk::Protocol::JK04));
 auto j4v=jk::readSetting(settings04,*balStart,jk::Protocol::JK04);
 assert(j4v&&std::abs(*j4v-3.4)<0.00001);
 auto j4cmd=jk::settingWriteFrame(*balStart,jk::Protocol::JK04,3.45);
 assert(j4cmd[4]==0x58&&j4cmd[5]==4);
 assert(!jk::canWrite(*uvp,jk::Protocol::JK04));
 auto reset=jk::request(0x66);
 assert(reset[4]==0x66&&reset[5]==0);
 std::cout<<"JK settings mapping, guarded writes, JK04 float, and restart frame tests: PASS\n";
 std::cout<<"JK protocol frame assembler and 24S/32S/JK04 decoder tests: PASS\n";
}
