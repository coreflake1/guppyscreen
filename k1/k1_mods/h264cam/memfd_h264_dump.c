/* Stream cam_app's H.264 from the double-buffered memfd, glitch-free via a
 * stability double-read: copy frame, wait, copy again; emit only if identical
 * (rejects frames caught mid-write). Primes SPS/PPS at start. seconds<=0=forever */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>

static const unsigned char* find_last_nal(const unsigned char*b,size_t len,int type,size_t*ol){
    const unsigned char*best=0; size_t bl=0;
    for(size_t i=0;i+5<len;i++) if(b[i]==0&&b[i+1]==0&&b[i+2]==0&&b[i+3]==1&&(b[i+4]&0x1f)==type){
        size_t j=i+4; while(j+4<len&&!(b[j]==0&&b[j+1]==0&&b[j+2]==0&&b[j+3]==1)) j++;
        best=b+i; bl=j-i;
    }
    *ol=bl; return best;
}
int main(int argc,char**argv){
    if(argc<3){fprintf(stderr,"usage: %s <memfd> <out|-> [seconds]\n",argv[0]);return 2;}
    int secs=argc>3?atoi(argv[3]):0;
    int fd=open(argv[1],O_RDONLY); if(fd<0){perror("open");return 1;}
    struct stat st; size_t sz=0; if(fstat(fd,&st)==0) sz=st.st_size; if(!sz) sz=8u<<20;
    volatile unsigned char*buf=mmap(0,sz,PROT_READ,MAP_SHARED,fd,0);
    if(buf==MAP_FAILED){perror("mmap");return 1;}
    FILE*out=(argv[2][0]=='-'&&!argv[2][1])?stdout:fopen(argv[2],"wb");
    if(!out){perror("fopen");return 1;}
    size_t sl=0,pl=0;
    const unsigned char*sps=find_last_nal((const unsigned char*)buf,sz,7,&sl);
    const unsigned char*pps=find_last_nal((const unsigned char*)buf,sz,8,&pl);
    if(sps&&sl)fwrite(sps,1,sl,out); if(pps&&pl)fwrite(pps,1,pl,out); fflush(out);
    fprintf(stderr,"primed SPS=%zu PPS=%zu\n",sl,pl);
    unsigned char*a=malloc(sz),*b=malloc(sz); uint32_t last=0xffffffffu; long frames=0,retry=0; time_t t0=time(NULL);
    for(;;){
        if(secs>0&&time(NULL)-t0>=secs)break;
        uint32_t off=*(volatile uint32_t*)(buf+16);
        if(off==last||(size_t)off+4>=sz){usleep(120);continue;}
        uint32_t fsz=*(volatile uint32_t*)(buf+off);
        if(fsz==0||fsz>=sz||(size_t)off+4+fsz>sz){usleep(120);continue;}
        volatile unsigned char*p=buf+off+4;
        if(!(p[0]==0&&p[1]==0&&p[2]==0&&p[3]==1)){usleep(100);continue;}
        memcpy(a,(const void*)p,fsz);
        usleep(1500);                                   /* let any in-progress write finish */
        if(*(volatile uint32_t*)(buf+16)!=off){retry++;continue;}  /* flipped */
        if(*(volatile uint32_t*)(buf+off)!=fsz){retry++;continue;} /* size changed */
        memcpy(b,(const void*)p,fsz);
        if(memcmp(a,b,fsz)!=0){retry++;continue;}        /* still being written */
        fwrite(b,1,fsz,out); fflush(out); frames++; last=off;
        usleep(120);
    }
    if(out!=stdout){fprintf(stderr,"wrote %ld frames (%ld retries)\n",frames,retry);fclose(out);}
    return 0;
}
