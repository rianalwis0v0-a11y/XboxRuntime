/* MIT-licensed NV2A vertex microcode front-end.
 * Field layout follows the public Xbox NV2A vertex shader documentation.
 * This module emits validated HLSL source; device compilation/binding is a
 * separate backend responsibility. Unsupported encodings fail closed.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static unsigned bits(uint32_t v,unsigned s,unsigned n){return (v>>s)&((1u<<n)-1u);}
typedef struct { char *p; size_t cap,n; int bad; } Out;
static void put(Out *o,const char *fmt,unsigned a,unsigned b,unsigned c,unsigned d){
 if(o->bad)return; int n=snprintf(o->p+o->n,o->cap-o->n,fmt,a,b,c,d);
 if(n<0||(size_t)n>=o->cap-o->n)o->bad=1;else o->n+=(size_t)n;
}
static int source(const uint32_t *w,unsigned s,char *dst,size_t cap){
 unsigned mux,r,neg,sw;
 if(!s){mux=bits(w[2],26,2);r=bits(w[2],28,4);neg=bits(w[1],8,1);sw=bits(w[1],0,8);}
 else if(s==1){mux=bits(w[2],11,2);r=bits(w[2],13,4);neg=bits(w[2],25,1);sw=bits(w[2],17,8);}
 else {mux=bits(w[3],28,2);r=(bits(w[2],0,2)<<2)|bits(w[3],30,2);neg=bits(w[2],10,1);sw=bits(w[2],2,8);}
 char base[48];
 if(mux==1){if(r>12)return 0;snprintf(base,sizeof(base),r==12?"oPos":"r[%u]",r);}
 else if(mux==2)snprintf(base,sizeof(base),"v[%u]",bits(w[1],9,4));
 else if(mux==3)snprintf(base,sizeof(base),"c[%u]",bits(w[1],13,8));
 else return 0;
 return snprintf(dst,cap,"%s%s.%c%c%c%c",neg?"-":"",base,
  "xyzw"[sw>>6&3],"xyzw"[sw>>4&3],"xyzw"[sw>>2&3],"xyzw"[sw&3])<(int)cap;
}
static int emit_instruction(Out *o,const uint32_t *w,unsigned slot){
 unsigned mac=bits(w[1],21,4),ilu=bits(w[1],25,3);
 unsigned mm=bits(w[3],24,4),im=bits(w[3],16,4),om=bits(w[3],12,4);
 unsigned dst=bits(w[3],20,4),outreg=bits(w[3],3,8),outsel=bits(w[3],11,1),mux=bits(w[3],2,1);
 char a[128]="0",b[128]="0",c[128]="0";
 if(mac>13||ilu>7||mac==13||ilu>4)return 0; /* ARL/EXP/LOG/LIT await backend-specific validation. */
 if(mac&&!source(w,0,a,sizeof(a)))return 0;
 if((mac==2||mac==4||mac==5||mac==6||mac==7||mac==8||mac==9||mac==10||mac==11||mac==12)&&!source(w,1,b,sizeof(b)))return 0;
 if((ilu||mac==3||mac==4)&&!source(w,2,c,sizeof(c)))return 0;
 put(o,"  { float4 A%u=%s; float4 B%u=%s; float4 C%u=%s;\n",slot,0,0,0);
 /* Replace the compact declaration above with explicit temporaries. */
 if(o->bad)return 0;
 { int n=snprintf(o->p+o->n,o->cap-o->n,"    float4 A=%s, B=%s, C=%s;\n",a,b,c);
   if(n<0||(size_t)n>=o->cap-o->n)return 0;o->n+=(size_t)n; }
 static const char *m[]={"0","A","A*B","A+C","A*B+C","dot(A.xyz,B.xyz)","dot(A.xyz,B.xyz)+B.w","dot(A,B)","float4(1,A.y*B.y,A.z,B.w)","min(A,B)","max(A,B)","(A<B)","(A>=B)"};
 static const char *u[]={"0","C","rcp(C.x)","(sign(C.x)*min(abs(rcp(C.x)),3.402823e38))","rsqrt(abs(C.x))"};
 { int n=snprintf(o->p+o->n,o->cap-o->n,"    float4 M=%s, I=%s;\n",m[mac],u[ilu]);
   if(n<0||(size_t)n>=o->cap-o->n)return 0;o->n+=(size_t)n; }
 if(om){const char *v=mux?"I":"M";if(!outsel||outreg>12||outreg==1||outreg==2||outreg==7||outreg==8)return 0;
  int n=snprintf(o->p+o->n,o->cap-o->n,"    o[%u].%s=%s;\n",outreg,"xyzw",v);
  if(n<0||(size_t)n>=o->cap-o->n)return 0;o->n+=(size_t)n;}
 if(ilu&&im){int n=snprintf(o->p+o->n,o->cap-o->n,"    r[1]=I;\n");if(n<0||(size_t)n>=o->cap-o->n)return 0;o->n+=(size_t)n;}
 if(mac&&mm&&!(ilu&&dst==1)){int n=snprintf(o->p+o->n,o->cap-o->n,"    r[%u]=M;\n",dst);if(n<0||(size_t)n>=o->cap-o->n)return 0;o->n+=(size_t)n;}
 {int n=snprintf(o->p+o->n,o->cap-o->n,"  } // instruction %u\n",slot);if(n<0||(size_t)n>=o->cap-o->n)return 0;o->n+=(size_t)n;}
 return 1;
}
__declspec(dllexport) int __cdecl XboxNV2A_TranslateVertexProgram(
 const uint32_t *words,uint32_t count,char *hlsl,uint32_t capacity,uint32_t *used){
 if(used)*used=0;if(!words||!hlsl||capacity<256||!count||count>136)return 1;hlsl[0]=0;
 Out o={hlsl,capacity,0,0};
 #define APP(s) do{int z=snprintf(o.p+o.n,o.cap-o.n,"%s",s);if(z<0||(size_t)z>=o.cap-o.n)return 2;o.n+=(size_t)z;}while(0)
 APP("struct VIn { float4 v[16] : TEXCOORD0; };\nfloat4 main(VIn input) : POSITION {\n");
 APP("float4 r[12], o[13], v[16], c[192]; float a0=0; [unroll] for(int i=0;i<12;i++)r[i]=0; [unroll] for(int j=0;j<13;j++)o[j]=0; v=input.v;\n");
 int final=0;
 for(uint32_t i=0;i<count;i++){if(!emit_instruction(&o,words+i*4,i)){hlsl[0]=0;return 3;}if(words[i*4+3]&1u){final=1;break;}}
 if(!final){hlsl[0]=0;return 4;}
 APP("return o[0];\n}\n");
 if(used)*used=(uint32_t)o.n;return 0;
 #undef APP
}
