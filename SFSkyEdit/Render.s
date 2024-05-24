;
; SFskyedit - Star Fighter 3000 sky colours editor
; Sky renderer
; Copyright (C) 2001  Christopher Bazley
;
; This program is free software; you can redistribute it and/or modify
; it under the terms of the GNU General Public Licence as published by
; the Free Software Foundation; either version 2 of the Licence, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public Licence for more details.
;
; You should have received a copy of the GNU General Public Licence
; along with this program; if not, write to the Free Software
; Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
;

; Original version by Fednet Software for Star Fighter 3000
; 13.07.01 CJB Modified for APCS-32 compliance
; 16.10.03 CJB Tweaked stack check to check for -ve rather than signed lower
; 21.09.09 CJB Added star plotting routine

; Area name C$$code advisable if wanted to link with C output

        AREA    |C$$code|, CODE, READONLY

        EXPORT  |sky_drawsky|
        EXPORT  |star_plot|
        IMPORT  |__rt_stkovf_split_small|

; Routine to draw sky at variable height with variable shading
; Ver.4 With vertical clipping end (Height based) end-of-table check
; + Star start height at file +4

; C prototype :
;   void sky_drawsky(int    height_scaler,
;                    SFSky *sky,
;                    void  *screen_address,
;                    int    scrstart_offset);

; a1 = height scaling number
; a2 = address of a sky file to use
; a3 = Start of screen address
; a4 = offset from start of screen

; File +0 = Min height of sky
; File +4 = Start height of star plot
;      +8+= Sky data
; -----------------------------------------------------------------------

|sky_drawsky|
  ; Create stack backtrace structure
  MOV    ip, sp ; save current sp, ready to save as old sp
  STMFD  sp!, {v1-v5, fp, ip, lr, pc}  ; as needed
  SUB    fp, ip, #4 ; points to saved pc

  CMP   sp, sl                        ; Stack limit checking
    BLMI   |__rt_stkovf_split_small|

  MOV lr,a1,ASR#7
  ADD lr,lr,lr,ASR#1
  ADD lr,lr,#48               ; calculate height-related cap for colour index
  CMP lr,#255
    MOVGT lr,#255             ; (48 to 255)

  LDR v1,[a2],#8
  ADD a1,a1,v1                ; Add initial height offset

  MOV v1,#0                   ; Current colour lookup

  CMP a4,#81920
    BGE sky_clipsky           ; Clip for bottom of screen

  ADD a4,a3,a4                ; screen (right) store location
  CMP a4,a3
    LDMLEEA  fp, {v1-v5, fp, sp, pc} ; Clip for top of screen

sky_dolineagain
  MOV v3,v1, ASR #10
  ;TST a4,#2_100000
  ;  SUBEQ v3,v3,#1     ; Don't understand this, and it is counter-productive

  CMP v3,lr
    LDRGT v2,[a2,lr,ASL#2] ; cap colour lookup to R14 (height related)
    LDRLE v2,[a2,v3,ASL#2] ; read word to plot

  MOV v3,v2 ; duplicate across four words (16 pixels)
  MOV v4,v2
  MOV v5,v2

  ; Store line backwards

  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}

  CMP a4,a3
    LDMLEEA  fp, {v1-v5, fp, sp, pc}

  ; Line is complete - now shift up colour bands

  ADD v1,v1,a1         ; the higher you are, the narrower the colour bands
  SUBS a1,a1,a1,ASR#5  ; the later bands also change slower

  MOV v3,v1, ASR #10
  ;TST a4,#2_100000
  ;  SUBEQ v3,v3,#1   ; Don't understand this, and it is counter-productive

  CMP v3,lr
    LDRGT v2,[a2,lr,ASL#2] ; cap colour lookup to R14 (height related)
    LDRLE v2,[a2,v3,ASL#2] ; read word to plot
  MOV v2,v2,ROR #8 ; prevent stripes in dithering

  MOV v3,v2  ; duplicate across four words (16 pixels)
  MOV v4,v2
  MOV v5,v2

  ; Store line backwards

  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}
  STMDA a4!,{v2-v5}

  ; Line is complete - now shift up colour bands
  ADD v1,v1,a1,ASL#1
  SUBS a1,a1,a1,ASR#4

  CMP a4,a3
    BGT sky_dolineagain

  LDMEA  fp, {v1-v5, fp, sp, pc}

  ; -----------------------------------------------------------------------

sky_clipsky                      ; Clip sky for bottom of screen
  ADD a4,a3,a4        ; screen (right) store location
  ADD v2,a3,#81920    ; end of frame buffer (exclusive)

sky_clipskyagain
  ; Line is complete - now shift up colour bands

  ADD v1,v1,a1        ; the higher you are, the narrower the colour bands
  SUBS a1,a1,a1,ASR#5 ; the later bands also change slower

  SUB a4,a4,#320      ; calculate same horizontal position on line above
  CMP a4,v2
    BLT sky_dolineagain ; start plotting upon passing the frame buffer end

  ; Line is complete - now shift up colour bands
  ADD v1,v1,a1,ASL#1
  SUBS a1,a1,a1,ASR#4

  SUB a4,a4,#320      ; calculate same horizontal position on line above
  CMP a4,v2
    BGE sky_clipskyagain ; still beyond the end of the frame buffer
  B sky_dolineagain   ; start plotting upon passing the frame buffer end

; -----------------------------------------------------------------------

; C prototype :
;   void star_plot(int           height,
;                  void         *screen_address,
;                  int  x,
;                  int  y,
;                  int  colour,
;                  int           bright,
;                  int           size);

; a1 = Height (0-8191 Shade, >=8192 Full mask)
; a2 = Screenstart
; a3 = X offset
; a4 = Y offset
; sp   -> Star colour
; sp+4 -> Star height adder
; sp+8 -> Max star size
; -----------------------------------------------------------------------

|star_plot|

; CALCULATE SCREEN POSITION, AND BAIL IF OFF SCREEN

  CMP a3,#1
    CMPGT a4,#1
    MOVLE pc,lr      ; Cannot plot, Too far up / left

  CMP a3,#316
    CMPLT a4,#254
    MOVGE pc,lr      ; Cannot plot, Too far down / right

  LDR ip,[sp,#4] ; get star height adder

  ADD a2,a2,a3       ; Add on X
  ADD a2,a2,a4,ASL#8 ; Add on Y
  ADD a2,a2,a4,ASL#6 ; Add on Y

; -----------------------------------------------------------------------

; CALCULATE STAR SHADE

  ADD a1,a1,ip
  SUBS a1,a1,#4096
    MOVMI pc,lr      ; Bail if -ve

  LDR ip,[sp,#8]     ; get max star size
  MOV a1,a1,ASR#9    ; Shifted shade value
  CMP a1,ip          ; Clip to max value
    MOVGT a1,ip

  ADR a3,star_colours

; -----------------------------------------------------------------------

  LDRB a3,[a3,a1]  ; Get colour (MAX)
  LDRB a4,[a2]
  ORR a4,a4,a3
  STRB a4,[a2]             ; Point 1

; -----------------------------------------------------------------------

  SUBS a1,a1,#4
    MOVMI pc,lr

  ADR a3,star_colours

  LDR ip,[sp,#0] ; get star colour

  LDRB a3,[a3,a1]  ; Get colour (MED)
  AND a3,a3,ip

  LDRB a4,[a2,#-1]
  ORR a4,a4,a3
  STRB a4,[a2,#-1]    ; Point 2

  LDRB a4,[a2,#+1]
  ORR a4,a4,a3
  STRB a4,[a2,#+1]

  LDRB a4,[a2,#-320]
  ORR a4,a4,a3
  STRB a4,[a2,#-320]

  LDRB a4,[a2,#+320]
  ORR a4,a4,a3
  STRB a4,[a2,#+320]

; -----------------------------------------------------------------------

  SUBS a1,a1,#4
    MOVMI pc,lr

  ADR a3,star_colours

  LDRB a3,[a3,a1]  ; Get colour (LOW)
  AND a3,a3,ip

  LDRB a4,[a2,#-2]
  ORR a4,a4,a3
  STRB a4,[a2,#-2]    ; Point 3

  LDRB a4,[a2,#+2]
  ORR a4,a4,a3
  STRB a4,[a2,#+2]

  LDRB a4,[a2,#-640]
  ORR a4,a4,a3
  STRB a4,[a2,#-640]

  LDRB a4,[a2,#+640]
  ORR a4,a4,a3
  STRB a4,[a2,#+640]

  MOV pc,lr

; -----------------------------------------------------------------------
|star_colours|
  DCB 2_00000000
  DCB 2_00000001
  DCB 2_00000010
  DCB 2_00000011

  DCB 2_00101100
  DCB 2_00101101
  DCB 2_00101110
  DCB 2_00101111

  DCB 2_11010000
  DCB 2_11010001
  DCB 2_11010010
  DCB 2_11010011

  DCB 2_11111100
  DCB 2_11111101
  DCB 2_11111110
  DCB 2_11111111

  END
