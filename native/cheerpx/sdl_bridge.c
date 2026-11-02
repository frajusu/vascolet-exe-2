/* Browser-facing SDL 1.2 video/input/audio adapter. MIT license.
 * Native SDL still implements image surfaces and software drawing. Only the
 * unavailable Linux display/audio devices are replaced by browser messages.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <termios.h>
#include <poll.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <link.h>
#include <sys/mman.h>
#include <errno.h>
#if __has_include("runtime_metadata.h")
#include "runtime_metadata.h"
static void configure_frame_limit(void){void*app=*(void**)(uintptr_t)ENGINE_INSTANCE;if(!app)return;unsigned(*volatile get)(void*)=(void*)(uintptr_t)ENGINE_GETLIMIT;void(*volatile set)(void*,unsigned)=(void*)(uintptr_t)ENGINE_SETLIMIT;unsigned limit=get(app);if(limit!=60){fprintf(stderr,"BROWSER_FRAME_LIMIT %u -> 60\n",limit);set(app,60);}}
static void color_jump(unsigned address,void*replacement,const unsigned char*expected){unsigned char*p=(void*)(uintptr_t)address;if(memcmp(p,expected,16)){fprintf(stderr,"BROWSER_COLOR unexpected function at %x\n",address);_exit(92);}uintptr_t page=address&~4095u;size_t size=((address+5+4095)&~4095u)-page;if(mprotect((void*)page,size,7))_exit(92);p[0]=0xe9;int32_t offset=(uintptr_t)replacement-address-5;memcpy(p+1,&offset,4);__builtin___clear_cache((char*)p,(char*)p+5);mprotect((void*)page,size,5);}
static void install_color_acceleration(void){static int checked;if(checked)return;checked=1;uint32_t(*volatile old_yuv)(unsigned char,unsigned char,unsigned char)=(void*)(uintptr_t)COLOR_FORWARD;uint32_t(*volatile old_rgb)(uint32_t)=(void*)(uintptr_t)COLOR_INVERSE;uint32_t random=0x12345678;for(unsigned n=0;n<4096;n++){random=random*1664525+1013904223;unsigned char r=random,g=random>>8,b=random>>16;if(old_yuv(r,g,b)!=fast_get_yuv(r,g,b)||old_rgb(random)!=fast_get_rgb(random)){fprintf(stderr,"BROWSER_COLOR validation failed at %u; using original converters\n",n);return;}}color_jump(COLOR_FORWARD,fast_get_yuv,color_forward_prefix);color_jump(COLOR_INVERSE,fast_get_rgb,color_inverse_prefix);fprintf(stderr,"BROWSER_COLOR exact integer converters verified on 8192 samples\n");}
#else
static void configure_frame_limit(void){}
static void install_color_acceleration(void){}
#endif
#if __has_include("compat_patches.h")
#include "compat_patches.h"
static int patch_module(struct dl_phdr_info*info,size_t size,void*arg){const char*name=strrchr(info->dlpi_name,'/');name=name?name+1:info->dlpi_name;unsigned changed=0;for(unsigned n=0;n<COMPAT_PATCH_COUNT;n++){const CompatPatch*p=&compat_patches[n];int main_module=!p->module[0]&&(!name[0]||!strcmp(name,COMPAT_MAIN_NAME));if(!main_module&&strcmp(name,p->module))continue;unsigned char*address=(unsigned char*)(uintptr_t)(info->dlpi_addr+p->address);if(!memcmp(address,p->replacement,p->length))continue;if(memcmp(address,p->original,p->length)){fprintf(stderr,"BROWSER_COMPAT unexpected instruction in %s at %x\n",name,p->address);_exit(92);}uintptr_t page=(uintptr_t)address&~4095u;size_t length=(((uintptr_t)address+p->length+4095)&~4095u)-page;if(mprotect((void*)page,length,PROT_READ|PROT_WRITE|PROT_EXEC)){fprintf(stderr,"BROWSER_COMPAT mprotect failed %d\n",errno);_exit(92);}for(unsigned i=0;i<p->length;i++)((volatile unsigned char*)address)[i]=p->replacement[i];__builtin___clear_cache((char*)address,(char*)address+p->length);mprotect((void*)page,length,PROT_READ|PROT_EXEC);changed++;}if(changed)fprintf(stderr,"BROWSER_COMPAT %s: %u equivalent cached stores\n",name[0]?name:COMPAT_MAIN_NAME,changed);return 0;}
static void apply_compat(void){dl_iterate_phdr(patch_module,0);}
#else
static void apply_compat(void){}
#endif
#include "math_bridge.h"
typedef struct {int16_t x,y;uint16_t w,h;} Rect;
typedef struct {void*palette;uint8_t bits,bytes,rloss,gloss,bloss,aloss,rshift,gshift,bshift,ashift;uint32_t rmask,gmask,bmask,amask,colorkey;uint8_t alpha;} Format;
typedef struct Surface {uint32_t flags;Format*format;int w,h;uint16_t pitch;void*pixels;int offset;void*hwdata;Rect clip;uint32_t unused,locked;void*map;uint32_t version;int refcount;} Surface;
typedef struct {uint32_t flags,memory;Format*format;int w,h;} VideoInfo;
static Surface*screen;
static Surface**uploaded;
static int uploaded_count,uploaded_capacity,gpu_frame;
static void*native;
static void*symbol(const char*name);
static void install_renderer_acceleration(void);
static int native_gpu_frame;
static unsigned render_width,render_height;
static int trace_enabled=-1;
static int trace(void){if(trace_enabled<0)trace_enabled=getenv("BROWSER_SDL_TRACE")!=0;return trace_enabled;}
uint32_t SDL_GetTicks(void){static uint64_t epoch;static int initialized;struct timespec stamp;uint64_t now;if(clock_gettime(CLOCK_MONOTONIC,&stamp)==0)now=(uint64_t)stamp.tv_sec*1000+stamp.tv_nsec/1000000;else{struct timeval fallback;gettimeofday(&fallback,0);now=(uint64_t)fallback.tv_sec*1000+fallback.tv_usec/1000;}if(!initialized){epoch=now;initialized=1;}uint32_t value=(uint32_t)(now-epoch);if(trace()){static uint32_t logged=~0u;static unsigned calls;calls++;if(value/1000!=logged){fprintf(stderr,"BROWSER_TICK %u calls=%u\n",value,calls);logged=value/1000;}}return value;}
static uint8_t commands[131072];
static uint32_t command_size;
static void*symbol(const char*name){if(!native)native=dlopen("libNAT-1.2.so.0",RTLD_NOW|RTLD_LOCAL);void*f=dlsym(native,name);if(!f){fprintf(stderr,"SDL bridge missing %s: %s\n",name,dlerror());_exit(90);}return f;}
static void write_packet(const void*data,size_t size){const uint8_t*bytes=data;size_t at=0;while(at<size){ssize_t count=write(1,bytes+at,size-at);if(count>0){at+=count;continue;}if(count<0&&errno==EINTR)continue;if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){struct pollfd output={1,POLLOUT,0};poll(&output,1,100);continue;}_exit(91);}}
static void flush_commands(void){if(command_size){write_packet(commands,command_size);command_size=0;}}
static void emit(uint32_t kind,const void*data,uint32_t size){uint32_t header[]={0x4c445356,kind,size};if(size+12>sizeof(commands)){flush_commands();uint8_t*message=malloc(size+12);memcpy(message,header,12);if(size)memcpy(message+12,data,size);write_packet(message,size+12);free(message);return;}if(command_size+size+12>sizeof(commands))flush_commands();memcpy(commands+command_size,header,12);if(size)memcpy(commands+command_size+12,data,size);command_size+=size+12;if(kind==2||kind==13||kind==22)flush_commands();}
static void named(uint32_t kind,uint32_t id,const char*name){size_t len=strlen(name)+1;char*data=malloc(len+4);memcpy(data,&id,4);memcpy(data+4,name,len);emit(kind,data,len+4);free(data);}
static void invalidate(Surface*s){for(int n=0;n<uploaded_count;n++)if(uploaded[n]==s){uploaded[n]=uploaded[--uploaded_count];uint32_t id=(uintptr_t)s;emit(8,&id,4);break;}}
static void upload(Surface*s){for(int n=0;n<uploaded_count;n++)if(uploaded[n]==s)return;int(*lock)(Surface*)=symbol("SDL_LockSurface");void(*unlock)(Surface*)=symbol("SDL_UnlockSurface");if(lock(s)!=0)return;if(!s->pixels){unlock(s);return;}if(uploaded_count==uploaded_capacity){int capacity=uploaded_capacity?uploaded_capacity*2:64;Surface**next=realloc(uploaded,capacity*sizeof(*next));if(next){uploaded=next;uploaded_capacity=capacity;}}if(uploaded_count<uploaded_capacity)uploaded[uploaded_count++]=s;uint32_t meta[]={(uint32_t)(uintptr_t)s,(uint32_t)s->w,(uint32_t)s->h,s->pitch,s->format->bytes,s->format->rmask,s->format->gmask,s->format->bmask,s->format->amask,s->flags,s->format->colorkey};uint8_t*data=malloc(sizeof(meta)+s->pitch*s->h);memcpy(data,meta,sizeof(meta));memcpy(data+sizeof(meta),s->pixels,s->pitch*s->h);emit(5,data,sizeof(meta)+s->pitch*s->h);free(data);unlock(s);}
int SDL_UpperBlit(Surface*src,Rect*sr,Surface*dest,Rect*dr){if(!src||!dest)return-1;if(dest!=screen){int(*real)(Surface*,Rect*,Surface*,Rect*)=symbol("SDL_UpperBlit");int result=real(src,sr,dest,dr);invalidate(dest);return result;}int sx=sr?sr->x:0,sy=sr?sr->y:0,w=sr?sr->w:src->w,h=sr?sr->h:src->h,dx=dr?dr->x:0,dy=dr?dr->y:0;if(sx<0){dx-=sx;w+=sx;sx=0;}if(sy<0){dy-=sy;h+=sy;sy=0;}if(w>src->w-sx)w=src->w-sx;if(h>src->h-sy)h=src->h-sy;int cut=dest->clip.x-dx;if(cut>0){sx+=cut;dx+=cut;w-=cut;}cut=dest->clip.y-dy;if(cut>0){sy+=cut;dy+=cut;h-=cut;}cut=dx+w-dest->clip.x-dest->clip.w;if(cut>0)w-=cut;cut=dy+h-dest->clip.y-dest->clip.h;if(cut>0)h-=cut;if(w<0)w=0;if(h<0)h=0;if(dr){dr->x=dx;dr->y=dy;dr->w=w;dr->h=h;}if(!w||!h)return 0;upload(src);int32_t data[]={(uint32_t)(uintptr_t)src,sx,sy,w,h,dx,dy,(src->flags&0x10000)&&!src->format->amask?src->format->alpha:255};emit(6,data,sizeof(data));gpu_frame=1;return 0;}
int SDL_FillRect(Surface*dest,Rect*rect,uint32_t color){if(dest!=screen){int(*real)(Surface*,Rect*,uint32_t)=symbol("SDL_FillRect");int result=real(dest,rect,color);invalidate(dest);return result;}uint32_t data[]={rect?rect->x:0,rect?rect->y:0,rect?rect->w:dest->w,rect?rect->h:dest->h,color,dest->format->bits,dest->w,dest->h};emit(7,data,sizeof(data));gpu_frame=1;return 0;}
void SDL_FreeSurface(Surface*s){invalidate(s);void(*real)(Surface*)=symbol("SDL_FreeSurface");real(s);}
int SDL_SetColorKey(Surface*s,uint32_t flags,uint32_t key){uint32_t before=s?s->flags:0,old=s?s->format->colorkey:0;int(*real)(Surface*,uint32_t,uint32_t)=symbol("SDL_SetColorKey");int result=real(s,flags,key);if(s&&((before^s->flags)&0x1000||old!=s->format->colorkey))invalidate(s);return result;}
int SDL_SetAlpha(Surface*s,uint32_t flags,uint8_t alpha){uint32_t before=s?s->flags:0;int(*real)(Surface*,uint32_t,uint8_t)=symbol("SDL_SetAlpha");int result=real(s,flags,alpha);if(s&&((before^s->flags)&0x10000))invalidate(s);return result;}
void SDL_UnlockSurface(Surface*s){void(*real)(Surface*)=symbol("SDL_UnlockSurface");real(s);invalidate(s);}
static uint8_t keys[323],oldkeys[323],mousebuttons,oldbuttons;
static uint16_t mx,my,oldmx,oldmy;
/* SDL_Event is 20 bytes on i386 (SDL_KeyboardEvent is its largest member).
 * Copying the 24-byte host-sized approximation overwrites callers' locals. */
static uint8_t events[256][20];
static unsigned event_read,event_write;
static void channel_ended(unsigned channel);
static void export_saves(void);
static uint8_t*new_event(void){if(event_write-event_read>=256)event_read++;uint8_t*e=events[event_write++%256];memset(e,0,sizeof(events[0]));return e;}
static void key_event(unsigned code,unsigned down){if(code>=323||keys[code]==down)return;keys[code]=down;uint8_t*e=new_event();e[0]=down?2:3;e[2]=down;memcpy(e+8,&code,4);uint16_t unicode=code<128?code:0;memcpy(e+16,&unicode,2);}
static void mouse_event(unsigned x,unsigned y,unsigned buttons){if(x!=mx||y!=my){uint8_t*e=new_event();e[0]=4;e[2]=buttons;uint16_t xx=x,yy=y;int16_t dx=x-mx,dy=y-my;memcpy(e+4,&xx,2);memcpy(e+6,&yy,2);memcpy(e+8,&dx,2);memcpy(e+10,&dy,2);}if(buttons!=mousebuttons){uint8_t*e=new_event();e[0]=buttons?5:6;e[2]=1;e[3]=buttons?1:0;uint16_t xx=x,yy=y;memcpy(e+4,&xx,2);memcpy(e+6,&yy,2);}mx=x;my=y;mousebuttons=buttons;}
static void input(void){static int initialized,at;static char line[96];if(!initialized){initialized=1;struct termios settings;if(tcgetattr(0,&settings)==0){cfmakeraw(&settings);tcsetattr(0,TCSANOW,&settings);}int flags=fcntl(0,F_GETFL);if(flags>=0)fcntl(0,F_SETFL,flags|O_NONBLOCK);}struct pollfd descriptor={0,POLLIN,0};while(poll(&descriptor,1,0)>0){char byte;if(read(0,&byte,1)!=1)break;if(byte=='\n'||byte=='\r'){line[at]=0;unsigned a,b,c;if(sscanf(line,"K:%u:%u",&a,&b)==2)key_event(a,b);else if(sscanf(line,"M:%u:%u:%u",&a,&b,&c)==3)mouse_event(a,b,c);else if(sscanf(line,"A:%u:%u",&a,&b)==2&&!b)channel_ended(a);else if(!strcmp(line,"E"))export_saves();at=0;}else if(at<95)line[at++]=byte;else at=0;}}
int SDL_Init(uint32_t flags){apply_compat();install_color_acceleration();install_math_acceleration();install_renderer_acceleration();int(*init)(uint32_t)=symbol("SDL_Init"),(*sub)(uint32_t)=symbol("SDL_InitSubSystem");int result=init(0x100000);if(!result)result=sub(flags&0x21);input();return result;}
void*SDL_LoadObject(const char*path){void*(*real)(const char*)=symbol("SDL_LoadObject");void*handle=real(path);apply_compat();return handle;}
int SDL_InitSubSystem(uint32_t flags){int(*real)(uint32_t)=symbol("SDL_InitSubSystem");return real(flags&0x21);}
void SDL_QuitSubSystem(uint32_t flags){}
Surface*SDL_SetVideoMode(int w,int h,int bpp,uint32_t flags){Surface*(*real)(int,int,int,uint32_t)=symbol("SDL_SetVideoMode");screen=real(w,h,bpp,0);if(screen){int size[]={screen->w,screen->h};emit(18,size,8);}return screen;}
Surface*SDL_GetVideoSurface(void){return screen;}
const VideoInfo*SDL_GetVideoInfo(void){static VideoInfo info={0};if(!info.format){Surface*(*create)(uint32_t,int,int,int,uint32_t,uint32_t,uint32_t,uint32_t)=symbol("SDL_CreateRGBSurface");Surface*s=create(0,1,1,16,0xf800,0x07e0,0x001f,0);info.format=s->format;info.w=800;info.h=600;info.flags=2;}return &info;}
int SDL_VideoModeOK(int w,int h,int bpp,uint32_t flags){return bpp?bpp:16;}
Surface*SDL_DisplayFormat(Surface*s){if(!s)return 0;Surface*(*convert)(Surface*,Format*,uint32_t)=symbol("SDL_ConvertSurface");return convert(s,screen?screen->format:s->format,0);}
int SDL_Flip(Surface*s){if(!s)s=screen;if(!s||!s->pixels)return 0;if(gpu_frame){uint32_t size[]={s->w,s->h};emit(2,size,sizeof(size));return 0;}uint32_t meta[]={(uint32_t)s->w,(uint32_t)s->h,s->pitch,s->format->bytes,s->format->rmask,s->format->gmask,s->format->bmask,s->format->amask};uint32_t size=sizeof(meta)+s->pitch*s->h;uint32_t head[]={0x4c445356,1,size};flush_commands();write_packet(head,sizeof(head));write_packet(meta,sizeof(meta));write_packet(s->pixels,s->pitch*s->h);return 0;}
void SDL_UpdateRects(Surface*s,int n,Rect*r){SDL_Flip(s);}
int SDL_PollEvent(void*event){configure_frame_limit();input();if(event_read==event_write)return 0;if(event){memcpy(event,events[event_read++%256],sizeof(events[0]));if(trace())fprintf(stderr,"BROWSER_EVENT %u caller=%p\n",*(unsigned char*)event,__builtin_return_address(0));}return 1;}
uint8_t SDL_GetMouseState(int*x,int*y){input();if(x)*x=mx;if(y)*y=my;return mousebuttons;}
int SDL_EnableUNICODE(int enable){return 1;}
int SDL_ShowCursor(int toggle){return 1;}
void SDL_WarpMouse(uint16_t x,uint16_t y){mx=x;my=y;}
void SDL_WM_SetCaption(const char*title,const char*icon){}
int SDL_GL_SetAttribute(int attribute,int value){return 0;}
void SDL_GL_SwapBuffers(void){SDL_Flip(screen);}
typedef struct {uint32_t format;int w,h,planes;uint16_t*pitches;uint8_t**pixels;void*functions;void*data;uint32_t flags;} Overlay;
Overlay*SDL_CreateYUVOverlay(int w,int h,uint32_t format,Surface*display){if(w<1||h<1||w>8192||h>8192)return 0;Overlay*o=calloc(1,sizeof(*o));o->format=format;o->w=w;o->h=h;o->flags=1;o->planes=(format==0x32315659||format==0x56555949)?3:1;o->pitches=calloc(o->planes,sizeof(uint16_t));o->pixels=calloc(o->planes,sizeof(uint8_t*));o->pitches[0]=o->planes==1?w*2:w;o->pixels[0]=calloc(o->pitches[0],h);if(o->planes==3)for(int n=1;n<3;n++){o->pitches[n]=(w+1)/2;o->pixels[n]=malloc(o->pitches[n]*((h+1)/2));memset(o->pixels[n],128,o->pitches[n]*((h+1)/2));}fprintf(stderr,"BROWSER_YUV %d %d %08x planes=%d\n",w,h,format,o->planes);return o;}
int SDL_LockYUVOverlay(Overlay*o){if(o){render_width=o->w/2;render_height=o->h;if(native_gpu_frame){uint32_t dimensions[]={render_width,render_height};emit(36,dimensions,sizeof(dimensions));}}return o?0:-1;}
void SDL_UnlockYUVOverlay(Overlay*o){}
#include "renderer_bridge.h"
int SDL_DisplayYUVOverlay(Overlay*o,Rect*dest){if(!o)return-1;if(native_gpu_frame){uint32_t dimensions[]={render_width,render_height};emit(34,dimensions,sizeof(dimensions));flush_commands();return 0;}uint32_t began=trace()?SDL_GetTicks():0;uint32_t meta[]={o->w,o->h,o->format,o->planes,o->pitches[0],o->planes>1?o->pitches[1]:0,o->planes>2?o->pitches[2]:0,dest?dest->x:0,dest?dest->y:0,dest?dest->w:o->w,dest?dest->h:o->h};uint32_t size=sizeof(meta)+o->pitches[0]*o->h;if(o->planes>1)size+=(o->pitches[1]+o->pitches[2])*((o->h+1)/2);uint8_t*data=malloc(size);memcpy(data,meta,sizeof(meta));uint32_t at=sizeof(meta);for(int n=0;n<o->planes;n++){uint32_t bytes=o->pitches[n]*(n?(o->h+1)/2:o->h);memcpy(data+at,o->pixels[n],bytes);at+=bytes;}emit(9,data,size);flush_commands();free(data);if(trace()){static uint32_t last,logged;uint32_t end=SDL_GetTicks();if(end-logged>=1000){fprintf(stderr,"BROWSER_FRAME draw=%u output=%u bytes=%u\n",last?began-last:0,end-began,size);logged=end;}last=end;}return 0;}
void SDL_FreeYUVOverlay(Overlay*o){if(!o)return;for(int n=0;n<o->planes;n++)free(o->pixels[n]);free(o->pixels);free(o->pitches);free(o);}
typedef struct {void*seek;void*read;void*write;int(*close)(void*);uint32_t type;int autoclose;FILE*file;} FileOps;
typedef struct {void*rw;char*name;char*buffer;int(*close)(void*);} FileRecord;
static FileRecord records[1024];
static int close_file(void*rw){for(int n=0;n<1024;n++)if(records[n].rw==rw){int(*original)(void*)=records[n].close;char*buffer=records[n].buffer;records[n].buffer=0;records[n].close=0;int status=original?original(rw):0;free(buffer);return status;}return-1;}
void*SDL_RWFromFile(const char*file,const char*mode){if(trace())fprintf(stderr,"BROWSER_OPEN %s\n",file);void*(*f)(const char*,const char*)=symbol("SDL_RWFromFile");void*rw=f(file,mode);if(trace())fprintf(stderr,"BROWSER_OPENED %s %p\n",file,rw);if(!rw)return 0;for(int n=0;n<1024;n++)if(!records[n].rw||records[n].rw==rw){records[n].rw=rw;free(records[n].name);records[n].name=strdup(file);FileOps*ops=rw;records[n].close=ops->close;records[n].buffer=malloc(262144);if(records[n].buffer&&ops->file){if(setvbuf(ops->file,records[n].buffer,_IOFBF,262144)){free(records[n].buffer);records[n].buffer=0;}}ops->close=close_file;break;}return rw;}
typedef struct {int allocated;uint8_t*buffer;uint32_t length;uint8_t volume;uint32_t id;} Chunk;
static uint32_t next_id=1;
static Chunk*channels[64];
static int volumes[64],paused[64],music_volume=128,music_playing,music_paused;
static uint32_t music_id;
static void channel_ended(unsigned channel){if(channel<64)channels[channel]=0;else if(channel==65535)music_playing=0;}
int Mix_OpenAudio(int frequency,uint16_t format,int channels_,int chunksize){for(int n=0;n<64;n++)volumes[n]=128;return 0;}
void Mix_CloseAudio(void){emit(13,0,0);}
int Mix_AllocateChannels(int count){return count<0?32:count;}
void*Mix_LoadMUS(const char*name){uint32_t*music=malloc(4);*music=next_id++;named(10,*music,name);return music;}
Chunk*Mix_LoadWAV_RW(void*rw,int freesrc){const char*name=0;for(int n=0;n<1024;n++)if(records[n].rw==rw){name=records[n].name;break;}if(!name){fprintf(stderr,"BROWSER_AUDIO_MISSING_RW %p\n",rw);return 0;}Chunk*c=calloc(1,sizeof(*c));c->id=next_id++;c->volume=128;named(10,c->id,name);if(freesrc&&rw){int(*close)(void*)=((int(**)(void*))rw)[3];close(rw);}return c;}
int Mix_PlayMusic(void*m,int loops){if(!m)return-1;music_id=*(uint32_t*)m;music_playing=1;music_paused=0;int32_t data[]={music_id,-2,loops,music_volume};emit(11,data,sizeof(data));return 0;}
int Mix_PlayChannelTimed(int channel,Chunk*c,int loops,int ticks){if(!c)return-1;if(channel<0){for(channel=0;channel<64&&channels[channel];channel++){}if(channel==64)channel=0;}if(channel>=64)return-1;channels[channel]=c;int32_t data[]={c->id,channel,loops,volumes[channel]};emit(11,data,sizeof(data));return channel;}
int Mix_HaltMusic(void){music_playing=0;int32_t channel=-2;emit(12,&channel,4);return 0;}
int Mix_HaltChannel(int channel){if(channel<0){memset(channels,0,sizeof(channels));}else if(channel<64)channels[channel]=0;emit(12,&channel,4);return 0;}
int Mix_Volume(int channel,int volume){int old=channel>=0&&channel<64?volumes[channel]:128;if(volume>=0){if(channel<0)for(int n=0;n<64;n++)volumes[n]=volume;else if(channel<64)volumes[channel]=volume;int data[]={channel,volume};emit(15,data,8);}return old;}
int Mix_VolumeMusic(int volume){int old=music_volume;if(volume>=0){music_volume=volume;int data[]={-2,volume};emit(15,data,8);}return old;}
int Mix_PlayingMusic(void){return music_playing;}
int Mix_Playing(int channel){return channel>=0&&channel<64&&channels[channel]?1:0;}
int Mix_Paused(int channel){if(channel>=0&&channel<64)return paused[channel];int count=0;for(int n=0;n<64;n++)count+=paused[n];return count;}
void Mix_Pause(int channel){if(channel<0)for(int n=0;n<64;n++)paused[n]=1;else if(channel<64)paused[channel]=1;int data[]={channel,1};emit(14,data,8);}
void Mix_Resume(int channel){if(channel<0)memset(paused,0,sizeof(paused));else if(channel<64)paused[channel]=0;int data[]={channel,0};emit(14,data,8);}
void Mix_PauseMusic(void){music_paused=1;int data[]={-2,1};emit(14,data,8);}
void Mix_ResumeMusic(void){music_paused=0;int data[]={-2,0};emit(14,data,8);}
void Mix_FreeChunk(Chunk*c){free(c);}
void Mix_FreeMusic(void*m){free(m);}
Chunk*Mix_GetChunk(int channel){return channel>=0&&channel<64?channels[channel]:0;}
void Mix_ChannelFinished(void*callback){}
void Mix_HookMusicFinished(void*callback){}
static size_t export_bytes;
static int export_directory(const char*path){DIR*dir=opendir(path);if(!dir)return 0;struct dirent*entry;while((entry=readdir(dir))){if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;char full[1024];snprintf(full,sizeof(full),"%s/%s",path,entry->d_name);struct stat info;if(lstat(full,&info))continue;if(S_ISDIR(info.st_mode)){int result=export_directory(full);if(result){closedir(dir);return result;}}else if(S_ISREG(info.st_mode)){export_bytes+=info.st_size;if(export_bytes>32*1024*1024){closedir(dir);return 9;}const char*relative=full+6;uint32_t length=strlen(relative);uint8_t*data=malloc(4+length+info.st_size);memcpy(data,&length,4);memcpy(data+4,relative,length);FILE*file=fopen(full,"rb");if(!file){free(data);continue;}size_t size=fread(data+4+length,1,info.st_size,file);fclose(file);emit(21,data,4+length+size);free(data);}}closedir(dir);return 0;}
static void export_saves(void){export_bytes=0;int status=export_directory("/home");emit(22,&status,4);}
