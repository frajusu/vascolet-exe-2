/* Minimal, explicit glibc i386 C-locale boundary. Other locales are NOT
 * approximated. Locale-data internals are opaque and direct accesses trap. */
#define C_LOCALE 0x08c00000u
#define CTYPE_TABLE 0x08c01000u
#define LOWER_TABLE 0x08c02000u
#define UPPER_TABLE 0x08c03000u
static uint32_t active_locale;
static void initialize_c_locale(void){
    active_locale=0xffffffffu;
    memcpy(guest_pointer(C_LOCALE+128,2),"C",2);
    for(unsigned cat=0;cat<13;cat++){W32(C_LOCALE+cat*4,0x70100000u+cat*16);W32(C_LOCALE+64+cat*4,C_LOCALE+128);}
    W32(C_LOCALE+52,CTYPE_TABLE+256);W32(C_LOCALE+56,LOWER_TABLE+512);W32(C_LOCALE+60,UPPER_TABLE+512);
    W32(C_LOCALE+160,CTYPE_TABLE+256);W32(C_LOCALE+164,LOWER_TABLE+512);W32(C_LOCALE+168,UPPER_TABLE+512);
    for(int c=-128;c<256;c++){
        unsigned flags=0;int x=c<-1?c+256:c;
        if(x>='A'&&x<='Z')flags|=0x0100;
        if(x>='a'&&x<='z')flags|=0x0200;
        if(flags)flags|=0x0400;
        if(x>='0'&&x<='9')flags|=0x0800;
        if((x>='0'&&x<='9')||(x>='A'&&x<='F')||(x>='a'&&x<='f'))flags|=0x1000;
        if(x==' '||(x>=9&&x<=13))flags|=0x2000;
        if(x>=32&&x<127)flags|=0x4000;
        if(x>32&&x<127)flags|=0x8000;
        if(x==' '||x=='\t')flags|=0x0001;
        if((x>=0&&x<32)||x==127)flags|=0x0002;
        if((flags&0x8000)&&!(flags&(0x0400|0x0800)))flags|=0x0004;
        if(flags&(0x0400|0x0800))flags|=0x0008;
        W16(CTYPE_TABLE+(c+128)*2,flags);
        W32(LOWER_TABLE+(c+128)*4,x>='A'&&x<='Z'?x+32:x);
        W32(UPPER_TABLE+(c+128)*4,x>='a'&&x<='z'?x-32:x);
    }
}
static int locale_import(CPU*c,const char*name){
    /* glibc MB_CUR_MAX query: the only accepted locales are C/POSIX, whose
     * multibyte encoding is single-byte. This is a size_t return, not a pointer.
     * Keep this in step with the strict locale checks below; not host locale. */
    if(!strcmp(name,"__ctype_get_mb_cur_max")||!strcmp(name,"_ctype_get_mb_cur_max")){
        if(active_locale!=C_LOCALE&&active_locale!=0xffffffffu)guest_trap(c,active_locale,"unknown multibyte locale");
        returned(c,1);return 1;
    }
    if(!strcmp(name,"__newlocale")||!strcmp(name,"newlocale")){
        uint32_t mask=arg(c,0),text=arg(c,1),base=arg(c,2);
        /* glibc also accepts 1 << LC_ALL (0x40) as the all-category shortcut;
         * the supplied libstdc++ uses that form, not the expanded 0x1fbf mask. */
        if(!text||(mask&~0x1fffu)){W32(cpu.fs_base+64,22);returned(c,0);return 1;}
        const char*locale=(char*)guest_pointer(text,string_length(text)+1);
        if(strcmp(locale,"C")&&strcmp(locale,"POSIX"))guest_trap(c,text,"only C/POSIX locale implemented");
        if(base&&base!=C_LOCALE)guest_trap(c,base,"unsupported base locale");
        returned(c,C_LOCALE);return 1;
    }
    if(!strcmp(name,"__duplocale")||!strcmp(name,"duplocale")){
        uint32_t value=arg(c,0);if(value!=C_LOCALE&&value!=0xffffffffu)guest_trap(c,value,"unsupported locale duplication");returned(c,C_LOCALE);return 1;
    }
    if(!strcmp(name,"__freelocale")||!strcmp(name,"freelocale")){if(arg(c,0)!=C_LOCALE)guest_trap(c,arg(c,0),"unknown locale release");returned(c,0);return 1;}
    if(!strcmp(name,"__uselocale")||!strcmp(name,"uselocale")){uint32_t next=arg(c,0),old=active_locale;if(next&&next!=C_LOCALE&&next!=0xffffffffu)guest_trap(c,next,"unsupported thread locale");if(next)active_locale=next;returned(c,old);return 1;}
    if(!strcmp(name,"__ctype_b_loc")){returned(c,C_LOCALE+160);return 1;}
    if(!strcmp(name,"__ctype_tolower_loc")){returned(c,C_LOCALE+164);return 1;}
    if(!strcmp(name,"__ctype_toupper_loc")){returned(c,C_LOCALE+168);return 1;}
    if(!strcmp(name,"__errno_location")){returned(c,cpu.fs_base+64);return 1;}
    {
        static const struct {const char*name;uint16_t mask;} classes[]={
            {"isalnum",0x0008},{"isalpha",0x0400},{"isblank",0x0001},{"iscntrl",0x0002},{"isdigit",0x0800},{"isgraph",0x8000},
            {"islower",0x0200},{"isprint",0x4000},{"ispunct",0x0004},{"isspace",0x2000},{"isupper",0x0100},{"isxdigit",0x1000}
        };
        for(unsigned i=0;i<12;i++)if(!strcmp(name,classes[i].name)){int32_t value=arg(c,0);returned(c,value>=-128&&value<256?R16(CTYPE_TABLE+(value+128)*2)&classes[i].mask:0);return 1;}
    }
    if(!strcmp(name,"tolower")||!strcmp(name,"toupper")){int32_t value=arg(c,0);uint32_t table=!strcmp(name,"tolower")?LOWER_TABLE:UPPER_TABLE;returned(c,value>=-128&&value<256?R32(table+(value+128)*4):(uint32_t)value);return 1;}
    if(!strcmp(name,"wctob")||!strcmp(name,"btowc")){uint32_t value=arg(c,0);returned(c,value<128?value:UINT32_MAX);return 1;}
    if(!strcmp(name,"wctype")||!strcmp(name,"wctype_l")||!strcmp(name,"__wctype_l")){
        static const char*classes[]={"alnum","alpha","blank","cntrl","digit","graph","lower","print","punct","space","upper","xdigit"};
        uint32_t p=arg(c,0);const char*value=(char*)guest_pointer(p,string_length(p)+1);unsigned result=0;
        for(unsigned i=0;i<12;i++)if(!strcmp(value,classes[i])){result=i+1;break;}
        returned(c,result);return 1;
    }
    if(!strcmp(name,"iswctype")||!strcmp(name,"iswctype_l")||!strcmp(name,"__iswctype_l")){
        static const uint16_t masks[]={0x0008,0x0400,0x0001,0x0002,0x0800,0x8000,0x0200,0x4000,0x0004,0x2000,0x0100,0x1000};
        uint32_t value=arg(c,0),kind=arg(c,1);if(!kind||kind>12)guest_trap(c,kind,"unknown C wctype descriptor");
        returned(c,value<128&&!!(R16(CTYPE_TABLE+(value+128)*2)&masks[kind-1]));return 1;
    }
    if(!strcmp(name,"towlower")||!strcmp(name,"__towlower_l")||!strcmp(name,"towlower_l")){uint32_t value=arg(c,0);returned(c,value>='A'&&value<='Z'?value+32:value);return 1;}
    if(!strcmp(name,"towupper")||!strcmp(name,"__towupper_l")||!strcmp(name,"towupper_l")){uint32_t value=arg(c,0);returned(c,value>='a'&&value<='z'?value-32:value);return 1;}
    /* POSIX C locale has no message catalog translations. Return the supplied
     * msgid, as gettext specifies for this locale; don't invent translations. */
    if(!strcmp(name,"gettext")){uint32_t text=arg(c,0),n=string_length(text);if(n>sizeof(last_message)-1)n=sizeof(last_message)-1;memcpy(last_message,guest_pointer(text,n),n);last_message[n]=0;returned(c,text);return 1;}
    if(!strcmp(name,"dgettext")||!strcmp(name,"dcgettext")){returned(c,arg(c,1));return 1;}
    if(!strcmp(name,"ngettext")){returned(c,arg(c,arg(c,2)==1?0:1));return 1;}
    return 0;
}
