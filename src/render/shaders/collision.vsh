; Collision viewer vertex shader: the ImGui vertex shader plus a third export,
; the clip-space position, so the pixel shader can look up the scene depth at
; the fragment it is about to write. ImGui's projection is orthographic with
; w = 1, so clip xy is already NDC.

; $MODE = "UniformRegister"

; $SPI_VS_OUT_CONFIG.VS_EXPORT_COUNT = 2
; $NUM_SPI_VS_OUT_ID = 1
; uv
; $SPI_VS_OUT_ID[0].SEMANTIC_0 = 0
; color
; $SPI_VS_OUT_ID[0].SEMANTIC_1 = 1
; screen position
; $SPI_VS_OUT_ID[0].SEMANTIC_2 = 2

; C0
; $UNIFORM_VARS[0].name = "ProjMtx"
; $UNIFORM_VARS[0].type = "mat4"
; $UNIFORM_VARS[0].count = 1
; $UNIFORM_VARS[0].block = -1
; $UNIFORM_VARS[0].offset = 0

; R1
; $ATTRIB_VARS[0].name = "Position"
; $ATTRIB_VARS[0].type = "vec2"
; $ATTRIB_VARS[0].location = 0
; R2
; $ATTRIB_VARS[1].name = "UV"
; $ATTRIB_VARS[1].type = "vec2"
; $ATTRIB_VARS[1].location = 1
; R3
; $ATTRIB_VARS[2].name = "Color"
; $ATTRIB_VARS[2].type = "vec4"
; $ATTRIB_VARS[2].location = 2

00 CALL_FS NO_BARRIER
01 ALU: ADDR(32) CNT(18)
    0  x: MUL    ____,   1.0f, C3.x
       y: MUL    ____,   1.0f, C3.y
       z: MUL    ____,   1.0f, C3.z
       w: MUL    ____,   1.0f, C3.w
    1  x: MULADD R127.x, R1.y, C1.x, PV0.x
       y: MULADD R127.y, R1.y, C1.y, PV0.y
       z: MULADD R127.z, R1.y, C1.z, PV0.z
       w: MULADD R127.w, R1.y, C1.w, PV0.w
    2  x: MULADD R1.x,   R1.x, C0.x, PV0.x
       y: MULADD R1.y,   R1.x, C0.y, PV0.y
       z: MULADD R1.z,   R1.x, C0.z, PV0.z
       w: MULADD R1.w,   R1.x, C0.w, PV0.w
    3  x: MULADD R4.x,   PV2.x, 0.5f, 0.5f
       y: MULADD R4.y,  -PV2.y, 0.5f, 0.5f
02 EXP_DONE: POS0, R1
03 EXP: PARAM0, R2.xy00 NO_BARRIER
04 EXP: PARAM1, R3 NO_BARRIER
05 EXP_DONE: PARAM2, R4.xy00 NO_BARRIER
END_OF_PROGRAM
