## Auto-generated file, transpiled with RSPL
#define TILE_W 16
#define TILE_H 16
#define TILE_NUM_PIXELS 256
#define TILE_NUM_BYTES 512
#include <rsp_queue.inc>

.set noreorder
.set noat
.set nomacro

#undef zero
#undef at
#undef v0
#undef v1
#undef a0
#undef a1
#undef a2
#undef a3
#undef t0
#undef t1
#undef t2
#undef t3
#undef t4
#undef t5
#undef t6
#undef t7
#undef s0
#undef s1
#undef s2
#undef s3
#undef s4
#undef s5
#undef s6
#undef s7
#undef t8
#undef t9
#undef k0
#undef k1
#undef gp
#undef sp
#undef fp
#undef ra
.equ hex.$zero, 0
.equ hex.$at, 1
.equ hex.$v0, 2
.equ hex.$v1, 3
.equ hex.$a0, 4
.equ hex.$a1, 5
.equ hex.$a2, 6
.equ hex.$a3, 7
.equ hex.$t0, 8
.equ hex.$t1, 9
.equ hex.$t2, 10
.equ hex.$t3, 11
.equ hex.$t4, 12
.equ hex.$t5, 13
.equ hex.$t6, 14
.equ hex.$t7, 15
.equ hex.$s0, 16
.equ hex.$s1, 17
.equ hex.$s2, 18
.equ hex.$s3, 19
.equ hex.$s4, 20
.equ hex.$s5, 21
.equ hex.$s6, 22
.equ hex.$s7, 23
.equ hex.$t8, 24
.equ hex.$t9, 25
.equ hex.$k0, 26
.equ hex.$k1, 27
.equ hex.$gp, 28
.equ hex.$sp, 29
.equ hex.$fp, 30
.equ hex.$ra, 31
#define vco 0
#define vcc 1
#define vce 2

.data
  RSPQ_BeginOverlayHeader
    RSPQ_DefineCommand FillCmd_SetScreenSize, 12
    RSPQ_DefineCommand FillCmd_DrawConstant, 12
  RSPQ_EndOverlayHeader

  RSPQ_BeginSavedState
    .align 3
    SCREEN_SIZE_VEC: .ds.b 16
    .align 1
    SCREEN_STRIDE: .ds.b 2
    .align 4
    TILE_COLORS: .ds.b 512
  RSPQ_EndSavedState

.text

FillCmd_SetScreenSize:
  srl $s7, $a1, 16
  andi $s6, $a1, 65535
  mtc2 $s7, $v28.e0
  mtc2 $s6, $v28.e1
  ori $at, $zero, %lo(SCREEN_SIZE_VEC)
  sw $a2, %lo(SCREEN_STRIDE)($zero)
  j RSPQ_Loop
  sqv $v28, 0, 0, $at
FillCmd_DrawConstant:
  or $s0, $zero, $a1
  ori $s4, $zero, %lo(TILE_COLORS)
  li $t0, DMA_SIZE(32,16) # inline-ASM
  lw $t1, %lo(SCREEN_STRIDE + 0)
  mtc2 $a2, $v28.e0
  vor $v28, $v00, $v28.e0
  ori $s7, $zero, %lo(TILE_COLORS)
  addiu $s6, $zero, 16
  LABEL_0001:
  sltu $at, $zero, $s6
  beq $at, $zero, LABEL_0002
  nop
  sqv $v28, 0, 0, $s7
  sqv $v28, 0, 16, $s7
  addiu $s6, $s6, 65535
  j LABEL_0001
  addiu $s7, $s7, 32
  LABEL_0002:
  jal DMAOut
  nop
  j RSPQ_Loop
  nop

#define zero $0
#define v0 $2
#define v1 $3
#define a0 $4
#define a1 $5
#define a2 $6
#define a3 $7
#define t0 $8
#define t1 $9
#define t2 $10
#define t3 $11
#define t4 $12
#define t5 $13
#define t6 $14
#define t7 $15
#define s0 $16
#define s1 $17
#define s2 $18
#define s3 $19
#define s4 $20
#define s5 $21
#define s6 $22
#define s7 $23
#define t8 $24
#define t9 $25
#define k0 $26
#define k1 $27
#define gp $28
#define sp $29
#define fp $30
#define ra $31

.set at
.set macro