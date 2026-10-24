#include "../src/jk_protocol.h"
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
 std::cout<<"JK protocol frame assembler and 24S/32S/JK04 decoder tests: PASS\n";
}
