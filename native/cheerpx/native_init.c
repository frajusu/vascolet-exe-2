/* Browser adapter setup and save export/import. MIT license. */
#include <sys/stat.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
static void parents(char*path){for(char*p=path+1;*p;p++)if(*p=='/'){*p=0;mkdir(path,0777);*p='/';}}
static int copy_manifest(const char*path){FILE*manifest=fopen(path,"r");if(!manifest)return 2;char line[512],source[300],dest[512],buffer[65536];while(fgets(line,sizeof(line),manifest)){for(char*p=line;*p;p++)if(*p=='\n'||*p=='\r')*p=0;if(!*line)continue;char*name=strchr(line,'|');if(!name){fclose(manifest);return 5;}*name++=0;snprintf(source,sizeof(source),"/seed/%s",line);snprintf(dest,sizeof(dest),"/%s",name);parents(dest);int in=open(source,O_RDONLY),out=open(dest,O_WRONLY|O_CREAT|O_TRUNC,0755);if(in<0||out<0){fclose(manifest);return 3;}int size;while((size=read(in,buffer,sizeof(buffer)))>0){int at=0;while(at<size){int n=write(out,buffer+at,size-at);if(n<1){fclose(manifest);return 4;}at+=n;}}close(in);close(out);chmod(dest,0755);}fclose(manifest);return 0;}
static void emit(uint32_t kind,const void*data,uint32_t size){uint8_t*message=malloc(size+12);uint32_t header[]={0x4c445356,kind,size};memcpy(message,header,12);if(size)memcpy(message+12,data,size);write(1,message,size+12);free(message);}
static size_t exported;
static int export_dir(const char*path){DIR*dir=opendir(path);if(!dir)return 0;struct dirent*entry;while((entry=readdir(dir))){if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;char full[1024];snprintf(full,sizeof(full),"%s/%s",path,entry->d_name);struct stat info;if(lstat(full,&info))continue;if(S_ISDIR(info.st_mode)){int result=export_dir(full);if(result){closedir(dir);return result;}}else if(S_ISREG(info.st_mode)){exported+=info.st_size;if(exported>32*1024*1024){closedir(dir);return 9;}const char*relative=full+6;uint32_t length=strlen(relative);uint8_t*data=malloc(4+length+info.st_size);memcpy(data,&length,4);memcpy(data+4,relative,length);FILE*file=fopen(full,"rb");if(!file){free(data);continue;}size_t size=fread(data+4+length,1,info.st_size,file);fclose(file);emit(21,data,4+length+size);free(data);}}closedir(dir);return 0;}
int main(int argc,char**argv){
 if(argc>1&&!strcmp(argv[1],"--export")){int status=export_dir("/home");emit(22,&status,4);return status;}
 if(argc>1&&!strcmp(argv[1],"--import"))return copy_manifest("/seed/restore-manifest");
 const char*root=getenv("SUGAR_ACTIVITY_ROOT");if(!root)root="/home/vascolet1";mkdir(root,0777);char directory[1024];const char*subdirs[]={"data","instance","tmp"};for(int n=0;n<3;n++){snprintf(directory,sizeof(directory),"%s/%s",root,subdirs[n]);mkdir(directory,0777);}
 int result=copy_manifest("/seed/manifest");if(!result&&argc>2){FILE*version=fopen("/game/.bundle-version","w");if(version){fputs(argv[2],version);fclose(version);}}return result;
}
