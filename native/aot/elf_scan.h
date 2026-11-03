/* Marshal scanf output pointers, not guest va_list bytes, into Wasm libc. */
static int scan_import(CPU*c,const char*name){
    if(strcmp(name,"sscanf")&&strcmp(name,"__isoc99_sscanf"))return 0;
    const char*input=guest_text(arg(c,0)),*format=guest_text(arg(c,1));void*out[32]={0};unsigned count=0;
    for(const char*p=format;*p;p++){
        if(*p!='%')continue;p++;if(*p=='%')continue;
        int suppressed=0;if(*p=='*'){suppressed=1;p++;}
        uint32_t width=0;while(*p>='0'&&*p<='9'){if(width>1000000)guest_trap(c,0,"scanf width exceeds diagnostic bound");width=width*10+(*p++-'0');}
        unsigned length=0;if(*p=='h'){length=1;p++;if(*p=='h'){length=2;p++;}}
        else if(*p=='l'){length=3;p++;if(*p=='l'){length=4;p++;}}
        else if(*p=='L')guest_trap(c,0,"scanf long-double ABI not implemented");
        else if(*p=='z'||*p=='t'){length=3;p++;}
        else if(*p=='j'){length=4;p++;}
        char conversion=*p;uint32_t bytes=0;
        if(!conversion)guest_trap(c,0,"incomplete scanf format");
        if(strchr("diuoxXnp",conversion))bytes=length==1?2:length==2?1:length==4?8:4;
        else if(strchr("aAeEfFgG",conversion))bytes=length==3?8:4;
        else if(conversion=='s'||conversion=='['||conversion=='c'){
            uint64_t size=(uint64_t)(width?width:conversion=='c'?1:strlen(input))+(conversion=='c'?0:1);if(length==3)size*=4;
            if(size>UINT32_MAX)guest_trap(c,0,"scanf buffer size overflow");bytes=size;
            if(conversion=='['){p++;if(*p=='^')p++;if(*p==']')p++;while(*p&&*p!=']')p++;if(!*p)guest_trap(c,0,"invalid scanf scanset");}
        }else guest_trap(c,0,"unsupported scanf conversion");
        if(!suppressed){if(count==32)guest_trap(c,0,"too many scanf outputs");out[count]=guest_pointer(arg(c,count+2),bytes);count++;}
    }
    int result=sscanf(input,format,out[0],out[1],out[2],out[3],out[4],out[5],out[6],out[7],out[8],out[9],out[10],out[11],out[12],out[13],out[14],out[15],out[16],out[17],out[18],out[19],out[20],out[21],out[22],out[23],out[24],out[25],out[26],out[27],out[28],out[29],out[30],out[31]);
    returned(c,result);return 1;
}
