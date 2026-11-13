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
 (void)reg; /* The interpreter applies the hardware's temp-register aliasing. */
 if(mux==1){ /* Temp 12 aliases oPos; 13..15 read as R0 on NV2A. */ }
 else if(mux==2)m->input_mask|=(uint16_t)(1u<<field(w[1],9,4));
 else if(mux==3){
  unsigned c=field(w[1],13,8);
  if(c>=192)return 0;
  m->constant_mask[c>>6]|=(uint64_t)1<<(c&63);
  /* Word 3 bit 1 applies to the instruction's constant source, regardless of slot. */
  if(w[3]&2u)m->uses_relative_constants=1;
 } else return 0;
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
  /* The NV2A is a paired-issue machine: ADD reads A+C, while MUL and
   * the other two-input MAC operations read A+B. MAD and ILU also read C.
   * Do not decode an unused source: its mux bits may contain arbitrary data. */
  if(mac&&!decode_source(w,0,&m))return 4;
  switch(mac){
  case 2: case 5: case 6: case 7: case 8:
  case 9: case 10: case 11: case 12:
   if(!decode_source(w,1,&m))return 5;
   break;
  default: break;
  }
  if((mac==3||mac==4||ilu!=0)&&!decode_source(w,2,&m))return 6;
  if(((mac&&mm)||(ilu&&im))&&dst>12)return 7;
  /* When MAC and ILU target R1 in one slot, NV2A suppresses the MAC write. */
  if(om){
   /* NV2A result slots 1 and 2 are reserved; BFC0/BFC1 (7/8) are valid. */
   if(!field(w[3],11,1)||outreg>12||outreg==1||outreg==2)return 8;
   m.output_mask|=(uint16_t)(1u<<outreg);
  }
  m.instruction_count=i+1;
  if(w[3]&1u){final=1;break;}
 }
 if(!final)return 9;
 *out=m;return 0;
}
