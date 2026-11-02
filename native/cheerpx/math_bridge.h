/* Exact SSE fast paths for translations/reflections. Arbitrary scales and
 * rotations still execute the original x87 functions through trampolines. */
#ifdef MATH_MULTIPLY
static void*(*original_multiply)(float*,const float*);
static void*(*original_premultiply)(float*,const float*);
static void(*original_transform)(const float*,float*);
static int axis_unit(const float*m){uint32_t b[4];memcpy(b,m,16);return(b[0]&0x7fffffff)==0x3f800000&&(b[3]&0x7fffffff)==0x3f800000&&!(b[1]&0x7fffffff)&&!(b[2]&0x7fffffff);}
static void multiply_values(float*out,const float*a,const float*b){float values[]={a[0]*b[0]+a[1]*b[2],a[0]*b[1]+a[1]*b[3],a[2]*b[0]+a[3]*b[2],a[2]*b[1]+a[3]*b[3],(a[4]*b[0]+a[5]*b[2])+b[4],(a[4]*b[1]+a[5]*b[3])+b[5]};memcpy(out,values,sizeof(values));}
static void*fast_matrix_multiply(float*a,const float*b){if(a==b||!axis_unit(b))return original_multiply(a,b);multiply_values(a,a,b);return a;}
static void*fast_matrix_premultiply(float*a,const float*b){if(a==b||!axis_unit(a))return original_premultiply(a,b);multiply_values(a,b,a);return a;}
static void fast_matrix_transform(const float*m,float*v){if(!axis_unit(m)){original_transform(m,v);return;}float x=(v[0]*m[0]+v[1]*m[2])+m[4],y=(v[0]*m[1]+v[1]*m[3])+m[5];v[0]=x;v[1]=y;}
static void*fast_matrix_scale(float*m,float x,float y){for(unsigned n=0;n<6;n++)m[n]*=n&1?y:x;return m;}
static void*fast_matrix_translate(float*m,float x,float y){m[4]+=x;m[5]+=y;return m;}
typedef struct{float values[4];} MathColor;
static MathColor fast_color_product(const float*a,const float*b){MathColor result;for(unsigned n=0;n<4;n++)result.values[n]=a[n]*b[n];return result;}
static MathColor fast_color_scale(const float*a,float b){MathColor result;for(unsigned n=0;n<4;n++)result.values[n]=a[n]*b;return result;}
static void*fast_color_assign_product(float*a,const float*b){for(unsigned n=0;n<4;n++)a[n]*=b[n];return a;}
static void*fast_color_assign_scale(float*a,float b){for(unsigned n=0;n<4;n++)a[n]*=b;return a;}
static void*math_trampoline(unsigned address,const unsigned char*prologue,unsigned length){unsigned char*code=mmap(0,4096,7,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(code==MAP_FAILED)_exit(92);memcpy(code,prologue,length);code[length]=0xe9;int32_t offset=address+length-(uintptr_t)(code+length)-5;memcpy(code+length+1,&offset,4);__builtin___clear_cache((char*)code,(char*)code+length+5);mprotect(code,4096,5);return code;}
#ifdef MATH_TRACK_HILL
static float(*original_track_hill)(const void*,float);
static float(*original_track_curve)(const void*,float);
static float fast_track_sample(const void*track,float position,unsigned vector_offset,unsigned spacing_offset,float(*fallback)(const void*,float)){
    const float*values=*(float*const*)((const char*)track+vector_offset);const float*end=*(float*const*)((const char*)track+vector_offset+4);unsigned count=end-values;if(!count)return 0;
    double spacing=*(const float*)((const char*)track+spacing_offset),p=position;if(!(p>=0&&spacing>0&&p/spacing<1000000))return fallback(track,position);
    unsigned index=(unsigned)(p/spacing),left=index,next;int loop=((const unsigned char*)track)[4];if(left>=count)left=loop?left%count:count-1;next=left+1;if(next>=count)next=loop?next%count:count-1;
    double a=values[left],b=values[next];if(a==b)return a;
    double t=(p-index*spacing)/spacing,weight=t<.5?2*t*t:1-2*(1-t)*(1-t);return a+(b-a)*weight;
}
static float fast_track_hill(const void*track,float p){return fast_track_sample(track,p,28,8,original_track_hill);}
static float fast_track_curve(const void*track,float p){return fast_track_sample(track,p,40,12,original_track_curve);}
static void install_track_acceleration(void){original_track_hill=math_trampoline(MATH_TRACK_HILL,math_track_hill_prologue,sizeof(math_track_hill_prologue));original_track_curve=math_trampoline(MATH_TRACK_CURVE,math_track_curve_prologue,sizeof(math_track_curve_prologue));uint32_t track[13]={0};float total=64,spacing=8;memcpy(track,&total,4);memcpy(track+2,&spacing,4);memcpy(track+3,&spacing,4);float values[]={-.125f,.25f,.5f,-.75f,.125f,0,1.5f,1.5f};track[7]=track[10]=(uintptr_t)values;track[8]=track[11]=(uintptr_t)(values+8);
    for(unsigned n=0;n<2048;n++){track[1]=n&1;float p=(n*193%4096)*.03125f,a=original_track_hill(track,p),b=fast_track_hill(track,p);if(memcmp(&a,&b,4))goto failed;a=original_track_curve(track,p);b=fast_track_curve(track,p);if(memcmp(&a,&b,4))goto failed;}
    color_jump(MATH_TRACK_HILL,fast_track_hill,math_track_hill_prefix);color_jump(MATH_TRACK_CURVE,fast_track_curve,math_track_curve_prefix);fprintf(stderr,"BROWSER_TRACK SSE interpolation verified on 4096 samples\n");return;
failed:fprintf(stderr,"BROWSER_TRACK validation failed; original track interpolation retained\n");}
#else
static void install_track_acceleration(void){}
#endif
static void install_math_acceleration(void){static int done;if(done)return;done=1;original_multiply=math_trampoline(MATH_MULTIPLY,math_multiply_prologue,sizeof(math_multiply_prologue));original_premultiply=math_trampoline(MATH_PREMULTIPLY,math_premultiply_prologue,sizeof(math_premultiply_prologue));original_transform=math_trampoline(MATH_TRANSFORM,math_transform_prologue,sizeof(math_transform_prologue));
    for(unsigned n=0;n<256;n++){float axis[]={n&1?-1:1,0,0,n&2?-1:1,(int)(n*37%193)-96.25f,(int)(n*29%157)-78.5f},value[]={0.75f,0.5f,-0.25f,1.125f,(int)(n*11%129)-64.75f,(int)(n*7%113)-56.5f},a[6],b[6];memcpy(a,value,24);memcpy(b,value,24);original_multiply(a,axis);fast_matrix_multiply(b,axis);if(memcmp(a,b,24))goto failed;memcpy(a,axis,24);memcpy(b,axis,24);original_premultiply(a,value);fast_matrix_premultiply(b,value);if(memcmp(a,b,24))goto failed;float va[]={value[4],value[5]},vb[]={value[4],value[5]};original_transform(axis,va);fast_matrix_transform(axis,vb);if(memcmp(va,vb,8))goto failed;}
    void*(*volatile scale)(float*,float,float)=(void*)(uintptr_t)MATH_SCALE;void*(*volatile translate)(float*,float,float)=(void*)(uintptr_t)MATH_TRANSLATE;MathColor(*volatile product)(const float*,const float*)=(void*)(uintptr_t)MATH_COLOR_PRODUCT;MathColor(*volatile color_scale)(const float*,float)=(void*)(uintptr_t)MATH_COLOR_SCALE;void*(*volatile assign_product)(float*,const float*)=(void*)(uintptr_t)MATH_COLOR_ASSIGN_PRODUCT;void*(*volatile assign_scale)(float*,float)=(void*)(uintptr_t)MATH_COLOR_ASSIGN_SCALE;
    for(unsigned n=0;n<256;n++){float input[]={0.125f,0.375f,-0.75f,0.875f,n*.125f,n*.375f},a[6],b[6],x=n*.03125f,y=-n*.125f;memcpy(a,input,24);memcpy(b,input,24);scale(a,x,y);fast_matrix_scale(b,x,y);if(memcmp(a,b,24))goto failed;translate(a,x,y);fast_matrix_translate(b,x,y);if(memcmp(a,b,24))goto failed;MathColor ca=product(a,input),cb=fast_color_product(a,input);if(memcmp(&ca,&cb,16))goto failed;ca=color_scale(a,x);cb=fast_color_scale(a,x);if(memcmp(&ca,&cb,16))goto failed;assign_product(a,input);fast_color_assign_product(b,input);if(memcmp(a,b,24))goto failed;assign_scale(a,x);fast_color_assign_scale(b,x);if(memcmp(a,b,24))goto failed;}
    color_jump(MATH_MULTIPLY,fast_matrix_multiply,math_multiply_prefix);color_jump(MATH_PREMULTIPLY,fast_matrix_premultiply,math_premultiply_prefix);color_jump(MATH_TRANSFORM,fast_matrix_transform,math_transform_prefix);color_jump(MATH_SCALE,fast_matrix_scale,math_scale_prefix);color_jump(MATH_TRANSLATE,fast_matrix_translate,math_translate_prefix);color_jump(MATH_COLOR_PRODUCT,fast_color_product,math_color_product_prefix);color_jump(MATH_COLOR_SCALE,fast_color_scale,math_color_scale_prefix);color_jump(MATH_COLOR_ASSIGN_PRODUCT,fast_color_assign_product,math_color_assign_product_prefix);color_jump(MATH_COLOR_ASSIGN_SCALE,fast_color_assign_scale,math_color_assign_scale_prefix);fprintf(stderr,"BROWSER_MATH exact transform/color fast paths verified on 2304 samples\n");install_track_acceleration();return;
failed:fprintf(stderr,"BROWSER_MATH validation failed; original affine math retained\n");}
#else
static void install_math_acceleration(void){}
#endif
