/* Diagnostic Linux i386 ABI host for AOT code. Unknown behavior traps; no
 * interpreter fallback and no pretend-success stubs for game subsystems. */
#include "recomp.h"
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#define GUEST_BASE 0x08000000u
#define GUEST_SIZE 0x0a000000u
#define HEAP_BASE 0x09600000u
#define STACK_TOP 0x11fff000u
uint8_t *g_mem;
volatile uint8_t g_guest_lock;
int g_preempt_on=1;
static CPU cpu;
static jmp_buf stop;
static char reason[256];
static char last_message[256];
static uint32_t stop_address, heap, calls, host_calls, completed_ctors, completed_library_ctors;
static int phase;
static struct {uint32_t fn,arg,dso;int active;} destructors[4096];
static unsigned destructor_count;
static struct {uint32_t value,destructor;int active;} tls_keys[128];
static uint32_t live_bytes,peak_live_bytes,last_allocation_size;
static uint32_t random_state[31];
static unsigned random_front,random_rear;
static double ticks_started;
static uint32_t entry_budget=2000000;
static uint32_t entry_budget_start;
static uint32_t guest_rand(void){uint32_t sum=random_state[random_front]+random_state[random_rear];random_state[random_front]=sum;random_front=(random_front+1)%31;random_rear=(random_rear+1)%31;return sum>>1;}
static void guest_srand(uint32_t seed){
    if(!seed)seed=1;random_state[0]=seed;int64_t word=(int32_t)seed;
    for(unsigned i=1;i<31;i++){word=(16807*word)%2147483647;if(word<0)word+=2147483647;random_state[i]=word;}
    random_front=3;random_rear=0;for(unsigned i=0;i<310;i++)guest_rand();
}
static void invoke(uint32_t target);
static struct {uint32_t addr; GuestFn fn;} dispatch[32768];

uint8_t *guest_pointer(uint32_t a,uint32_t n) {
    if(a<GUEST_BASE || n>GUEST_SIZE || a-GUEST_BASE>GUEST_SIZE-n) {
        snprintf(reason,sizeof reason,"guest memory out of bounds (%u bytes)",n);
        stop_address=a;longjmp(stop,1);
    }
    return g_mem+(a-GUEST_BASE);
}
void guest_bounds_fail(uint32_t a,int n){(void)guest_pointer(a,n);}
void guest_trap(CPU*c,uint32_t a,const char*why){(void)c;stop_address=a;snprintf(reason,sizeof reason,"%s",why);longjmp(stop,1);}
void register_function(uint32_t addr,GuestFn fn){uint32_t i=(addr*2654435761u)>>17;while(dispatch[i].fn&&dispatch[i].addr!=addr)i=(i+1)&32767;dispatch[i].addr=addr;dispatch[i].fn=fn;}
static GuestFn lookup(uint32_t addr){uint32_t i=(addr*2654435761u)>>17;while(dispatch[i].fn){if(dispatch[i].addr==addr)return dispatch[i].fn;i=(i+1)&32767;}return NULL;}
#include "image.h"

void guest_preempt(CPU*c){c->preempt_ticks=0;calls+=PREEMPT_QUANTUM;if((uint32_t)(calls-entry_budget_start)>=entry_budget)guest_trap(c,R32(c->esp),"function-entry budget between frames exceeded");
#ifdef AOT_ASYNC
    emscripten_sleep(0);
#endif
}
EMSCRIPTEN_KEEPALIVE int set_entry_budget(uint32_t limit){if(limit<100000||limit>500000000)return 1;entry_budget=limit;return 0;}
EMSCRIPTEN_KEEPALIVE void renew_entry_budget(void){entry_budget_start=calls;}
void trace_insn(CPU*c,uint32_t a){(void)c;(void)a;}
void fpu_sync_rounding(CPU*c){if(((c->fpu_cw>>10)&3)!=0)guest_trap(c,0,"non-default x87 rounding not implemented");}
double fpu_round(CPU*c,double v){unsigned mode=(c->fpu_cw>>10)&3;return mode==3?trunc(v):mode==1?floor(v):mode==2?ceil(v):nearbyint(v);}
int32_t fpu_to_int32(CPU*c,double x){unsigned mode=(c->fpu_cw>>10)&3;double v=mode==3?trunc(x):mode==1?floor(x):mode==2?ceil(x):nearbyint(x);return !isfinite(v)||v<INT32_MIN||v>INT32_MAX?INT32_MIN:(int32_t)v;}
int64_t fpu_to_int64(CPU*c,double x){unsigned mode=(c->fpu_cw>>10)&3;double v=mode==3?trunc(x):mode==1?floor(x):mode==2?ceil(x):nearbyint(x);return !isfinite(v)||v<-9223372036854775808.0||v>=9223372036854775808.0?INT64_MIN:(int64_t)v;}
double RF80(uint32_t a){guest_trap(&cpu,a,"80-bit float read not implemented");}
void WF80(uint32_t a,double v){(void)v;guest_trap(&cpu,a,"80-bit float write not implemented");}
void cpuid(CPU*c){guest_trap(c,0,"CPUID not implemented");}
void rdtsc(CPU*c){guest_trap(c,0,"RDTSC not implemented");}

static uint32_t arg(CPU*c,unsigned n){return R32(c->esp+4+4*n);}
static void returned(CPU*c,uint32_t result){c->eax=result;c->esp+=4;}
#include "elf_allocator.h"
static uint32_t string_length(uint32_t p){uint32_t n=0;while(R8(p+n))n++;return n;}
static FILE *standard_stream(uint32_t handle){
    if(handle==0x70000010u)return stdin;if(handle==0x70000020u)return stdout;if(handle==0x70000030u)return stderr;
    guest_trap(&cpu,handle,"unknown guest FILE handle");
}
static int math_import(CPU*c,const char*name){
    static const struct {const char*name;double(*wide)(double);float(*narrow)(float);} unary[]={
        {"acos",acos,acosf},{"asin",asin,asinf},{"atan",atan,atanf},
        {"sin",sin,sinf},{"cos",cos,cosf},{"tan",tan,tanf},
        {"sqrt",sqrt,sqrtf},{"floor",floor,floorf},{"ceil",ceil,ceilf},
        {"log",log,logf},{"log10",log10,log10f},{"exp",exp,expf},{"fabs",fabs,fabsf}
    };
    for(unsigned i=0;i<sizeof(unary)/sizeof(*unary);i++){
        size_t n=strlen(unary[i].name);int narrow=!strncmp(name,unary[i].name,n)&&name[n]=='f'&&!name[n+1];
        if(!strcmp(name,unary[i].name)||narrow){double value=narrow?(double)unary[i].narrow(RF32(c->esp+4)):unary[i].wide(RF64(c->esp+4));FPUSH(c,value);c->esp+=4;return 1;}
    }
    if(!strcmp(name,"atan2")||!strcmp(name,"pow")||!strcmp(name,"fmod")){
        double a=RF64(c->esp+4),b=RF64(c->esp+12);FPUSH(c,!strcmp(name,"atan2")?atan2(a,b):!strcmp(name,"pow")?pow(a,b):fmod(a,b));c->esp+=4;return 1;
    }
    if(!strcmp(name,"atan2f")||!strcmp(name,"powf")||!strcmp(name,"fmodf")){
        float a=RF32(c->esp+4),b=RF32(c->esp+8);FPUSH(c,!strcmp(name,"atan2f")?atan2f(a,b):!strcmp(name,"powf")?powf(a,b):fmodf(a,b));c->esp+=4;return 1;
    }
    if(!strcmp(name,"__fpclassify")){returned(c,fpclassify(RF64(c->esp+4)));return 1;}
    return 0;
}
#include "elf_locale.h"
#include "elf_multibyte.h"
#include "elf_files.h"
#include "elf_scan.h"
#include "elf_print.h"
#include "elf_mixer.h"
#include "elf_sdl.h"

static void host_import(CPU*c,const Import*entry){
    host_calls++;
    const char *name=entry->name;
    if(locale_import(c,name))return;
    if(multibyte_import(c,name))return;
    if(math_import(c,name))return;
    if(file_import(c,name))return;
    if(scan_import(c,name))return;
    if(print_import(c,name))return;
    if(mixer_import(c,name))return;
    if(sdl_import(c,name))return;
    if(!strcmp(name,"time")){uint32_t now=(uint32_t)time(NULL),pointer=arg(c,0);if(pointer)W32(pointer,now);returned(c,now);return;}
    if(!strcmp(name,"srand")){guest_srand(arg(c,0));returned(c,0);return;}
    if(!strcmp(name,"rand")){returned(c,guest_rand());return;}
    if(!strcmp(name,"SDL_GetTicks")){returned(c,(uint32_t)(emscripten_get_now()-ticks_started));return;}
    if(!strcmp(name,"getenv")){
        uint32_t p=arg(c,0);const char*key=(char*)guest_pointer(p,string_length(p)+1),*value=NULL;
        if(!strcmp(key,"HOME"))value="/home";
        else if(!strcmp(key,"USER"))value="vascolet";
        else if(!strcmp(key,"LANG")||!strcmp(key,"LC_ALL"))value="C";
        else if(!strcmp(key,"SUGAR_ACTIVITY_ROOT"))value=GAME_NUMBER==2?"/home/vascolet2":"/home/vascolet3";
        else if(!strcmp(key,"SUGAR_BUNDLE_PATH"))value="/game";
        uint32_t result=0;if(value){unsigned size=strlen(value)+1;result=allocate(size);memcpy(guest_pointer(result,size),value,size);}returned(c,result);return;
    }
    if(!strcmp(name,"malloc")||!strcmp(name,"_Znwj")||!strcmp(name,"_Znaj")){returned(c,allocate(arg(c,0)));return;}
    if(!strcmp(name,"free")||!strcmp(name,"_ZdlPv")||!strcmp(name,"_ZdaPv")){release(arg(c,0));returned(c,0);return;}
    if(!strcmp(name,"calloc")){uint64_t size=(uint64_t)arg(c,0)*arg(c,1);returned(c,size>UINT32_MAX?0:allocate(size));return;}
    if(!strcmp(name,"realloc")){uint32_t p=arg(c,0),n=arg(c,1);if(!p){returned(c,allocate(n));return;}unsigned i=allocation_index(p);if(!n){release(p);returned(c,0);return;}if(n<=allocations[i].size){returned(c,p);return;}uint32_t next=allocate(n);memcpy(guest_pointer(next,n),guest_pointer(p,allocations[i].size),allocations[i].size);release(p);returned(c,next);return;}
    if(!strcmp(name,"__cxa_guard_acquire")){returned(c,R8(arg(c,0))?0:1);return;}
    if(!strcmp(name,"__cxa_guard_release")){W8(arg(c,0),1);returned(c,0);return;}
    if(!strcmp(name,"memcpy")||!strcmp(name,"memmove")){uint32_t dst=arg(c,0),src=arg(c,1),n=arg(c,2);if(n)memmove(guest_pointer(dst,n),guest_pointer(src,n),n);returned(c,dst);return;}
    if(!strcmp(name,"memset")){uint32_t dst=arg(c,0),n=arg(c,2);if(n)memset(guest_pointer(dst,n),arg(c,1),n);returned(c,dst);return;}
    if(!strcmp(name,"wmemcpy")||!strcmp(name,"wmemmove")){
        uint32_t dst=arg(c,0),src=arg(c,1),n=arg(c,2);uint64_t bytes=(uint64_t)n*4;
        if(bytes>UINT32_MAX)guest_trap(c,entry->address,"wide copy size overflow");
        if(bytes)memmove(guest_pointer(dst,bytes),guest_pointer(src,bytes),bytes);returned(c,dst);return;
    }
    if(!strcmp(name,"wmemset")){uint32_t dst=arg(c,0),value=arg(c,1),n=arg(c,2);if((uint64_t)n*4>UINT32_MAX)guest_trap(c,entry->address,"wide fill size overflow");if(n)guest_pointer(dst,n*4);for(unsigned i=0;i<n;i++)W32(dst+i*4,value);returned(c,dst);return;}
    if(!strcmp(name,"wmemcmp")){uint32_t a=arg(c,0),b=arg(c,1),n=arg(c,2);if((uint64_t)n*4>UINT32_MAX)guest_trap(c,entry->address,"wide comparison size overflow");if(n){guest_pointer(a,n*4);guest_pointer(b,n*4);}int result=0;for(unsigned i=0;i<n;i++){int32_t x=R32(a+i*4),y=R32(b+i*4);if(x!=y){result=x<y?-1:1;break;}}returned(c,result);return;}
    if(!strcmp(name,"wmemchr")||!strcmp(name,"memchr")){uint32_t p=arg(c,0),value=arg(c,1),n=arg(c,2),width=!strcmp(name,"wmemchr")?4:1;if((uint64_t)n*width>UINT32_MAX)guest_trap(c,entry->address,"memory search size overflow");if(n)guest_pointer(p,n*width);uint32_t found=0;for(unsigned i=0;i<n;i++)if((width==4?R32(p+i*width):R8(p+i))==(width==4?value:value&255)){found=p+i*width;break;}returned(c,found);return;}
    if(!strcmp(name,"__cxa_atexit")||!strcmp(name,"__cxa_thread_atexit_impl")){
        if(destructor_count==4096)guest_trap(c,entry->address,"destructor registry full");
        unsigned i=destructor_count++;destructors[i].fn=arg(c,0);destructors[i].arg=arg(c,1);destructors[i].dso=arg(c,2);destructors[i].active=1;returned(c,0);return;
    }
    if(!strcmp(name,"__cxa_finalize")){
        uint32_t dso=arg(c,0);
        for(unsigned i=destructor_count;i-->0;)if(destructors[i].active&&(!dso||destructors[i].dso==dso)){
            destructors[i].active=0;uint32_t sp=c->esp;c->esp-=16;W32(c->esp,destructors[i].arg);invoke(destructors[i].fn);c->esp=sp;
        }
        returned(c,0);return;
    }
    if(!strcmp(name,"pthread_once")||!strcmp(name,"__pthread_once")){
        uint32_t control=arg(c,0),fn=arg(c,1),state=R32(control);
        if(state==1)guest_trap(c,entry->address,"recursive pthread_once");
        if(state!=2){W32(control,1);invoke(fn);W32(control,2);}returned(c,0);return;
    }
    if(!strcmp(name,"pthread_mutex_lock")||!strcmp(name,"pthread_mutex_trylock")){
        uint32_t mutex=arg(c,0);
        /* Single guest thread only. A contended default mutex cannot make
         * progress; report it rather than silently ignoring synchronization. */
        if(R32(mutex)){
            if(!strcmp(name,"pthread_mutex_trylock")){returned(c,16);return;}
            guest_trap(c,entry->address,"contended mutex in single-thread diagnostic runtime");
        }
        W32(mutex,1);returned(c,0);return;
    }
    if(!strcmp(name,"pthread_mutex_unlock")){uint32_t mutex=arg(c,0);if(!R32(mutex))guest_trap(c,entry->address,"unlock of unlocked mutex");W32(mutex,0);returned(c,0);return;}
    if(!strcmp(name,"pthread_key_create")||!strcmp(name,"__pthread_key_create")){
        for(unsigned i=0;i<128;i++)if(!tls_keys[i].active){tls_keys[i].active=1;tls_keys[i].value=0;tls_keys[i].destructor=arg(c,1);W32(arg(c,0),i);returned(c,0);return;}
        returned(c,11);return;
    }
    if(!strcmp(name,"pthread_getspecific")){uint32_t key=arg(c,0);returned(c,key<128&&tls_keys[key].active?tls_keys[key].value:0);return;}
    if(!strcmp(name,"pthread_setspecific")){uint32_t key=arg(c,0);if(key>=128||!tls_keys[key].active){returned(c,22);return;}tls_keys[key].value=arg(c,1);returned(c,0);return;}
    if(!strcmp(name,"pthread_key_delete")){uint32_t key=arg(c,0);if(key>=128||!tls_keys[key].active){returned(c,22);return;}tls_keys[key].active=0;returned(c,0);return;}
    if(!strcmp(name,"strlen")||!strcmp(name,"wcslen")){
        uint32_t p=arg(c,0),n=0,width=!strcmp(name,"wcslen")?4:1;
        while(width==4?R32(p+n*4):R8(p+n))n++;
        returned(c,n);return;
    }
    if(!strcmp(name,"strcmp")||!strcmp(name,"strncmp")){
        uint32_t a=arg(c,0),b=arg(c,1),n=!strcmp(name,"strncmp")?arg(c,2):UINT32_MAX;int value=0;
        for(uint32_t i=0;i<n;i++){unsigned x=R8(a+i),y=R8(b+i);if(x!=y){value=(int)x-(int)y;break;}if(!x)break;}
        returned(c,value);return;
    }
    if(!strcmp(name,"memcmp")){uint32_t a=arg(c,0),b=arg(c,1),n=arg(c,2);returned(c,n?memcmp(guest_pointer(a,n),guest_pointer(b,n),n):0);return;}
    if(!strcmp(name,"strchr")){uint32_t a=arg(c,0);uint8_t needle=arg(c,1);while(R8(a)&&R8(a)!=needle)a++;returned(c,R8(a)==needle?a:0);return;}
    if(!strcmp(name,"strtol")||!strcmp(name,"strtoul")||!strcmp(name,"atoi")){
        uint32_t a=arg(c,0),end=!strcmp(name,"atoi")?0:arg(c,1);int base=!strcmp(name,"atoi")?10:arg(c,2);char *text=(char*)guest_pointer(a,string_length(a)+1),*last;
        uint32_t value=!strcmp(name,"strtoul")?strtoul(text,&last,base):strtol(text,&last,base);
        if(end)W32(end,a+(last-text));returned(c,value);return;
    }
    if(!strcmp(name,"atof")||!strcmp(name,"strtod")||!strcmp(name,"strtof")||!strcmp(name,"__strtof_l")||!strcmp(name,"__strtod_l")||!strcmp(name,"strtof_l")||!strcmp(name,"strtod_l")){
        int localized=strstr(name,"_l")!=NULL,narrow=strstr(name,"strtof")!=NULL;if(localized&&arg(c,2)!=C_LOCALE)guest_trap(c,arg(c,2),"unsupported numeric conversion locale");
        uint32_t p=arg(c,0),end=!strcmp(name,"atof")?0:arg(c,1);char*text=(char*)guest_pointer(p,string_length(p)+1),*last;errno=R32(cpu.fs_base+64);double value=narrow?(double)strtof(text,&last):strtod(text,&last);W32(cpu.fs_base+64,errno);if(end)W32(end,p+(last-text));FPUSH(c,value);c->esp+=4;return;
    }
    if(!strcmp(name,"strstr")){uint32_t haystack=arg(c,0),needle=arg(c,1);const char*h=guest_text(haystack),*found=strstr(h,guest_text(needle));returned(c,found?haystack+(found-h):0);return;}
    if(!strcmp(name,"_setjmp")||!strcmp(name,"setjmp")){
        /* Initial return only. Preserve the opaque guest register image, but
         * do NOT claim that a later non-local C continuation is implemented. */
        uint32_t buffer=arg(c,0);W32(buffer,c->ebx);W32(buffer+4,c->esi);W32(buffer+8,c->edi);W32(buffer+12,c->ebp);W32(buffer+16,c->esp+4);W32(buffer+20,R32(c->esp));returned(c,0);return;
    }
    if(!strcmp(name,"longjmp")||!strcmp(name,"_longjmp"))guest_trap(c,entry->address,"guest longjmp continuation not compiled");
    if(!strcmp(name,"fileno")){uint32_t handle=arg(c,0);standard_stream(handle);returned(c,handle==0x70000010u?0:handle==0x70000020u?1:2);return;}
    if(!strcmp(name,"fwrite")){uint32_t n=arg(c,1),count=arg(c,2);uint64_t bytes=(uint64_t)n*count;if(bytes>UINT32_MAX)guest_trap(c,entry->address,"fwrite size overflow");returned(c,fwrite(guest_pointer(arg(c,0),bytes),n,count,standard_stream(arg(c,3))));return;}
    if(!strcmp(name,"fputs")){uint32_t text=arg(c,0);returned(c,fputs((char*)guest_pointer(text,string_length(text)+1),standard_stream(arg(c,1))));return;}
    if(!strcmp(name,"fflush")){uint32_t handle=arg(c,0);returned(c,fflush(handle?standard_stream(handle):NULL));return;}
    if(!strcmp(name,"__tls_get_addr")||!strcmp(name,"___tls_get_addr")){
        /* i386 GNU TLS resolver receives its descriptor in EAX, not cdecl. */
        uint32_t descriptor=c->eax;if(R32(descriptor)!=1)guest_trap(c,entry->address,"unknown TLS module");
        returned(c,0x08f00000u+R32(descriptor+4));return;
    }
    guest_trap(c,entry->address,name);
}

void call_indirect(CPU*c,uint32_t target){
    GuestFn fn=lookup(target);if(fn){fn(c);return;}
    for(unsigned i=0;i<sizeof(imports)/sizeof(*imports);i++)if(imports[i].address==target){host_import(c,&imports[i]);return;}
    guest_trap(c,target,"unknown indirect call");
}
void jump_indirect(CPU*c,uint32_t target,uint32_t from){(void)from;call_indirect(c,target);}

static void reset(void){
    reset_files();reset_mixer();sdl_screen=sdl_info=sdl_initialized=sdl_frames=0;
    if(!g_mem)g_mem=malloc(GUEST_SIZE);
    if(!g_mem)abort();
    memset(g_mem,0,GUEST_SIZE);memset(&cpu,0,sizeof cpu);memset(tls_keys,0,sizeof tls_keys);
    heap=HEAP_BASE;allocator_reset();entry_budget_start=0;calls=host_calls=completed_ctors=completed_library_ctors=destructor_count=0;phase=0;stop_address=0;reason[0]=last_message[0]=0;
    live_bytes=peak_live_bytes=last_allocation_size=0;
    FILE*f=fopen("/image.bin","rb");if(!f)abort();fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);if(fread(guest_pointer(IMAGE_BASE,size),1,size,f)!=(size_t)size)abort();fclose(f);
    initialize_library();
    register_all_functions();
    for(unsigned i=0;i<sizeof(imports)/sizeof(*imports);i++)if(imports[i].got)W32(imports[i].got,imports[i].address);
    relocate_library();
    cpu.esp=STACK_TOP;cpu.fpu_cw=0x37f;
    cpu.fs_base=0x08e00000u;W32(cpu.fs_base,cpu.fs_base);W32(cpu.fs_base+0x14,0x5a1700u);
    initialize_c_locale();
    guest_srand(1);
    ticks_started=emscripten_get_now();
}
static void invoke(uint32_t target){uint32_t sp=cpu.esp;PUSH32(&cpu,0);call_indirect(&cpu,target);if(cpu.esp!=sp)guest_trap(&cpu,target,"unbalanced guest stack");}

static void print_json_string(const char*s){putchar('"');for(;*s;s++){unsigned char ch=*s;if(ch=='"'||ch=='\\'){putchar('\\');putchar(ch);}else if(ch<32)printf("\\u%04x",ch);else putchar(ch);}putchar('"');}
EMSCRIPTEN_KEEPALIVE int run_startup(void){
    if(!setjmp(stop)){
        reset();phase=4;
        for(unsigned i=0;i<sizeof(library_constructors)/sizeof(*library_constructors);i++){invoke(library_constructors[i]);completed_library_ctors++;}
        phase=1;
        for(unsigned i=0;i<sizeof(constructors)/sizeof(*constructors);i++){invoke(constructors[i]);completed_ctors++;}
        phase=2;
        W32(STACK_TOP,1);W32(STACK_TOP+4,STACK_TOP+32);W32(STACK_TOP+32,STACK_TOP+64);strcpy((char*)HP(STACK_TOP+64),"vascolet");
        invoke(MAIN_ADDRESS);phase=3;
    }
    printf("{\"game\":%d,\"phase\":%d,\"libraryConstructorsCompleted\":%u,\"constructorsCompleted\":%u,\"constructorsTotal\":%zu,\"hostCalls\":%u,\"stopAddress\":%u,\"reason\":",GAME_NUMBER,phase,completed_library_ctors,completed_ctors,sizeof(constructors)/sizeof(*constructors),host_calls,stop_address);
    print_json_string(reason);printf(",\"lastMessage\":");print_json_string(last_message);printf(",\"frames\":%u,\"entryBudgetUsed\":%u,\"heapLiveBytes\":%u,\"heapPeakBytes\":%u,\"lastAllocationBytes\":%u,\"elapsedMs\":%.1f,\"playable\":false}\n",sdl_frames,calls,live_bytes,peak_live_bytes,last_allocation_size,emscripten_get_now()-ticks_started);
    return phase==3?0:1;
}
EMSCRIPTEN_KEEPALIVE int test_printf(void){
    if(setjmp(stop))return 1;reset();
    uint32_t format=allocate(256),args=allocate(64),text=allocate(32),out=allocate(256),count=allocate(8);
    strcpy((char*)HP(text),"guest");strcpy((char*)HP(format),"%s %d %08x %.2f %lld %*.*s%n!");
    W32(args,text);W32(args+4,-42);W32(args+8,0xabc);WF64(args+12,1.25);W64(args+20,0x123456789ll);
    W32(args+28,6);W32(args+32,3);W32(args+36,text);W32(args+40,count);
    const char *expected="guest -42 00000abc 1.25 4886718345    gue!";unsigned n=strlen(expected);
    if(guest_format(out,256,format,args)!=(int)n||strcmp((char*)HP(out),expected)||R32(count)!=n-1)return 2;
    memset(HP(out),0xa5,16);if(guest_format(out,8,format,args)!=(int)n||memcmp(HP(out),expected,7)||R8(out+7)||R8(out+8)!=0xa5)return 3;
    if(guest_format(0,0,format,args)!=(int)n)return 4;
    strcpy((char*)HP(format),"%.*f");W32(args,3);WF64(args+4,12.375);if(guest_format(out,256,format,args)!=6||strcmp((char*)HP(out),"12.375"))return 5;
    strcpy((char*)HP(format),"%hhd %hu %%");W32(args,255);W32(args+4,65537);if(guest_format(out,256,format,args)!=6||strcmp((char*)HP(out),"-1 1 %"))return 6;
    printf("{\"guestPrintf\":true,\"i386UnalignedDouble\":true,\"truncationAndGuards\":true,\"guestStringPointers\":true,\"starWidthPrecision\":true}\n");return 0;
}
EMSCRIPTEN_KEEPALIVE int test_allocator(void){
    if(setjmp(stop))return 1;reset();uint32_t base=heap,live=live_bytes;
    uint32_t *p=malloc(70000*sizeof *p);if(!p)return 2;
    for(unsigned i=0;i<70000;i++){p[i]=allocate(16+(i%15)*16);W32(p[i],i^0xa5a55a5a);}
    for(unsigned i=0;i<70000;i++)if(R32(p[i])!=(i^0xa5a55a5a)){free(p);return 3;}
    for(unsigned i=0;i<70000;i+=2)release(p[i]);
    for(unsigned i=0;i<70000;i+=2){p[i]=allocate(16);W32(p[i],i^0xa5a55a5a);}
    for(unsigned i=0;i<70000;i++)if(R32(p[i])!=(i^0xa5a55a5a)){free(p);return 4;}
    for(unsigned i=70000;i;i--)release(p[i-1]);free(p);
    if(heap!=base||live_bytes!=live||allocation_tail!=ALLOCATION_NONE)return 5;
    uint32_t a=allocate(64),b=allocate(128),c=allocate(64);release(b);
    uint32_t d=allocate(32),e=allocate(64);if(d!=b||e!=b+32)return 6;
    release(d);release(e);release(a);release(c);
    if(heap!=base||live_bytes!=live)return 7;
    printf("{\"allocatorStress\":true,\"simultaneousBlocks\":70000,\"splitAndCoalesce\":true,\"metadataBytes\":%zu}\n",sizeof allocations+sizeof allocation_hash+sizeof allocation_bins);return 0;
}
EMSCRIPTEN_KEEPALIVE int test_host(void){
    if(setjmp(stop))return 1;
    reset();uint32_t locale_sp=cpu.esp;
    if(!locale_import(&cpu,"__ctype_get_mb_cur_max")||cpu.eax!=1||cpu.esp!=locale_sp+4)return 41;
    cpu.esp=locale_sp;active_locale=C_LOCALE;
    if(!locale_import(&cpu,"__ctype_get_mb_cur_max")||cpu.eax!=1||cpu.esp!=locale_sp+4)return 42;
    cpu.esp=locale_sp;active_locale=0xffffffffu;
    printf("{\"cLocaleMultibyteMaximum\":1,\"globalAndThreadLocale\":true,\"cdeclStackBalanced\":true}\n");
    uint32_t wide=allocate(32),narrow=allocate(32),src=allocate(4),state=allocate(8);
    W32(wide,'A');W32(wide+4,'B');W32(wide+8,0);W32(src,wide);memset(HP(narrow),0xa5,32);
    if(guest_wcsnrtombs(narrow,src,3,1,state)!=1||R32(src)!=wide+4||R8(narrow)!='A'||R8(narrow+1)!=0xa5)return 43;
    W32(src,wide);if(guest_wcsnrtombs(0,src,3,0,state)!=2||R32(src)!=wide)return 44;
    if(guest_wcsnrtombs(narrow,src,3,32,state)!=2||R32(src)||strcmp((char*)HP(narrow),"AB"))return 45;
    W32(src,narrow);if(guest_mbsnrtowcs(wide,src,3,8,state)!=2||R32(src)||R32(wide)!='A'||R32(wide+4)!='B'||R32(wide+8))return 46;
    W32(wide,0x100);W32(src,wide);if(guest_wcsnrtombs(narrow,src,1,32,state)!=UINT32_MAX||R32(cpu.fs_base+64)!=84||R32(src)!=wide)return 47;
    W8(narrow,0xff);W32(src,narrow);if(guest_mbsnrtowcs(wide,src,1,8,state)!=UINT32_MAX||R32(cpu.fs_base+64)!=84)return 48;
    printf("{\"cLocaleMultibyteConversions\":true,\"sourcePointerUpdates\":true,\"nullDestinationLength\":true,\"truncationGuards\":true,\"invalidSequenceEILSEQ\":true}\n");
    uint32_t file_frame=allocate(32),file_path=allocate(64),file_data=allocate(16),file_vec=allocate(16),file_stat=allocate(104),file_read=allocate(16),saved_sp=cpu.esp;
    strcpy((char*)HP(file_path),"/tmp/vascolet-fd-regression");memcpy(HP(file_data),"ABCxyz",6);
    cpu.esp=file_frame;W32(file_frame+4,file_path);W32(file_frame+8,2|64|512);W32(file_frame+12,0600);file_import(&cpu,"open");uint32_t fd=cpu.eax;if(fd>=1024)return 49;
    cpu.esp=file_frame;W32(file_frame+4,fd);W32(file_frame+8,file_data);W32(file_frame+12,3);file_import(&cpu,"write");if(cpu.eax!=3)return 50;
    W32(file_vec,file_data+3);W32(file_vec+4,1);W32(file_vec+8,file_data+4);W32(file_vec+12,2);
    cpu.esp=file_frame;W32(file_frame+4,fd);W32(file_frame+8,file_vec);W32(file_frame+12,2);file_import(&cpu,"writev");if(cpu.eax!=3)return 51;
    cpu.esp=file_frame;W32(file_frame+4,fd);W32(file_frame+8,0);W32(file_frame+12,0);W32(file_frame+16,SEEK_SET);file_import(&cpu,"lseek64");if(cpu.eax||cpu.edx)return 52;
    cpu.esp=file_frame;W32(file_frame+4,fd);W32(file_frame+8,file_read);W32(file_frame+12,6);file_import(&cpu,"read");if(cpu.eax!=6||memcmp(HP(file_read),"ABCxyz",6))return 53;
    memset(HP(file_stat),0xa5,104);cpu.esp=file_frame;W32(file_frame+4,3);W32(file_frame+8,fd);W32(file_frame+12,file_stat);file_import(&cpu,"__fxstat64");if(cpu.eax||R64(file_stat+44)!=6||R32(file_stat+96)!=0xa5a5a5a5)return 54;
    cpu.esp=file_frame;W32(file_frame+4,fd);file_import(&cpu,"close");if(cpu.eax)return 55;cpu.esp=saved_sp;unlink((char*)HP(file_path));
    printf("{\"guestFileDescriptors\":true,\"writeAndWritev\":true,\"seek64ReadRoundTrip\":true,\"i386Stat64Guards\":true}\n");
    reset();uint32_t p=allocate(32);W32(p,0x12345678);if(R32(p)!=0x12345678)return 2;
    release(p);uint32_t reused=allocate(16);if(reused!=p)return 7;
    uint32_t block_a=allocate(64),block_b=allocate(64),block_guard=allocate(16);release(block_a);release(block_b);uint32_t combined=allocate(112);if(combined!=block_a)return 19;release(combined);release(block_guard);if(live_bytes!=16)return 20;
    W32(GUEST_BASE+GUEST_SIZE-4,0xabcdef01);if(R32(GUEST_BASE+GUEST_SIZE-4)!=0xabcdef01)return 3;
    CPU test={0};test.fpu_cw=0x37f;if(fpu_to_int32(&test,2.5)!=2||fpu_to_int32(&test,-3.5)!=-4)return 4;
    test.fpu_cw=0xf7f;if(fpu_to_int32(&test,-3.9)!=-3)return 5;
    if(fpu_to_int32(&test,NAN)!=INT32_MIN)return 6;
    guest_srand(1);if(guest_rand()!=1804289383u||guest_rand()!=846930886u||guest_rand()!=1681692777u)return 11;
    for(int ch=0;ch<256;ch++){
        uint16_t mask=R16(CTYPE_TABLE+(ch+128)*2);
        if(!!(mask&0x0100)!=!!isupper(ch)||!!(mask&0x0200)!=!!islower(ch)||!!(mask&0x0400)!=!!isalpha(ch)||!!(mask&0x0800)!=!!isdigit(ch)||!!(mask&0x1000)!=!!isxdigit(ch)||!!(mask&0x2000)!=!!isspace(ch)||!!(mask&0x4000)!=!!isprint(ch)||!!(mask&0x8000)!=!!isgraph(ch)||!!(mask&0x0001)!=!!isblank(ch)||!!(mask&0x0002)!=!!iscntrl(ch)||!!(mask&0x0004)!=!!ispunct(ch)||!!(mask&0x0008)!=!!isalnum(ch))return 8;
        if(R32(LOWER_TABLE+(ch+128)*4)!=(uint32_t)tolower(ch)||R32(UPPER_TABLE+(ch+128)*4)!=(uint32_t)toupper(ch))return 9;
    }
    uint32_t sp=cpu.esp;W32(sp+4,0x40);W32(sp+8,C_LOCALE+128);W32(sp+12,0);
    if(!locale_import(&cpu,"__newlocale")||cpu.eax!=C_LOCALE||cpu.esp!=sp+4)return 10;cpu.esp=sp;
    cpu.top=0;WF32(sp+4,1);if(!math_import(&cpu,"acosf")||cpu.st[cpu.top]!=0||cpu.esp!=sp+4)return 12;cpu.esp=sp;
    cpu.top=0;WF64(sp+4,2);WF64(sp+12,10);if(!math_import(&cpu,"pow")||cpu.st[cpu.top]!=1024||cpu.esp!=sp+4)return 13;cpu.esp=sp;
    uint32_t path=allocate(32),mode=allocate(8),data=allocate(32),readback=allocate(32);
    strcpy((char*)HP(path),"/aot-files-proof.bin");strcpy((char*)HP(mode),"wb+");memcpy(HP(data),"original-abi",12);
    W32(sp+4,path);W32(sp+8,mode);if(!file_import(&cpu,"fopen"))return 14;uint32_t file=cpu.eax;cpu.esp=sp;if(!file)return 15;
    W32(sp+4,data);W32(sp+8,1);W32(sp+12,12);W32(sp+16,file);file_import(&cpu,"fwrite");if(cpu.eax!=12)return 16;cpu.esp=sp;
    W32(sp+4,file);W32(sp+8,0);W32(sp+12,SEEK_SET);file_import(&cpu,"fseek");if(cpu.eax)return 17;cpu.esp=sp;
    W32(sp+4,readback);W32(sp+8,1);W32(sp+12,12);W32(sp+16,file);file_import(&cpu,"fread");if(cpu.eax!=12||memcmp(HP(readback),"original-abi",12))return 18;cpu.esp=sp;
    W32(sp+4,file);file_import(&cpu,"fclose");cpu.esp=sp;remove("/aot-files-proof.bin");
    uint32_t scan_text=allocate(32),scan_format=allocate(32),scan_output=allocate(32);
    strcpy((char*)HP(scan_text),"-12 0x2a 3.5 marker");strcpy((char*)HP(scan_format),"%d %x %f %6s%n");
    W32(sp+4,scan_text);W32(sp+8,scan_format);W32(sp+12,scan_output);W32(sp+16,scan_output+4);W32(sp+20,scan_output+8);W32(sp+24,scan_output+16);W32(sp+28,scan_output+12);
    if(!scan_import(&cpu,"sscanf")||cpu.eax!=4||(int32_t)R32(scan_output)!=-12||R32(scan_output+4)!=42||RF32(scan_output+8)!=3.5f||R32(scan_output+12)!=19||strcmp((char*)HP(scan_output+16),"marker"))return 21;cpu.esp=sp;
    Import empty_copy={0,0,"wmemcpy"};W32(sp+4,0);W32(sp+8,0);W32(sp+12,0);host_import(&cpu,&empty_copy);if(cpu.eax||cpu.esp!=sp+4)return 22;cpu.esp=sp;
    /* Exact cdecl callback/return discipline, through an ORIGINAL constructor's
     * wrapper, is separately exercised by run_startup (until its first blocker). */
    printf("{\"hostMemoryTest\":true,\"allocatorCoalescing\":true,\"scanfGuestPointers\":true,\"zeroLengthNullRanges\":true,\"roundingConversionTest\":true,\"scalarMathABI\":true,\"glibcRandSequence\":true,\"stdioGuestPointerRoundTrip\":true,\"cLocaleCharactersChecked\":256,\"guestWindowBytes\":%u}\n",GUEST_SIZE);return 0;
}
EMSCRIPTEN_KEEPALIVE int test_sdl_abi(void){
    if(setjmp(stop))return 1;reset();uint32_t sp=cpu.esp;
    W32(sp+4,0x21);sdl_import(&cpu,"SDL_Init");cpu.esp=sp;
    W32(sp+4,2);W32(sp+8,1);W32(sp+12,16);W32(sp+16,0);sdl_import(&cpu,"SDL_SetVideoMode");uint32_t surface=cpu.eax;cpu.esp=sp;
    if(R32(surface+8)!=2||R32(surface+12)!=1||R16(surface+16)!=4||R8(R32(surface+4)+4)!=16)return 2;
    W32(sp+4,0);sdl_import(&cpu,"SDL_PollEvent");if(cpu.eax!=1)return 3;cpu.esp=sp;
    uint32_t event=allocate(24);memset(guest_pointer(event,24),0xa5,24);W32(sp+4,event);sdl_import(&cpu,"SDL_PollEvent");cpu.esp=sp;
    if(cpu.eax!=1||R8(event)!=2||R32(event+8)!=275||R32(event+20)!=0xa5a5a5a5u)return 4;
    W32(sp+4,2);W32(sp+8,1);W32(sp+12,0x32595559);W32(sp+16,surface);sdl_import(&cpu,"SDL_CreateYUVOverlay");uint32_t overlay=cpu.eax;cpu.esp=sp;
    if(R32(overlay+12)!=1||R16(R32(overlay+16))!=4)return 5;
    uint32_t pixels=R32(R32(overlay+20));W32(pixels,0x80eb8010u);uint32_t rect=allocate(8);W16(rect+4,2);W16(rect+6,1);
    W32(sp+4,overlay);W32(sp+8,rect);sdl_import(&cpu,"SDL_DisplayYUVOverlay");cpu.esp=sp;
    W32(sp+4,overlay);sdl_import(&cpu,"SDL_FreeYUVOverlay");cpu.esp=sp;W32(sp+4,surface);sdl_import(&cpu,"SDL_FreeSurface");cpu.esp=sp;
    puts("{\"sdlSurfaceABI\":true,\"sdlOverlayABI\":true,\"eventExactlyTwentyBytes\":true,\"pollNullDoesNotConsume\":true,\"browserPresent\":true}");return 0;
}
#include "original_xml.h"
