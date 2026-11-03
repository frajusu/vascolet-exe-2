/* SDL 1.2 guest objects store i386 pointers, never Wasm host pointers.
 * The browser owns presentation/input; original game code owns its pixels. */
static uint32_t sdl_screen,sdl_info,sdl_initialized,sdl_frames;
EM_JS(int,browser_sdl_init,(unsigned flags),{
    return Module.aotHost?.init(flags)?1:0;
});
EM_JS(void,browser_sdl_size,(int w,int h),{Module.aotHost.resize(w,h);});
EM_JS(void,browser_sdl_caption,(const char* text),{Module.aotHost.caption(UTF8ToString(text));});
EM_JS(int,browser_sdl_poll,(unsigned pointer),{const event=Module.aotHost.events[0];if(!event)return 0;if(pointer){Module.aotHost.events.shift();HEAPU8.set(event,pointer);}return 1;});
EM_JS(int,browser_sdl_mouse,(int which),{const m=Module.aotHost.mouse;return which===0?m.x:which===1?m.y:m.buttons;});
EM_JS(int,browser_sdl_cursor,(int mode),{return Module.aotHost.cursor(mode);});
EM_JS(int,browser_sdl_image,(const char* path,int which,unsigned target),{
    let name=UTF8ToString(path);if(name.startsWith('/game/'))name=name.slice(6);
    const image=Module.aotHost.image(name);
    if(!image)return 0;if(which===0)return image.w;if(which===1)return image.h;HEAPU8.set(image.pixels,target);return 1;
});
EM_JS(int,browser_sdl_present,(unsigned pixels,int w,int h,int pitch,int kind,int dw,int dh),{
    Module.aotHost.present(HEAPU8,pixels,w,h,pitch,kind,dw,dh);
    return Module.aotHost.shouldStop()?1:0;
});
static uint32_t sdl_format(unsigned bits){
    uint32_t f=allocate(40);W8(f+4,bits);W8(f+5,bits/8);W8(f+36,255);
    if(bits==16){W8(f+6,3);W8(f+7,2);W8(f+8,3);W8(f+9,8);W8(f+10,11);W8(f+11,5);W32(f+16,0xf800);W32(f+20,0x07e0);W32(f+24,0x001f);}
    else if(bits==32){W8(f+11,8);W8(f+12,16);W8(f+13,24);W32(f+16,0xff);W32(f+20,0xff00);W32(f+24,0xff0000);W32(f+28,0xff000000);}
    else guest_trap(&cpu,0,"unsupported SDL surface pixel depth");
    return f;
}
static uint32_t sdl_surface(unsigned w,unsigned h,unsigned bits){
    if(!w||!h||w>4096||h>4096)guest_trap(&cpu,0,"unsupported SDL surface dimensions");
    uint32_t pitch=(w*(bits/8)+3)&~3u;if(pitch>65535)guest_trap(&cpu,0,"SDL pitch overflow");
    uint32_t s=allocate(60);W32(s+4,sdl_format(bits));W32(s+8,w);W32(s+12,h);W16(s+16,pitch);W32(s+20,allocate(pitch*h));W16(s+36,w);W16(s+38,h);W32(s+56,1);return s;
}
static void sdl_free_surface(uint32_t s){if(!s)return;release(R32(s+20));release(R32(s+4));release(s);if(sdl_screen==s)sdl_screen=0;}
static int sdl_import(CPU*c,const char*name){
    if(!strcmp(name,"SDL_Init")||!strcmp(name,"SDL_InitSubSystem")){
        uint32_t flags=arg(c,0);if(flags&~0x00100031u)guest_trap(c,flags,"unsupported SDL subsystem flags");
        if(!browser_sdl_init(flags))guest_trap(c,0,"SDL_Init needs browser host");sdl_initialized|=flags;returned(c,0);return 1;
    }
    if(!strcmp(name,"SDL_GetVideoInfo")){
        if(!sdl_info){sdl_info=allocate(20);W32(sdl_info,2);W32(sdl_info+8,sdl_format(16));W32(sdl_info+12,800);W32(sdl_info+16,600);}returned(c,sdl_info);return 1;
    }
    if(!strcmp(name,"SDL_SetVideoMode")){
        if(!(sdl_initialized&0x20))guest_trap(c,0,"SDL video not initialized");
        uint32_t w=arg(c,0),h=arg(c,1),bits=arg(c,2);if(!bits)bits=16;if(sdl_screen)sdl_free_surface(sdl_screen);sdl_screen=sdl_surface(w,h,bits);browser_sdl_size(w,h);returned(c,sdl_screen);return 1;
    }
    if(!strcmp(name,"SDL_GetVideoSurface")){returned(c,sdl_screen);return 1;}
    if(!strcmp(name,"SDL_WM_SetCaption")){browser_sdl_caption(guest_text(arg(c,0)));returned(c,0);return 1;}
    if(!strcmp(name,"SDL_ShowCursor")){returned(c,browser_sdl_cursor((int)arg(c,0)));return 1;}
    if(!strcmp(name,"SDL_PollEvent")){uint32_t output=arg(c,0);mixer_drain(c);int available=browser_sdl_poll(output?(uintptr_t)guest_pointer(output,20):0);
#ifdef AOT_ASYNC
        if(!available){emscripten_sleep(0);mixer_drain(c);available=browser_sdl_poll(output?(uintptr_t)guest_pointer(output,20):0);}
#endif
        returned(c,available);return 1;}
    if(!strcmp(name,"SDL_GetMouseState")){if(arg(c,0))W32(arg(c,0),browser_sdl_mouse(0));if(arg(c,1))W32(arg(c,1),browser_sdl_mouse(1));returned(c,browser_sdl_mouse(2));return 1;}
    if(!strcmp(name,"SDL_LockSurface")){uint32_t s=arg(c,0);W32(s+44,R32(s+44)+1);returned(c,0);return 1;}
    if(!strcmp(name,"SDL_UnlockSurface")){uint32_t s=arg(c,0),n=R32(s+44);if(n)W32(s+44,n-1);returned(c,0);return 1;}
    if(!strcmp(name,"SDL_FreeSurface")){sdl_free_surface(arg(c,0));returned(c,0);return 1;}
    if(!strcmp(name,"IMG_Load")){
        const char*path=guest_text(arg(c,0));int w=browser_sdl_image(path,0,0),h=browser_sdl_image(path,1,0);if(w<=0||h<=0)guest_trap(c,arg(c,0),"image not predecoded by browser host");
        uint32_t s=sdl_surface(w,h,32);W32(s,0x10000);browser_sdl_image(path,2,(uintptr_t)guest_pointer(R32(s+20),w*h*4));returned(c,s);return 1;
    }
    if(!strcmp(name,"SDL_CreateYUVOverlay")){
        uint32_t w=arg(c,0),h=arg(c,1),format=arg(c,2);if(format!=0x32595559u||!w||w>4096||!h||h>4096)guest_trap(c,format,"only bounded YUY2 overlays supported");
        uint32_t o=allocate(36),pitches=allocate(2),pixels=allocate(4);W32(o,format);W32(o+4,w);W32(o+8,h);W32(o+12,1);W32(o+16,pitches);W32(o+20,pixels);W16(pitches,w*2);W32(pixels,allocate(w*h*2));W32(o+32,1);returned(c,o);return 1;
    }
    if(!strcmp(name,"SDL_LockYUVOverlay")||!strcmp(name,"SDL_UnlockYUVOverlay")){uint32_t o=arg(c,0);guest_pointer(R32(R32(o+20)),(uint32_t)R16(R32(o+16))*R32(o+8));returned(c,0);return 1;}
    if(!strcmp(name,"SDL_DisplayYUVOverlay")){
        uint32_t o=arg(c,0),rect=arg(c,1),w=R32(o+4),h=R32(o+8),pitch=R16(R32(o+16));int dw=rect?R16(rect+4):w,dh=rect?R16(rect+6):h;
        sdl_frames++;int stop_now=browser_sdl_present((uintptr_t)guest_pointer(R32(R32(o+20)),pitch*h),w,h,pitch,1,dw,dh);if(stop_now)guest_trap(c,0,"browser diagnostic frame limit reached");returned(c,0);return 1;
    }
    if(!strcmp(name,"SDL_FreeYUVOverlay")){uint32_t o=arg(c,0);release(R32(R32(o+20)));release(R32(o+20));release(R32(o+16));release(o);returned(c,0);return 1;}
    if(!strcmp(name,"SDL_Flip")){
        uint32_t s=arg(c,0),w=R32(s+8),h=R32(s+12),pitch=R16(s+16),bits=R8(R32(s+4)+4);sdl_frames++;
        int stop_now=browser_sdl_present((uintptr_t)guest_pointer(R32(s+20),pitch*h),w,h,pitch,bits==16?2:3,w,h);if(stop_now)guest_trap(c,0,"browser diagnostic frame limit reached");returned(c,0);return 1;
    }
    return 0;
}
