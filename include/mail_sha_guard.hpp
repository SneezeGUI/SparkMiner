#pragma once
#include <advanced_fast_kernel.hpp>
namespace mail {
// ESP32 SHA1/256/384/512 share SHA_TEXT. Lock the SDK memory block for
// <=1024 hashes at once, never call a scheduler or network operation inside it.
// The SDK driver waits for every engine to be idle before touching shared TEXT.
template<class Driver>
uint32_t guarded_kernel(advanced_fast::Kernel kernel,volatile uint32_t*base,advanced_fast::Context*c,Driver&driver){
 while(c->budget&&c->run){
  const uint32_t budget=c->budget,part=budget<1024?budget:1024;
  c->budget=part;
  driver.lock();driver.wait_idle();
  const uint32_t hit=kernel(base,c);
  driver.unlock();
  if(hit>1||c->budget>part)return 2;
  const uint32_t done=part-c->budget;
  if(!done)return 2;
  c->budget=budget-done;
  if(hit)return hit;
  if(done!=part&&c->run)return 2;
 }
 return 0;
}
}
