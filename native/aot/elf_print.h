/* Linux i386 va_list is a guest stack pointer, NOT a native Wasm va_list.
 * Marshal one conversion at a time to typed native snprintf arguments.
 * Unsupported positional/wide/80-bit formats trap instead of misreading data.
 */
#define PRINT_FIELD_LIMIT 1048576u
typedef struct {char *bytes;uint32_t capacity;uint64_t total;} GuestPrint;
static void print_literal(GuestPrint*out,const char*text,unsigned length){
    if(out->capacity&&out->total<out->capacity-1){uint32_t room=out->capacity-1-out->total,n=length<room?length:room;memcpy(out->bytes+out->total,text,n);}
    out->total+=length;
}
static uint32_t print_word(uint32_t*args){uint32_t value=R32(*args);*args+=4;return value;}
static uint64_t print_quad(uint32_t*args){uint64_t value=R64(*args);*args+=8;return value;}
static int guest_format(uint32_t target,uint32_t capacity,uint32_t format,uint32_t args){
    GuestPrint out={capacity?(char*)guest_pointer(target,capacity):NULL,capacity,0};
    const char *p=guest_text(format);unsigned conversions=0;
    while(*p){
        if(*p!='%'){const char*begin=p;while(*p&&*p!='%')p++;print_literal(&out,begin,p-begin);continue;}
        p++;if(*p=='%'){print_literal(&out,"%",1);p++;continue;}
        if(++conversions>4096)guest_trap(&cpu,format,"printf conversion limit exceeded");
        char spec[96]="%";unsigned at=1;
        while(*p&&strchr("#0- +'",*p)){if(at>=16)guest_trap(&cpu,format,"printf flag limit exceeded");spec[at++]=*p++;}
        int32_t width=0,precision=-1;
        if(*p=='*'){p++;width=(int32_t)print_word(&args);if(width<0){if(width==INT32_MIN)guest_trap(&cpu,format,"printf width exceeds bound");spec[at++]='-';width=-width;}}
        else while(*p>='0'&&*p<='9'){if(width>(int32_t)PRINT_FIELD_LIMIT/10)guest_trap(&cpu,format,"printf width exceeds bound");width=width*10+(*p++-'0');}
        if(width>(int32_t)PRINT_FIELD_LIMIT)guest_trap(&cpu,format,"printf width exceeds bound");
        if(width)at+=snprintf(spec+at,sizeof spec-at,"%d",width);
        if(*p=='.'){
            p++;precision=0;if(*p=='*'){p++;precision=(int32_t)print_word(&args);}
            else while(*p>='0'&&*p<='9'){if(precision>(int32_t)PRINT_FIELD_LIMIT/10)guest_trap(&cpu,format,"printf precision exceeds bound");precision=precision*10+(*p++-'0');}
            if(precision>(int32_t)PRINT_FIELD_LIMIT)guest_trap(&cpu,format,"printf precision exceeds bound");
            if(precision>=0)at+=snprintf(spec+at,sizeof spec-at,".%d",precision);
        }
        /* i386 long/size_t/ptrdiff_t are 32 bits; long long/intmax_t are 64. */
        int length=0;
        if(*p=='h'){p++;length=-1;if(*p=='h'){p++;length=-2;}}
        else if(*p=='l'){p++;length=1;if(*p=='l'){p++;length=2;}}
        else if(*p=='j'){p++;length=2;}
        else if(*p=='z'||*p=='t'){p++;length=1;}
        else if(*p=='L'){p++;length=3;}
        char conversion=*p;if(!conversion)guest_trap(&cpu,format,"incomplete printf format");p++;
        if(conversion=='n'){
            uint32_t pointer=print_word(&args);
            if(length==-2)W8(pointer,out.total);else if(length==-1)W16(pointer,out.total);else if(length==2)W64(pointer,out.total);else if(length<=1)W32(pointer,out.total);else guest_trap(&cpu,format,"unsupported printf count format");
            continue;
        }
        if(strchr("diouxX",conversion)){
            if(length==3)guest_trap(&cpu,format,"unsupported integer printf length");
            if(length==2){spec[at++]='l';spec[at++]='l';}
            else if(length<0){spec[at++]='h';if(length==-2)spec[at++]='h';}
        }else if(strchr("aAeEfFgG",conversion)){
            if(length!=0&&length!=1)guest_trap(&cpu,format,"80-bit printf argument not implemented");
        }else if(conversion=='s'||conversion=='c'||conversion=='p'){
            if(length)guest_trap(&cpu,format,"wide printf argument not implemented");
        }else guest_trap(&cpu,format,"unsupported printf conversion");
        spec[at++]=conversion;spec[at]=0;
        uint32_t stored=out.capacity&&out.total<out.capacity?out.total:out.capacity;
        char *destination=out.capacity>stored?out.bytes+stored:NULL;
        size_t room=out.capacity>stored?out.capacity-stored:0;int count;
        if(strchr("diouxX",conversion)){
            if(length==2){uint64_t value=print_quad(&args);count=(conversion=='d'||conversion=='i')?snprintf(destination,room,spec,(long long)value):snprintf(destination,room,spec,(unsigned long long)value);}
            else{uint32_t value=print_word(&args);count=(conversion=='d'||conversion=='i')?snprintf(destination,room,spec,(int32_t)value):snprintf(destination,room,spec,value);}
        }else if(strchr("aAeEfFgG",conversion)){
            uint64_t bits=print_quad(&args);double value;memcpy(&value,&bits,8);count=snprintf(destination,room,spec,value);
        }else if(conversion=='s'){
            uint32_t pointer=print_word(&args);const char *text="(null)";
            if(pointer){if(precision>=0){unsigned n=0;while(n<(unsigned)precision&&R8(pointer+n))n++;text=n?(char*)guest_pointer(pointer,n):"";}else text=guest_text(pointer);}
            count=snprintf(destination,room,spec,text);
        }else if(conversion=='c')count=snprintf(destination,room,spec,(int)print_word(&args));
        else count=snprintf(destination,room,spec,(void*)(uintptr_t)print_word(&args));
        if(count<0)return -1;out.total+=(unsigned)count;
        if(out.total>INT_MAX)guest_trap(&cpu,format,"printf result exceeds INT_MAX");
    }
    if(capacity)out.bytes[out.total<capacity?out.total:capacity-1]=0;
    return (int)out.total;
}
static int print_import(CPU*c,const char*name){
    if(!strcmp(name,"snprintf")||!strcmp(name,"vsnprintf")){
        uint32_t args=!strcmp(name,"vsnprintf")?arg(c,3):c->esp+16;
        returned(c,guest_format(arg(c,0),arg(c,1),arg(c,2),args));return 1;
    }
    if(!strcmp(name,"sprintf")||!strcmp(name,"vsprintf")){
        uint32_t target=arg(c,0),args=!strcmp(name,"vsprintf")?arg(c,2):c->esp+12;
        /* Format once to determine the exact validated destination span. */
        int length=guest_format(0,0,arg(c,1),args);if(length<0){returned(c,-1);return 1;}
        returned(c,guest_format(target,(uint32_t)length+1,arg(c,1),args));return 1;
    }
    return 0;
}
