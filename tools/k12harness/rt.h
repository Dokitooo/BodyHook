/* minimal 32-bit linux runtime for running decrypted nba2k12 code in a harness */
typedef unsigned int u32; typedef unsigned char u8; typedef unsigned short u16;
static int sys6(int n,int a,int b,int c,int d,int e,int f){int r;__asm__ volatile("push %%ebp; mov %7,%%ebp; int $0x80; pop %%ebp":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"S"(d),"D"(e),"m"(f):"memory");return r;}
#define sys3(n,a,b,c) sys6(n,a,b,c,0,0,0)
static void out(const char*s){int n=0;while(s[n])n++;sys3(4,1,(int)s,n);}
static void hex(u32 v,int w){char b[9];int i;for(i=w-1;i>=0;i--){b[i]="0123456789ABCDEF"[v&15];v>>=4;}b[w]=0;out(b);}
static void dec(int v){char b[12];int i=11;b[i]=0;if(v<0){out("-");v=-v;}do{b[--i]='0'+v%10;v/=10;}while(v);out(b+i);}
static void *memset(void*d,int c,u32 n){u8*p=d;while(n--)*p++=c;return d;}
static void *memcpy(void*d,const void*s,u32 n){u8*p=d;const u8*q=s;while(n--)*p++=*q++;return d;}
static int load_img(const char *path){
  int fd=sys3(5,(int)path,0,0); u32 got=0; int r;
  if(fd<0){out("open fail\n");return 0;}
  /* mmap2: addr, len, prot rwx=7, MAP_PRIVATE|MAP_FIXED|MAP_ANON=0x32, fd -1, off */
  r=sys6(192,0x400000,0x19ED000,7,0x32,-1,0); if(r!=0x400000){out("mmap fail\n");return 0;}
  while((r=sys3(3,fd,0x400000+got,0x100000))>0) got+=r;
  out("image loaded "); hex(got,8); out("\n"); return 1;
}
