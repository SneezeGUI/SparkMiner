#include <advanced_fast_kernel.hpp>
#include <mail_sha_guard.hpp>
#include "miner_sha256.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned mode=0, yields=0;
static void pause_test(){++yields;}
static uint32_t raw_model(volatile uint32_t*, advanced_fast::Context*c){
 if(mode==1)return 0; // No progress must fail, never hang.
 if(mode==2){c->budget++;return 1;}
 block_header_t h; uint32_t *w=reinterpret_cast<uint32_t*>(&h);
 for(unsigned i=0;i<20;++i)w[i]=__builtin_bswap32(c->hdr[i]);
 sha256_hash_t mid={},out={};miner_sha256_midstate(&mid,&h);
 while(c->budget){
  h.nonce=__builtin_bswap32(c->nonce++);--c->budget;
  if(miner_sha256_header(&mid,&out,&h)){
   if(mode==3)++c->nonce; // Inconsistent accounting.
   if(mode==4)return 0; // Concealed hit: known-answer test must reject.
   return 1;
  }
 }
 return 0;
}
struct Memory {
 unsigned locks=0,unlocks=0;bool held=false;
 void lock(){assert(!held);held=true;++locks;}
 void wait_idle(){assert(held);}
 void unlock(){assert(held);held=false;++unlocks;}
};
static Memory memory;
static uint32_t checked_raw(volatile uint32_t*base,advanced_fast::Context*c){assert(memory.held&&c->budget<=1024);return raw_model(base,c);}
static uint32_t model(volatile uint32_t*base,advanced_fast::Context*c){return mail::guarded_kernel(checked_raw,base,c,memory);}
int main(){
 assert(advanced_fast::selftest(model,nullptr,pause_test)); assert(yields>=8);
 for(mode=1;mode<=4;++mode)assert(!advanced_fast::selftest(model,nullptr,pause_test));
 assert(!memory.held&&memory.locks==memory.unlocks&&memory.locks>256);
 puts("PASS: shared SHA memory is locked only around bounded 1024-hash calls; all exits release it;  production software SHA agrees with all vectors; no-progress, corrupt budget, nonce and missing hit rejected");
}
