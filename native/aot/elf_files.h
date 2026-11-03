/* i386 stdio handles are opaque: native FILE* never enter guest memory. */
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/stat.h>
static FILE *guest_files[256];
static unsigned char guest_owned_fds[1024];
static FILE *file_from_handle(uint32_t value){
    if(value==0x70000010u)return stdin;if(value==0x70000020u)return stdout;if(value==0x70000030u)return stderr;
    if(value>=0x70010000u&&(value-0x70010000u)%16==0){unsigned i=(value-0x70010000u)/16;if(i<256&&guest_files[i])return guest_files[i];}
    guest_trap(&cpu,value,"unknown guest FILE handle");
}
static const char *guest_text(uint32_t address){return(char*)guest_pointer(address,string_length(address)+1);}
static void reset_files(void){for(unsigned i=0;i<256;i++)if(guest_files[i]){fclose(guest_files[i]);guest_files[i]=NULL;}for(unsigned i=3;i<1024;i++)if(guest_owned_fds[i]){close(i);guest_owned_fds[i]=0;}}
static int guest_fd(uint32_t value){
    if(value<3||(value<1024&&guest_owned_fds[value]))return (int)value;
    for(unsigned i=0;i<256;i++)if(guest_files[i]&&fileno(guest_files[i])==(int)value)return (int)value;
    errno=EBADF;return -1;
}
static void file_result(CPU*c,int64_t result){if(result<0)W32(cpu.fs_base+64,errno);returned(c,(uint32_t)result);}
static int guest_stat64(uint32_t target,const struct stat*s){
    memset(guest_pointer(target,96),0,96);
    W64(target,s->st_dev);W32(target+12,s->st_ino);W32(target+16,s->st_mode);W32(target+20,s->st_nlink);
    W32(target+24,s->st_uid);W32(target+28,s->st_gid);W64(target+32,s->st_rdev);
    W64(target+44,s->st_size);W32(target+52,s->st_blksize);W64(target+56,s->st_blocks);
    W32(target+64,s->st_atim.tv_sec);W32(target+68,s->st_atim.tv_nsec);
    W32(target+72,s->st_mtim.tv_sec);W32(target+76,s->st_mtim.tv_nsec);
    W32(target+80,s->st_ctim.tv_sec);W32(target+84,s->st_ctim.tv_nsec);W64(target+88,s->st_ino);return 0;
}
static int file_import(CPU*c,const char*name){
    if(!strcmp(name,"open")||!strcmp(name,"open64")){
        uint32_t flags=arg(c,1),allowed=3|64|128|256|512|1024|2048|32768|65536|131072|524288;
        if(flags&~allowed)guest_trap(c,flags,"unsupported guest open flags");
        int native=(flags&3);if(flags&64)native|=O_CREAT;if(flags&128)native|=O_EXCL;if(flags&512)native|=O_TRUNC;if(flags&1024)native|=O_APPEND;
        if(flags&2048)native|=O_NONBLOCK;if(flags&65536)native|=O_DIRECTORY;if(flags&131072)native|=O_NOFOLLOW;if(flags&524288)native|=O_CLOEXEC;
        int fd=open(guest_text(arg(c,0)),native,(mode_t)(flags&64?arg(c,2):0));
        if(fd>=1024){close(fd);errno=EMFILE;fd=-1;}if(fd>=0)guest_owned_fds[fd]=1;file_result(c,fd);return 1;
    }
    if(!strcmp(name,"read")||!strcmp(name,"write")){
        int fd=guest_fd(arg(c,0));uint32_t n=arg(c,2);if(n>INT_MAX){errno=EINVAL;file_result(c,-1);return 1;}
        void*buffer=n?guest_pointer(arg(c,1),n):NULL;
        file_result(c,fd<0?-1:!strcmp(name,"read")?read(fd,buffer,n):write(fd,buffer,n));return 1;
    }
    if(!strcmp(name,"writev")){
        int fd=guest_fd(arg(c,0));uint32_t pointer=arg(c,1),count=arg(c,2);struct iovec vectors[1024];uint64_t total=0;
        if(fd<0){file_result(c,-1);return 1;}if(count>1024){errno=EINVAL;file_result(c,-1);return 1;}
        if(count)guest_pointer(pointer,count*8);
        for(unsigned i=0;i<count;i++){uint32_t base=R32(pointer+i*8),n=R32(pointer+i*8+4);total+=n;if(total>INT_MAX){errno=EINVAL;file_result(c,-1);return 1;}vectors[i].iov_base=n?guest_pointer(base,n):NULL;vectors[i].iov_len=n;}
        file_result(c,writev(fd,vectors,count));return 1;
    }
    if(!strcmp(name,"close")){int fd=guest_fd(arg(c,0));int result=fd<0?-1:close(fd);if(result==0&&fd<1024)guest_owned_fds[fd]=0;file_result(c,result);return 1;}
    if(!strcmp(name,"lseek64")){
        int fd=guest_fd(arg(c,0));int64_t offset=(int64_t)((uint64_t)arg(c,1)|((uint64_t)arg(c,2)<<32));
        int64_t result=fd<0?-1:lseek(fd,offset,arg(c,3));file_result(c,result);c->edx=(uint64_t)result>>32;return 1;
    }
    if(!strcmp(name,"mkdir")){file_result(c,mkdir(guest_text(arg(c,0)),arg(c,1)));return 1;}
    if(!strcmp(name,"rename")){file_result(c,rename(guest_text(arg(c,0)),guest_text(arg(c,1))));return 1;}
    if(!strcmp(name,"__fxstat64")||!strcmp(name,"__xstat64")||!strcmp(name,"__lxstat64")){
        if(arg(c,0)!=3)guest_trap(c,arg(c,0),"unsupported i386 stat64 version");struct stat info;int result;
        if(!strcmp(name,"__fxstat64")){int fd=guest_fd(arg(c,1));result=fd<0?-1:fstat(fd,&info);}
        else result=!strcmp(name,"__lxstat64")?lstat(guest_text(arg(c,1)),&info):stat(guest_text(arg(c,1)),&info);
        if(!result)guest_stat64(arg(c,2),&info);file_result(c,result);return 1;
    }
    if(!strcmp(name,"fdopen")){
        int fd=guest_fd(arg(c,0));FILE*f=fd<0?NULL:fdopen(fd,guest_text(arg(c,1)));
        if(!f){W32(cpu.fs_base+64,errno);returned(c,0);return 1;}
        for(unsigned i=0;i<256;i++)if(!guest_files[i]){guest_files[i]=f;if(fd<1024)guest_owned_fds[fd]=0;returned(c,0x70010000u+i*16);return 1;}
        fclose(f);W32(cpu.fs_base+64,EMFILE);returned(c,0);return 1;
    }
    if(!strcmp(name,"fopen")||!strcmp(name,"fopen64")){
        FILE*f=fopen(guest_text(arg(c,0)),guest_text(arg(c,1)));if(!f){W32(cpu.fs_base+64,errno);returned(c,0);return 1;}
        for(unsigned i=0;i<256;i++)if(!guest_files[i]){guest_files[i]=f;returned(c,0x70010000u+i*16);return 1;}
        fclose(f);W32(cpu.fs_base+64,24);returned(c,0);return 1;
    }
    if(!strcmp(name,"fclose")){uint32_t handle=arg(c,0);FILE*f=file_from_handle(handle);if(handle<0x70010000u)guest_trap(c,handle,"closing a standard stream unsupported");guest_files[(handle-0x70010000u)/16]=NULL;returned(c,fclose(f));return 1;}
    if(!strcmp(name,"fread")||!strcmp(name,"fwrite")){
        uint32_t size=arg(c,1),count=arg(c,2);uint64_t bytes=(uint64_t)size*count;if(bytes>UINT32_MAX)guest_trap(c,0,"stdio size overflow");
        FILE*f=file_from_handle(arg(c,3));void*p=bytes?guest_pointer(arg(c,0),bytes):NULL;
        returned(c,!strcmp(name,"fread")?fread(p,size,count,f):fwrite(p,size,count,f));return 1;
    }
    if(!strcmp(name,"fseek")){returned(c,fseek(file_from_handle(arg(c,0)),(int32_t)arg(c,1),arg(c,2)));return 1;}
    if(!strcmp(name,"ftell")){returned(c,ftell(file_from_handle(arg(c,0))));return 1;}
    if(!strcmp(name,"ferror")){returned(c,ferror(file_from_handle(arg(c,0))));return 1;}
    if(!strcmp(name,"feof")){returned(c,feof(file_from_handle(arg(c,0))));return 1;}
    if(!strcmp(name,"clearerr")){clearerr(file_from_handle(arg(c,0)));returned(c,0);return 1;}
    if(!strcmp(name,"fgetc")||!strcmp(name,"getc")){returned(c,fgetc(file_from_handle(arg(c,0))));return 1;}
    if(!strcmp(name,"fputc")||!strcmp(name,"putc")){returned(c,fputc(arg(c,0),file_from_handle(arg(c,1))));return 1;}
    if(!strcmp(name,"ungetc")){returned(c,ungetc(arg(c,0),file_from_handle(arg(c,1))));return 1;}
    if(!strcmp(name,"fputs")){returned(c,fputs(guest_text(arg(c,0)),file_from_handle(arg(c,1))));return 1;}
    if(!strcmp(name,"fflush")){returned(c,fflush(arg(c,0)?file_from_handle(arg(c,0)):NULL));return 1;}
    if(!strcmp(name,"fileno")){returned(c,fileno(file_from_handle(arg(c,0))));return 1;}
    return 0;
}
