"""Exercise the actual readback block with fake GPU completion and guarded RAM."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'port/rex/graphics/metal/metal_command_processor.cc').read_text()
a=s.index('  const auto copy_control =',s.index('bool MetalCommandProcessor::IssueCopy()'))
b=s.index('\n  return true;\n}',a)
block=s[a:b]
harness=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
#define REXCVAR_GET(x) enabled
#define REXLOG_ERROR(...) ((void)0)
#define REXLOG_INFO(...) ((void)0)
constexpr int XE_GPU_REG_RB_COPY_DEST_BASE=0;
namespace xenos { constexpr int kMaxColorRenderTargets=4; enum class ColorFormat { k_8_8_8_8, Other }; }
namespace reg { struct RB_COPY_CONTROL { int copy_src_select=0; }; struct RB_COPY_DEST_INFO { xenos::ColorFormat copy_dest_format=xenos::ColorFormat::k_8_8_8_8; }; }
namespace MTL { constexpr int CommandBufferStatusCompleted=1; constexpr int CommandBufferStatusError=2; }
enum class RenderEncoderEndReason { kResolveNeedsBoundary };
struct Registers { uint32_t base=0x12704000; reg::RB_COPY_CONTROL control; reg::RB_COPY_DEST_INFO info;
template<class T> T Get() { if constexpr(__is_same(T,reg::RB_COPY_CONTROL)) return control; else return info; }
uint32_t operator[](int) { return base; } } regs, *register_file_=&regs;
struct Texture { bool scaled=false; bool IsDrawResolutionScaled(){return scaled;} } tex,*texture_cache_=&tex;
struct Buffer { unsigned char data[64]; void* contents(){return data;} uint64_t length(){return 64;} } buf;
struct Shared { Buffer* GetBuffer(){return &buf;} } shared,*shared_memory_=&shared;
struct Memory { unsigned char data[64]; unsigned char* TranslatePhysical(uint32_t a){return data+a;} } mem,*memory_=&mem;
struct Completion { int state=1; int pending_polls=0; void retain(){} void release(){} int status(){if(pending_polls>0){--pending_polls;return 0;}return state;} } completion;
bool enabled=true; int waits=0; bool submit=true;
Completion* EnsureCommandBuffer(){return submit?&completion:nullptr;}
void EndRenderEncoder(RenderEncoderEndReason){}
void FlushCommandBufferAndWait(uint64_t,const char*){++waits;}
bool Run(uint32_t written_address,uint32_t written_length) {
BLOCK
return true;
}
void Reset(){regs={};tex={};enabled=true;waits=0;submit=true;completion.state=1;completion.pending_polls=0;memset(mem.data,0xCC,64);memset(buf.data,0x5A,64);}
void Untouched(){for(auto x:mem.data)assert(x==0xCC);}
int main(){
Reset();assert(Run(8,16));assert(waits==1);for(int i=0;i<64;++i)assert(mem.data[i]==(i>=8&&i<24?0x5A:0xCC));
Reset();enabled=false;assert(Run(8,16));Untouched();assert(!waits);
Reset();regs.base++;assert(Run(8,16));Untouched();assert(!waits);
Reset();regs.control.copy_src_select=4;assert(Run(8,16));Untouched();assert(!waits);
Reset();regs.info.copy_dest_format=xenos::ColorFormat::Other;assert(Run(8,16));Untouched();assert(!waits);
Reset();tex.scaled=true;assert(Run(8,16));Untouched();assert(!waits);
Reset();assert(Run(8,0));Untouched();assert(!waits);
Reset();assert(!Run(60,16));Untouched();assert(!waits);
Reset();assert(!Run(0xFFFFFFF0,32));Untouched();assert(!waits);
Reset();completion.pending_polls=3;assert(Run(8,16));assert(mem.data[8]==0x5A);
Reset();completion.state=2;assert(!Run(8,16));Untouched();assert(waits==1);
Reset();completion.state=0;assert(!Run(8,16));Untouched();assert(waits==1);
Reset();submit=false;assert(!Run(8,16));Untouched();assert(!waits);
}
'''.replace('BLOCK',block)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(harness)
 subprocess.run(['clang++','-std=c++20',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('13 readback control-flow/bounds tests passed (GPU and guest memory are test doubles).')
