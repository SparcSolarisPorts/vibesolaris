/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <limits.h>
#include <string.h>

#if UINT_MAX != 0xffffffffU
#error VibeSolaris SHA-256 requires a 32-bit unsigned int
#endif

typedef unsigned int U32;
typedef struct {
    U32 h[8];
    unsigned char block[64];
    size_t used;
    U32 total_lo;
    U32 total_hi;
} SHA256Ctx;

static U32 rotr(U32 x, unsigned n) { return (x >> n) | (x << (32U - n)); }
static U32 ch(U32 x,U32 y,U32 z){return (x&y)^((~x)&z);}
static U32 maj(U32 x,U32 y,U32 z){return (x&y)^(x&z)^(y&z);}
static U32 bs0(U32 x){return rotr(x,2)^rotr(x,13)^rotr(x,22);}
static U32 bs1(U32 x){return rotr(x,6)^rotr(x,11)^rotr(x,25);}
static U32 ss0(U32 x){return rotr(x,7)^rotr(x,18)^(x>>3);}
static U32 ss1(U32 x){return rotr(x,17)^rotr(x,19)^(x>>10);}

static const U32 k[64] = {
0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};

static void transform(SHA256Ctx *c,const unsigned char *p)
{
    U32 w[64],a,b,d,e,f,g,h,t1,t2,cc;
    int i;
    for(i=0;i<16;i++) w[i]=((U32)p[i*4]<<24)|((U32)p[i*4+1]<<16)|((U32)p[i*4+2]<<8)|(U32)p[i*4+3];
    for(i=16;i<64;i++) w[i]=ss1(w[i-2])+w[i-7]+ss0(w[i-15])+w[i-16];
    a=c->h[0];b=c->h[1];cc=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];
    for(i=0;i<64;i++){
        t1=h+bs1(e)+ch(e,f,g)+k[i]+w[i];
        t2=bs0(a)+maj(a,b,cc);
        h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}

static void init(SHA256Ctx *c)
{
    memset(c,0,sizeof(*c));
    c->h[0]=0x6a09e667U;c->h[1]=0xbb67ae85U;c->h[2]=0x3c6ef372U;c->h[3]=0xa54ff53aU;
    c->h[4]=0x510e527fU;c->h[5]=0x9b05688cU;c->h[6]=0x1f83d9abU;c->h[7]=0x5be0cd19U;
}

static void update(SHA256Ctx *c,const unsigned char *p,size_t n)
{
    size_t take;
    U32 old=c->total_lo;
    c->total_lo += (U32)n;
    if(c->total_lo<old)c->total_hi++;
    while(n){
        take=64-c->used;if(take>n)take=n;
        memcpy(c->block+c->used,p,take);c->used+=take;p+=take;n-=take;
        if(c->used==64){transform(c,c->block);c->used=0;}
    }
}

static void finish(SHA256Ctx *c,unsigned char out[32])
{
    unsigned char pad[128];
    unsigned char lenb[8];
    U32 bytes_lo=c->total_lo, bytes_hi=c->total_hi;
    U32 bit_lo=(bytes_lo<<3);
    U32 bit_hi=(bytes_hi<<3)|(bytes_lo>>29);
    size_t padn;
    int i;
    memset(pad,0,sizeof(pad));pad[0]=0x80;
    padn=(c->used<56)?(56-c->used):(120-c->used);
    update(c,pad,padn);
    for(i=0;i<4;i++)lenb[3-i]=(unsigned char)(bit_hi>>(i*8));
    for(i=0;i<4;i++)lenb[7-i]=(unsigned char)(bit_lo>>(i*8));
    update(c,lenb,8);
    for(i=0;i<8;i++){
        out[i*4]=(unsigned char)(c->h[i]>>24);out[i*4+1]=(unsigned char)(c->h[i]>>16);
        out[i*4+2]=(unsigned char)(c->h[i]>>8);out[i*4+3]=(unsigned char)c->h[i];
    }
}

void vs_sha256(const unsigned char *data,size_t len,unsigned char out[32])
{
    SHA256Ctx c;init(&c);update(&c,data,len);finish(&c,out);
}
