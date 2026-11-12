/* MIT-licensed NV2A vertex-program microcode validator and metadata decoder.
 * Field positions are from the public Xbox NV2A vertex-shader format reference.
 * This deliberately does not pretend to compile or execute shaders.
 */
#include <stdint.h>
#include <string.h>
#include "nv2a_vertex_shader.h"
static unsigned field(uint32_t v,unsigned shift,unsigned width){return (v>>shift)&((1u<<width)-1u);}
static int decode_source(const uint32_t *w,unsigned s,XboxNV2A_VERTEX_PROGRAM_INFO *m){
 unsigned mux,reg;
 if(s==0){mux=field(w[2],26,2);reg=field(w[2],28,4);}
 else if(s==1){mux=field(w[2],11,2);reg=field(w[2],13,4);}
 else {mux=field(w[3],28,2);reg=(field(w[2],0,2)<<2)|field(w[3],30,2);}
 if(mux==0)return 0;
 if(mux==1){if(reg>12)return 0;}
 else if(mux==2)m->input_mask|=(uint16_t)(1u<<field(w[1],9,4));
 else if(mux==3){unsigned c=field(w[1],13,8);if(c>=192)return 0;m->constant_mask[c>>6]|=(uint64_t)1<<(c&63);}
 else return 0;
 if(s==2 && mux==3 && (w[3]&2u))m->uses_relative_constants=1;
 return 1;
}
__declspec(dllexport) int __cdecl XboxNV2A_ValidateVertexProgram(
 const uint32_t *words,uint32_t count,XboxNV2A_VERTEX_PROGRAM_INFO *out){
 if(!words||!out||!count||count>136)return 1;
 XboxNV2A_VERTEX_PROGRAM_INFO m;memset(&m,0,sizeof(m));
 int final=0;
 for(uint32_t i=0;i<count;i++){
  const uint32_t *w=words+i*4;
  unsigned mac=field(w[1],21,4),ilu=field(w[1],25,3);
  unsigned mm=field(w[3],24,4),im=field(w[3],16,4),om=field(w[3],12,4);
  unsigned dst=field(w[3],20,4),outreg=field(w[3],3,8);
  if(mac>13||ilu>7)return 2;
  if(mac==13||ilu>=5)return 3; /* ARL/EXP/LOG/LIT need translator-specific handling. */
  if(mac&&!decode_source(w,0,&m))return 4;
  if((mac==2||mac==4||mac==5||mac==6||mac==7||mac==8||mac==9||mac==10||mac==11||mac==12)&&!decode_source(w,1,&m))return 5;
  if((ilu||mac==3||mac==4)&&!decode_source(w,2,&m))return 6;
  if(mac&&mm&&dst>12)return 7;
  if(ilu&&im&&mac&&dst==1){} /* Paired ILU owns R1; MAC R1 write is suppressed. */
  if(om){if(!field(w[3],11,1)||outreg>12||outreg==1||outreg==2||outreg==7||outreg==8)return 8;m.output_mask|=(uint16_t)(1u<<outreg);}
  m.instruction_count=i+1;
  if(w[3]&1u){final=1;break;}
 }
 if(!final)return 9;
 *out=m;return 0;
}
