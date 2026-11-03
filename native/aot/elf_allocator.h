/* Bounded guest allocator. Physical neighbors allow constant-time coalescing;
 * size bins avoid rescanning every live XML object on every malloc/free.
 * 262144 metadata slots + 262144 hash buckets occupy exactly 9 MiB.
 * The guest heap/window and Wasm memory maximum are NOT increased.
 */
#define ALLOCATION_CAPACITY 262144u
#define ALLOCATION_NONE UINT_MAX
typedef struct {
    uint32_t address,size;
    unsigned used,previous,next,free_previous,free_next,hash_next;
} GuestAllocation;
static GuestAllocation allocations[ALLOCATION_CAPACITY];
static unsigned allocation_count,allocation_spare,allocation_tail;
static unsigned allocation_bins[32],allocation_hash[ALLOCATION_CAPACITY];

static void allocator_reset(void){
    memset(allocations,0,sizeof allocations);memset(allocation_hash,0,sizeof allocation_hash);
    for(unsigned i=0;i<32;i++)allocation_bins[i]=ALLOCATION_NONE;
    allocation_count=0;allocation_spare=allocation_tail=ALLOCATION_NONE;
}
static unsigned allocation_bin(uint32_t size){return 31u-(unsigned)__builtin_clz(size);}
static unsigned allocation_bucket(uint32_t address){return ((address>>4)*2654435761u)&(ALLOCATION_CAPACITY-1);}
static unsigned allocation_slot(void){
    if(allocation_spare!=ALLOCATION_NONE){unsigned i=allocation_spare;allocation_spare=allocations[i].next;return i;}
    if(allocation_count==ALLOCATION_CAPACITY)guest_trap(&cpu,heap,"bounded allocation registry exhausted");
    return allocation_count++;
}
static void allocation_recycle(unsigned i){allocations[i].size=0;allocations[i].used=0;allocations[i].next=allocation_spare;allocation_spare=i;}
static void allocation_unbin(unsigned i){
    GuestAllocation *a=&allocations[i];unsigned b=allocation_bin(a->size);
    if(a->free_previous==ALLOCATION_NONE)allocation_bins[b]=a->free_next;
    else allocations[a->free_previous].free_next=a->free_next;
    if(a->free_next!=ALLOCATION_NONE)allocations[a->free_next].free_previous=a->free_previous;
}
static void allocation_bin_add(unsigned i){
    GuestAllocation *a=&allocations[i];unsigned b=allocation_bin(a->size);
    a->free_previous=ALLOCATION_NONE;a->free_next=allocation_bins[b];
    if(a->free_next!=ALLOCATION_NONE)allocations[a->free_next].free_previous=i;
    allocation_bins[b]=i;
}
static unsigned allocation_index(uint32_t p){
    for(unsigned entry=allocation_hash[allocation_bucket(p)];entry;entry=allocations[entry-1].hash_next)
        if(allocations[entry-1].address==p&&allocations[entry-1].used)return entry-1;
    guest_trap(&cpu,p,"invalid or double free");return ALLOCATION_NONE;
}
static uint32_t allocate(uint32_t bytes){
    last_allocation_size=bytes;uint32_t size=(bytes+15)&~15u;
    if(size<bytes)guest_trap(&cpu,heap,"allocation size overflow");if(!size)size=16;
    unsigned block=ALLOCATION_NONE;
    for(unsigned b=allocation_bin(size);b<32&&block==ALLOCATION_NONE;b++)
        for(unsigned i=allocation_bins[b];i!=ALLOCATION_NONE;i=allocations[i].free_next)
            if(allocations[i].size>=size){block=i;break;}
    if(block!=ALLOCATION_NONE){
        allocation_unbin(block);GuestAllocation *a=&allocations[block];uint32_t remainder=a->size-size;
        if(remainder>=16&&(allocation_spare!=ALLOCATION_NONE||allocation_count<ALLOCATION_CAPACITY)){
            unsigned tail=allocation_slot();GuestAllocation *t=&allocations[tail];
            *t=(GuestAllocation){.address=a->address+size,.size=remainder,.previous=block,.next=a->next};
            if(t->next!=ALLOCATION_NONE)allocations[t->next].previous=tail;else allocation_tail=tail;
            a->next=tail;a->size=size;allocation_bin_add(tail);
        }
    }else{
        if((uint64_t)heap+size>STACK_TOP-0x200000)guest_trap(&cpu,heap,"diagnostic heap exhausted");
        block=allocation_slot();GuestAllocation *a=&allocations[block];
        *a=(GuestAllocation){.address=heap,.size=size,.previous=allocation_tail,.next=ALLOCATION_NONE};
        if(allocation_tail!=ALLOCATION_NONE)allocations[allocation_tail].next=block;
        allocation_tail=block;heap+=size;
    }
    GuestAllocation *a=&allocations[block];a->used=1;
    unsigned bucket=allocation_bucket(a->address);a->hash_next=allocation_hash[bucket];allocation_hash[bucket]=block+1;
    live_bytes+=a->size;if(live_bytes>peak_live_bytes)peak_live_bytes=live_bytes;
    memset(guest_pointer(a->address,size),0,size);return a->address;
}
static void release(uint32_t p){
    if(!p)return;unsigned block=allocation_index(p);GuestAllocation *a=&allocations[block];
    unsigned *entry=&allocation_hash[allocation_bucket(p)];
    while(*entry!=block+1)entry=&allocations[*entry-1].hash_next;
    *entry=a->hash_next;live_bytes-=a->size;a->used=0;
    unsigned previous=a->previous;
    if(previous!=ALLOCATION_NONE&&!allocations[previous].used){
        allocation_unbin(previous);GuestAllocation *left=&allocations[previous];
        left->size+=a->size;left->next=a->next;
        if(a->next!=ALLOCATION_NONE)allocations[a->next].previous=previous;else allocation_tail=previous;
        allocation_recycle(block);block=previous;a=left;
    }
    unsigned next=a->next;
    if(next!=ALLOCATION_NONE&&!allocations[next].used){
        allocation_unbin(next);GuestAllocation *right=&allocations[next];
        a->size+=right->size;a->next=right->next;
        if(a->next!=ALLOCATION_NONE)allocations[a->next].previous=block;else allocation_tail=block;
        allocation_recycle(next);
    }
    if(a->next==ALLOCATION_NONE){
        heap=a->address;allocation_tail=a->previous;
        if(allocation_tail!=ALLOCATION_NONE)allocations[allocation_tail].next=ALLOCATION_NONE;
        allocation_recycle(block);
    }else allocation_bin_add(block);
}
