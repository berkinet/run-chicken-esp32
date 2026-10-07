#pragma once
#include <stdint.h>
#include <stddef.h>
inline void feederPacket(uint8_t *p, uint64_t seconds, uint8_t action, bool hello=false) {
  for (int i=0;i<32;++i) p[i]=0;
  p[0]=hello ? 1 : 0;
  for (int i=0;i<8;++i) p[i+1]=(seconds>>(i*8))&255;
  p[21]=action;
  uint8_t crc=0;
  for(int i=0;i<31;++i) { crc^=p[i]; for(int j=0;j<8;++j) crc=(crc&128)?(crc<<1)^7:crc<<1; }
  p[31]=crc;
}
// Only the observed final-state notification confirms a completed movement.
inline int feederFinalState(const uint8_t *p, size_t n) {
  if(n<18 || p[0]!=0x81 || p[15]!=0 || p[17]>1) return -1;
  return p[17]; // 0=open, 1=closed
}
