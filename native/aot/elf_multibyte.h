/* glibc i386 wchar_t=32 bits, mbstate_t=8 bytes; strict C/POSIX encoding.
 * Every valid C-locale character consumes exactly one byte. Invalid sequences
 * return (size_t)-1/EILSEQ, not UTF-8 guessing or silent replacement.
 */
static int multibyte_state(uint32_t state){
    if(!state||R32(state)==0)return 1;
    W32(cpu.fs_base+64,22);return 0;
}
static uint32_t multibyte_failure(void){W32(cpu.fs_base+64,84);return UINT32_MAX;}
static uint32_t guest_wcsnrtombs(uint32_t dst,uint32_t source,uint32_t nwc,uint32_t len,uint32_t state){
    if(!multibyte_state(state))return UINT32_MAX;
    uint32_t input=R32(source),count=0;
    while(count<nwc&&(!dst||count<len)){
        uint32_t value=R32(input+count*4);
        if(value>=128){if(dst)W32(source,input+count*4);return multibyte_failure();}
        if(!value){if(dst){W8(dst+count,0);W32(source,0);}return count;}
        if(dst)W8(dst+count,value);count++;
    }
    if(dst)W32(source,input+count*4);return count;
}
static uint32_t guest_mbsnrtowcs(uint32_t dst,uint32_t source,uint32_t nms,uint32_t len,uint32_t state){
    if(!multibyte_state(state))return UINT32_MAX;
    uint32_t input=R32(source),count=0;
    while(count<nms&&(!dst||count<len)){
        uint32_t value=R8(input+count);
        if(value>=128){if(dst)W32(source,input+count);return multibyte_failure();}
        if(!value){if(dst){W32(dst+count*4,0);W32(source,0);}return count;}
        if(dst)W32(dst+count*4,value);count++;
    }
    if(dst)W32(source,input+count);return count;
}
static int multibyte_import(CPU*c,const char*name){
    if(!strcmp(name,"mbsinit")){uint32_t state=arg(c,0);returned(c,!state||R32(state)==0);return 1;}
    if(!strcmp(name,"wcsnrtombs")||!strcmp(name,"wcsrtombs")){
        int limited=!strcmp(name,"wcsnrtombs");
        returned(c,guest_wcsnrtombs(arg(c,0),arg(c,1),limited?arg(c,2):UINT32_MAX,arg(c,limited?3:2),arg(c,limited?4:3)));return 1;
    }
    if(!strcmp(name,"mbsnrtowcs")||!strcmp(name,"mbsrtowcs")){
        int limited=!strcmp(name,"mbsnrtowcs");
        returned(c,guest_mbsnrtowcs(arg(c,0),arg(c,1),limited?arg(c,2):UINT32_MAX,arg(c,limited?3:2),arg(c,limited?4:3)));return 1;
    }
    if(!strcmp(name,"mbrtowc")||!strcmp(name,"mbrlen")){
        int full=!strcmp(name,"mbrtowc");uint32_t dst=full?arg(c,0):0,src=arg(c,full?1:0),n=arg(c,full?2:1),state=arg(c,full?3:2);
        if(!multibyte_state(state)){returned(c,UINT32_MAX);return 1;}
        if(!src){returned(c,0);return 1;}if(!n){returned(c,UINT32_MAX-1);return 1;}
        uint32_t value=R8(src);if(value>=128){returned(c,multibyte_failure());return 1;}
        if(dst)W32(dst,value);returned(c,value?1:0);return 1;
    }
    if(!strcmp(name,"wcrtomb")){
        uint32_t dst=arg(c,0),value=arg(c,1),state=arg(c,2);
        if(!multibyte_state(state)){returned(c,UINT32_MAX);return 1;}
        if(!dst){returned(c,1);return 1;}if(value>=128){returned(c,multibyte_failure());return 1;}
        W8(dst,value);returned(c,1);return 1;
    }
    if(!strcmp(name,"wcscmp")||!strcmp(name,"wcsncmp")){
        uint32_t a=arg(c,0),b=arg(c,1),limit=!strcmp(name,"wcsncmp")?arg(c,2):UINT32_MAX;int result=0;
        for(uint32_t i=0;i<limit;i++){int32_t x=R32(a+i*4),y=R32(b+i*4);if(x!=y){result=x<y?-1:1;break;}if(!x)break;}
        returned(c,result);return 1;
    }
    return 0;
}
