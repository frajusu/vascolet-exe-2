/* SDL_mixer control ABI -> real Web Audio buffers/sources. PCM pointers are
 * explicitly opaque; direct guest access to raw sample buffers is unsupported. */
static struct {uint32_t address;char path[512];} audio_rw[128],audio_chunks[256];
static uint32_t audio_callback,audio_frequency,audio_channels,audio_open;
EM_JS(int,browser_audio_open,(int frequency,int format,int channels,int chunk),{return Module.aotHost.openAudio(frequency,format,channels,chunk)?1:0;});
EM_JS(int,browser_audio_allocate,(int count),{return Module.aotHost.allocateChannels(count);});
EM_JS(double,browser_audio_duration,(const char* path),{let name=UTF8ToString(path);if(name.startsWith('/game/'))name=name.slice(6);return Module.aotHost.soundDuration(name);});
EM_JS(int,browser_audio_play,(int channel,const char* path,int loops,int ticks,int volume,unsigned chunk),{let name=UTF8ToString(path);if(name.startsWith('/game/'))name=name.slice(6);return Module.aotHost.play(channel,name,loops,ticks,volume,chunk);});
EM_JS(int,browser_audio_halt,(int channel),{return Module.aotHost.halt(channel);});
EM_JS(void,browser_audio_free,(unsigned chunk),{Module.aotHost.freeSound(chunk);});
EM_JS(void,browser_audio_pause,(int channel,int paused),{Module.aotHost.pause(channel,!!paused);});
EM_JS(int,browser_audio_volume,(int channel,int volume),{return Module.aotHost.volume(channel,volume);});
EM_JS(int,browser_audio_playing,(int channel,int paused),{return Module.aotHost.playing(channel,!!paused);});
EM_JS(int,browser_audio_finished,(void),{return Module.aotHost.finished.length?Module.aotHost.finished.shift():-1;});
EM_JS(void,browser_audio_reset,(void),{if(Module.aotHost){Module.aotHost.halt(-1);Module.aotHost.finished.length=0;}});
static void mixer_drain(CPU*c){
    if(!audio_open)return;unsigned budget=512;int channel;
    while((channel=browser_audio_finished())>=0){if(!budget--)guest_trap(c,0,"audio callback budget exceeded");if(audio_callback){uint32_t sp=c->esp;PUSH32(c,channel);invoke(audio_callback);c->esp=sp;}}
}
static void reset_mixer(void){browser_audio_reset();memset(audio_rw,0,sizeof audio_rw);memset(audio_chunks,0,sizeof audio_chunks);audio_callback=audio_frequency=audio_channels=audio_open=0;}
static int mixer_import(CPU*c,const char*name){
    if(!strcmp(name,"Mix_OpenAudio")){
        if(!browser_audio_open(arg(c,0),arg(c,1),arg(c,2),arg(c,3))){returned(c,-1);return 1;}audio_frequency=arg(c,0);audio_channels=arg(c,2);audio_open++;returned(c,0);return 1;
    }
    if(!strcmp(name,"Mix_CloseAudio")){if(audio_open&&!--audio_open)browser_audio_halt(-1);returned(c,0);return 1;}
    if(!strcmp(name,"Mix_AllocateChannels")){returned(c,browser_audio_allocate((int)arg(c,0)));return 1;}
    if(!strcmp(name,"Mix_ChannelFinished")){audio_callback=arg(c,0);returned(c,0);return 1;}
    if(!strcmp(name,"SDL_RWFromFile")){
        const char*path=guest_text(arg(c,0)),*mode=guest_text(arg(c,1));if(strcmp(mode,"r")&&strcmp(mode,"rb"))guest_trap(c,arg(c,1),"audio RW only supports read mode");if(strlen(path)>511)guest_trap(c,arg(c,0),"audio path too long");
        FILE*f=fopen(path,"rb");if(!f){returned(c,0);return 1;}fclose(f);
        for(unsigned i=0;i<128;i++)if(!audio_rw[i].address){audio_rw[i].address=allocate(64);strcpy(audio_rw[i].path,path);returned(c,audio_rw[i].address);return 1;}guest_trap(c,0,"audio RW registry exhausted");
    }
    if(!strcmp(name,"Mix_LoadWAV_RW")){
        if(!arg(c,0)){returned(c,0);return 1;}
        unsigned rw=0;while(rw<128&&audio_rw[rw].address!=arg(c,0))rw++;if(rw==128)guest_trap(c,arg(c,0),"unknown mixer RW handle");
        double duration=browser_audio_duration(audio_rw[rw].path);if(!duration){snprintf(last_message,sizeof(last_message),"%s",audio_rw[rw].path);guest_trap(c,arg(c,0),"audio not predecoded by browser host");}
        for(unsigned i=0;i<256;i++)if(!audio_chunks[i].address){uint32_t chunk=allocate(16);audio_chunks[i].address=chunk;strcpy(audio_chunks[i].path,audio_rw[rw].path);W32(chunk,0);W32(chunk+4,0x70200000u+i*16);W32(chunk+8,(uint32_t)(duration*audio_frequency+.5)*audio_channels*2);W8(chunk+12,128);if(arg(c,1)){release(audio_rw[rw].address);audio_rw[rw].address=0;}returned(c,chunk);return 1;}guest_trap(c,0,"audio chunk registry exhausted");
    }
    if(!strcmp(name,"Mix_FreeChunk")){uint32_t chunk=arg(c,0);if(chunk){unsigned i=0;while(i<256&&audio_chunks[i].address!=chunk)i++;if(i==256)guest_trap(c,chunk,"unknown mixer chunk");browser_audio_free(chunk);audio_chunks[i].address=0;release(chunk);}returned(c,0);return 1;}
    if(!strcmp(name,"Mix_PlayChannelTimed")){
        if(!arg(c,1)){returned(c,-1);return 1;}
        uint32_t chunk=arg(c,1);unsigned i=0;while(i<256&&audio_chunks[i].address!=chunk)i++;if(i==256)guest_trap(c,chunk,"unknown mixer play chunk");if(!audio_open){returned(c,-1);return 1;}
        returned(c,browser_audio_play((int)arg(c,0),audio_chunks[i].path,(int)arg(c,2),(int)arg(c,3),R8(chunk+12),chunk));return 1;
    }
    if(!strcmp(name,"Mix_Volume")){returned(c,browser_audio_volume((int)arg(c,0),(int)arg(c,1)));return 1;}
    if(!strcmp(name,"Mix_HaltChannel")){uint32_t result=browser_audio_halt((int)arg(c,0));mixer_drain(c);returned(c,result);return 1;}
    if(!strcmp(name,"Mix_Pause")||!strcmp(name,"Mix_Resume")){browser_audio_pause((int)arg(c,0),!strcmp(name,"Mix_Pause"));returned(c,0);return 1;}
    if(!strcmp(name,"Mix_Playing")||!strcmp(name,"Mix_Paused")){returned(c,browser_audio_playing((int)arg(c,0),!strcmp(name,"Mix_Paused")));return 1;}
    return 0;
}
