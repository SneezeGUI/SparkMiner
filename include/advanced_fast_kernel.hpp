// ABI and fail-closed known-answer test for MIT NerdMiner-Modded kernel.
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace advanced_fast {
struct Context {
 uint32_t hdr[20]; uint32_t nonce; uint32_t budget;
 volatile uint32_t run; uint32_t pad; uint32_t chk;
};
static_assert(sizeof(Context)==100, "kernel ABI size");
static_assert(offsetof(Context,nonce)==80 && offsetof(Context,budget)==84 &&
 offsetof(Context,run)==88 && offsetof(Context,pad)==92 && offsetof(Context,chk)==96,"kernel ABI offsets");
struct Diagnostic {uint32_t reason=0, before=0, after=0, budget=0, hit=0, found=0;};
using Kernel = uint32_t (*)(volatile uint32_t*, Context*);
// Each invocation is bounded; the callback permits Wi-Fi/display scheduling.
inline bool selftest(Kernel fn, volatile uint32_t* base, void (*yield)(), Diagnostic* diagnostic=nullptr) {
 const uint32_t expected[]={0xa94c,0xe496,0x1f476,0x302f0,0x35af9,0x3cf5c};
 Context c={};
 for(uint32_t i=0;i<19;++i)c.hdr[i]=0x9e3779b9u*(i+1);
 c.nonce=0x1000; c.pad=0x80000000u; c.run=1;
 unsigned found=0;
 for(uint32_t remaining=262144;remaining;) {
  const uint32_t budget=remaining<32768?remaining:32768;
  c.budget=budget;
  const uint32_t before=c.nonce;
  const uint32_t hit=fn(base,&c);
  if(diagnostic){diagnostic->before=before;diagnostic->after=c.nonce;diagnostic->budget=c.budget;diagnostic->hit=hit;diagnostic->found=found;}
  if(hit>1 || c.budget>=budget){if(diagnostic)diagnostic->reason=1;return false;}
  const uint32_t done=budget-c.budget;
  if(c.nonce-before!=done || (!hit && c.budget)){if(diagnostic)diagnostic->reason=2;return false;}
  remaining-=done;
  if(hit && (found>=6 || c.nonce-1-0x1000!=expected[found++])){if(diagnostic)diagnostic->reason=3;return false;}
  yield();
 }
 if(diagnostic)diagnostic->reason=found==6?0:4;
 return found==6;
}
}
extern "C" uint32_t pipelined_hw_mine_classic_v2(volatile uint32_t*,advanced_fast::Context*);
extern "C" uint32_t pipelined_hw_mine_classic_v2_margin(volatile uint32_t*,advanced_fast::Context*);
extern "C" uint32_t pipelined_hw_mine_classic_v2_wide(volatile uint32_t*,advanced_fast::Context*);
