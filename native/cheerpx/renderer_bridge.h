/* Browser rendering for the original engine's YUV sprite/line API. MIT.
 * The original game still calculates positions, animation, collisions, and time.
 * Texture data and alpha come from its original loader. Only rasterization moves.
 */
#ifdef RENDERER_DRAW
typedef struct NativeTextureRecord {void*texture;struct NativeTextureRecord*next;} NativeTextureRecord;
static NativeTextureRecord*native_textures[2048];
static unsigned native_texture_bucket(const void*texture){uintptr_t p=(uintptr_t)texture;return((p>>4)^(p>>16))&2047;}
static void native_texture_forget(const void*texture){NativeTextureRecord**entry=&native_textures[native_texture_bucket(texture)];while(*entry){if((*entry)->texture==texture){NativeTextureRecord*old=*entry;*entry=old->next;free(old);uint32_t id=(uintptr_t)texture;emit(35,&id,4);break;}entry=&(*entry)->next;}}
static uint32_t*native_texture_data(const void*texture){native_texture_forget(texture);return(void*)(uintptr_t)((const uint32_t*)texture)[7];}
static void native_texture_unload(void*texture){native_texture_forget(texture);void**data=(void**)((char*)texture+28);void(*volatile release)(void*)=(void*)(uintptr_t)ENGINE_ARRAY_DELETE;if(data[1])release(data[1]);if(data[0])release(data[0]);data[0]=data[1]=0;}
static void native_texture_upload(void*renderer,const void*texture){unsigned bucket=native_texture_bucket(texture);for(NativeTextureRecord*entry=native_textures[bucket];entry;entry=entry->next)if(entry->texture==texture)return;const uint32_t*t=texture;unsigned w=t[5],h=t[6],count=w*h;const void*pixels=(void*)(uintptr_t)t[7],*alpha=(void*)(uintptr_t)t[8];if(!pixels||!w||!h||w>8192||h>8192)return;uint32_t meta[]={(uintptr_t)texture,w,h,t[2],((uint32_t*)renderer)[21],alpha?1:0};unsigned size=sizeof(meta)+count*4+(alpha?count*2:0);unsigned char*packet=malloc(size);memcpy(packet,meta,sizeof(meta));memcpy(packet+sizeof(meta),pixels,count*4);if(alpha)memcpy(packet+sizeof(meta)+count*4,alpha,count*2);emit(31,packet,size);free(packet);NativeTextureRecord*entry=malloc(sizeof(*entry));entry->texture=(void*)texture;entry->next=native_textures[bucket];native_textures[bucket]=entry;}
static void native_clear(void*renderer){native_gpu_frame=1;uint32_t packet[6]={render_width,render_height};memcpy(packet+2,(char*)renderer+28,16);emit(30,packet,sizeof(packet));}
static void native_draw(void*renderer,unsigned mode,const void*texture,const float*source,const float*origin,const float*matrix,float alpha,const float*color,unsigned tint,unsigned blend,int extra){
    if(!render_width||!render_height||alpha<=0)return;
    int flip=matrix[0]<=0;float dx=matrix[4]+render_width/2.0f+(flip?origin[0]-source[2]:-origin[0]),dy=matrix[5]+render_height/2.0f-origin[1];
    if(dx>=render_width||dy>=render_height||dx+source[2]<=0||dy+source[3]<=0)return;
    native_texture_upload(renderer,texture);
    uint32_t packet[15]={(uintptr_t)texture,mode==1?((uint32_t*)texture)[2]:mode,blend,flip};
    memcpy(packet+4,source,16);float position[]={dx,dy,alpha};memcpy(packet+8,position,sizeof(position));memcpy(packet+11,color,16);
    memcpy((char*)renderer+68,color,16);((uint32_t*)renderer)[16]=blend;
    emit(32,packet,sizeof(packet));
}
static void native_line(void*renderer,const float*from,const float*to,float width,const float*matrix,const float*color,unsigned blend,int extra){float values[]={from[0]*matrix[0]+from[1]*matrix[2]+matrix[4]+render_width/2.0f,from[0]*matrix[1]+from[1]*matrix[3]+matrix[5]+render_height/2.0f,to[0]*matrix[0]+to[1]*matrix[2]+matrix[4]+render_width/2.0f,to[0]*matrix[1]+to[1]*matrix[3]+matrix[5]+render_height/2.0f,width,color[0],color[1],color[2],color[3]};uint32_t packet[10];packet[0]=blend;memcpy(packet+1,values,sizeof(values));emit(33,packet,sizeof(packet));}
static void install_renderer_acceleration(void){static int installed;if(installed||getenv("BROWSER_SOFTWARE_RENDERER"))return;installed=1;native_gpu_frame=1;color_jump(RENDERER_DRAW,native_draw,renderer_draw_prefix);color_jump(RENDERER_CLEAR,native_clear,renderer_clear_prefix);color_jump(RENDERER_LINE,native_line,renderer_line_prefix);color_jump(RENDERER_UNLOAD,native_texture_unload,renderer_unload_prefix);color_jump(RENDERER_DATA,native_texture_data,renderer_data_prefix);fprintf(stderr,"BROWSER_RENDERER browser sprite/line rasterization enabled\n");}
#else
static void install_renderer_acceleration(void){}
#endif
