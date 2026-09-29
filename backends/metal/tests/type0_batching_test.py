from pathlib import Path
import subprocess
import os
import tempfile
r=Path(__file__).resolve().parents[1]
sdk=Path(os.environ.get('FABLE2_METAL_DEPS',r.parents[1]/'out/metal-build/dependencies'))/'sdk-source'
s=(sdk/'src/graphics/command_processor.cpp').read_text()
a=s.index('bool CommandProcessor::ExecutePacketType0(');b=s.index('\nbool CommandProcessor::',a+1)
fn=s[a:b]
harness=r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <utility>
#define REXCVAR_GET(x) batching
#define REXGPU_ERROR(...) ((void)0)
bool batching=false;
namespace memory {
struct RingBuffer {
 std::vector<uint32_t> values; size_t start=0,used=0,available=0;
 uint32_t read_count() const {return (available-used)*4;}
 template<class T> T ReadAndSwap() {assert(used<available);return values[(start+used++)%values.size()];}
};
}
struct CommandProcessor {
 std::vector<std::pair<uint32_t,uint32_t>> writes; unsigned ranges=0;
 void WriteRegister(uint32_t a,uint32_t b){writes.emplace_back(a,b);}
 // Contract stub: records every write (including side effects) in order.
 void WriteRegisterRangeFromRing(memory::RingBuffer* r,uint32_t base,uint32_t n) {
  ++ranges;for(uint32_t i=0;i<n;++i)WriteRegister(base+i,r->ReadAndSwap<uint32_t>());
 }
 bool ExecutePacketType0(memory::RingBuffer*,uint32_t);
};
'''+fn+r'''
int main() {
 for(unsigned n:{1u,2u,16u,16384u})for(unsigned base:{0u,0x4000u,0x4800u,0x4900u,0x7fffu})
 for(bool repeat:{false,true})for(bool wrap:{false,true}) {
  memory::RingBuffer a,b;a.values.resize(n+3);a.available=n;a.start=wrap?n:0;
  for(size_t i=0;i<a.values.size();++i)a.values[i]=0x12345678u+uint32_t(i)*97;
  b=a;CommandProcessor old,test;uint32_t packet=((n-1)<<16)|base|(repeat?0x8000:0);
  batching=false;assert(old.ExecutePacketType0(&a,packet));batching=true;assert(test.ExecutePacketType0(&b,packet));
  assert(old.writes==test.writes && a.used==b.used && b.used==n);
  assert(test.ranges==(repeat?0u:1u));
 }
 for(bool enabled:{false,true}) {
  batching=enabled;memory::RingBuffer r;r.values={1,2};r.available=2;CommandProcessor p;
  assert(!p.ExecutePacketType0(&r,3u<<16));assert(p.writes.empty() && r.used==0 && p.ranges==0);
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.cpp';p.write_text(harness)
 exe=Path(tmp)/'test'
 subprocess.run(['/usr/bin/clang++','-std=c++23',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Parser dispatch equivalence: 80 range/repeated/wrap cases and truncated packets passed.')
