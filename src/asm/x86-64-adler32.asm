;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Copyright (C) 2023, jpn
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
; http://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


default rel


; for better column aligment
%define xmmA xmm10
%define xmmB xmm11
%define xmmC xmm12
%define xmmD xmm13
%define xmmE xmm14
%define xmmF xmm15


%ifidn __?OUTPUT_FORMAT?__, elf64
	%define SYSTEMV64
%endif
%ifidn __?OUTPUT_FORMAT?__, macho64
	%define SYSTEMV64
%endif

%ifndef SYSTEMV64
	%ifidn __?OUTPUT_FORMAT?__, win64
		%define WINDOWS64
	%else
		%error ABI not supported.
	%endif
%endif


section .text
align 16


global zstrm_adler32updateASM
; Parameters:
; adler32, buffer (pointer), size


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Initialize the jump table according to the CPU capabilities
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

SSSE3_FLAG equ 0000200h


initjump:
	; preserve registers
	push		rcx
	push		rdx
	push		rbx

	xor			rax, rax
	cmp			rax, qword[initdone]
	jne .done

	mov			eax, 1
	cpuid
	and			ecx, SSSE3_FLAG
	jz .nossse3

	lea			rax, [ssse3_adler32update]
	mov			qword[jumptable.update], rax
	jmp .done

.nossse3:
	lea			rax, [sse2_adler32update]
	mov			qword[jumptable.update], rax

.done:
	lea			rax, [initdone]
	mov			qword[rax], 1h

	pop			rbx
	pop			rdx
	pop			rcx


zstrm_adler32updateASM:
	jmp qword[jumptable.update]


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; SSE2 version
; Ported from https://github.com/mcountryman/simd-adler32
;
; See also:
; https://wooo.sh/articles/adler32.html
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

align 16

c3:
	dw 1, 1, 1, 1, 1, 1, 1, 1

c1lo: dw 0x10, 0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09
c1hi: dw 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01

c2lo: dw 0x20, 0x1f, 0x1e, 0x1d, 0x1c, 0x1b, 0x1a, 0x19
c2hi: dw 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11


; systemv x64: rdi=adler32, rsi=buffer, rdx=size
; windows x64: rcx=adler32, rdx=buffer, r8
sse2_adler32update:
	push		rbx
%ifdef WINDOWS64
	push		rsi
	mov 		rsi, rdx
	mov 		rdx, r8
%endif
	; rsi=buffer, rdx=size

%ifdef WINDOWS64
	mov			eax, ecx  ; eax=a
	mov			ebx, ecx  ; ebx=b
%endif
%ifdef SYSTEMV64
	mov			eax, edi  ; eax=a
	mov			ebx, edi  ; ebx=b
%endif

	and			eax, 0xffff
	shr			ebx, 0x10

	; number of blocks (size div 512)
	mov			 r8, rdx
	shr			 r8, 9
	jz .loop3

%ifdef WINDOWS64
	; preserve xmm registers xmm6, xmm7, xmm8 and xmm9
	sub			rsp, 48h
	movdqa		[rsp+0x00], xmm6
	movdqa		[rsp+0x10], xmm7
	movdqa		[rsp+0x20], xmm8
	movdqa		[rsp+0x30], xmm9
%endif

	mov			 r9, r8
	shl			 r9, 9
	sub			 rdx, r9
	push		 rdx

	; load constants
	pxor		xmm0, xmm0
	movdqa		xmm3, [c3]

.loop1:
	test		r8, r8
	jz .done1

	mov			r10d, eax  ; prev a
	mov			r11d, ebx  ; prev b
	mov			rdx, 16

	; init the xmm registers for a, b and the accumulator
	pxor		xmm4, xmm4  ; a
	pxor		xmm5, xmm5  ; b
	pxor		xmm6, xmm6  ; accumulator

	; inner loop
.loop2:
	test		rdx, rdx
	jz .done2

	; implemented as:
	; p = a
	; a += [0] + [1] + ... + [n]
	; b += n*p + n*[0] + (n-1)*[1] + ... + (n-n)*[n]

	movdqu		xmm7, [rsi+0x00]
	movdqu		xmm8, [rsi+0x10]

	movdqa		xmm9, xmm7
	movdqa		xmm2, xmm8
	paddd		xmm6, xmm4  ; accumulator + a

	psadbw		xmm9, xmm0  ; _mm_sad_epu8
	psadbw		xmm2, xmm0  ; _mm_sad_epu8

	movdqa		xmm1, xmm7
	punpcklbw	xmm1, xmm0
	pmaddwd		xmm1, [c2lo]

	punpckhbw	xmm7, xmm0
	pmaddwd		xmm7, [c2hi]
	paddd		xmm7, xmm1

	movdqa		xmm1, xmm8
	punpcklbw	xmm1, xmm0
	pmaddwd		xmm1, [c1lo]

	punpckhbw	xmm8, xmm0
	pmaddwd		xmm8, [c1hi]
	paddd		xmm8, xmm1

	paddd		xmm4, xmm9
	paddd		xmm4, xmm2

	pmaddwd		xmm7, xmm3  ; _mm_madd_epi16
	pmaddwd		xmm8, xmm3  ; _mm_madd_epi16
	paddd		xmm5, xmm7
	paddd		xmm5, xmm8

	add			rsi, 0x20
	dec			rdx
	jmp .loop2

.done2:
	pslld		xmm6, 5
	paddd		xmm5, xmm6

	; reduce a
	movdqa		xmm6, xmm4
	punpckhqdq	xmm6, xmm6
	paddd		xmm6, xmm4
	pshufd		xmm4, xmm6, 0xb1
	paddd		xmm4, xmm6

	; reduce b
	movdqa		xmm6, xmm5
	punpckhqdq	xmm6, xmm6
	paddd		xmm6, xmm5
	pshufd		xmm5, xmm6, 0xb1
	paddd		xmm5, xmm6

	movd		eax, xmm4
	movd		ebx, xmm5

	add			 eax, r10d
	shl			r10d, 9
	add			 ebx, r11d
	add			 ebx, r10d

	; modulo reduction
	mov			 r10d, eax
	mov			 r11d, ebx
	shr			 r10d, 16
	shr			 r11d, 16
	and			 eax, 0xffff
	and			 ebx, 0xffff

	mov			 ecx, r10d
	shl			 ecx, 4
	sub			 ecx, r10d
	add			 eax, ecx

	mov			 ecx, r11d
	shl			 ecx, 4
	sub			 ecx, r11d
	add			 ebx, ecx

	dec			r8
	jmp .loop1

.done1:
	pop			rdx

%ifdef WINDOWS64
	; restore xmm registers
	movdqa		xmm6, [rsp+0x00]
	movdqa		xmm7, [rsp+0x10]
	movdqa		xmm8, [rsp+0x20]
	movdqa		xmm9, [rsp+0x30]
	add			rsp, 48h
%endif

.loop3:
	cmp			rdx, 16
	jb .loop4

	%assign counter 0
	%rep 4
		movzx		 r8d, byte[rsi+0+counter*4]
		movzx		 r9d, byte[rsi+1+counter*4]
		movzx		r10d, byte[rsi+2+counter*4]
		movzx		r11d, byte[rsi+3+counter*4]
		add			eax, r8d
		add			ebx, eax
		add			eax, r9d
		add			ebx, eax
		add			eax, r10d
		add			ebx, eax
		add			eax, r11d
		add			ebx, eax
		%assign counter counter+1
	%endrep

	sub			rdx, 16
	add			rsi, 16
	jmp .loop3

.loop4:
	test		rdx, rdx
	jz .done

	movzx		r8d, byte[rsi]
	add			eax, r8d
	add			ebx, eax
	dec			rdx
	inc			rsi
	jmp .loop4

.done:
	; modulo reduction
	mov			 r8d, eax
	shr			 r8d, 0x10
	mov			r10d, r8d
	and			 eax, 0xffff
	shl			r10d, 4
	sub			r10d, r8d
	add			 eax, r10d
	cmp			 eax, 65521
	jb .r2
	sub			 eax, 65521

.r2:
	mov			 r8d, ebx
	shr			 r8d, 0x10
	mov			r10d, r8d
	and			 ebx, 0xffff
	shl			r10d, 4
	sub			r10d, r8d
	add			 ebx, r10d
	cmp			 ebx, 65521
	jb	.r3
	sub			 ebx, 65521

.r3:
	; combine a and b
	shl			ebx, 0x10
	or			eax, ebx

%ifdef WINDOWS64
	pop			rsi
%endif
	pop			rbx
	ret


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; SSSE3 version
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

align 16
c1:
	db 0x10, 0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09
	db 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01
c2:
	db 0x20, 0x1f, 0x1e, 0x1d, 0x1c, 0x1b, 0x1a, 0x19
	db 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11


; systemv x64: rdi=adler32, rsi=buffer, rdx=size
; windows x64: rcx=adler32, rdx=buffer, r8
ssse3_adler32update:
	push		rbx
%ifdef WINDOWS64
	push		rsi
	mov 		rsi, rdx
	mov 		rdx, r8
%endif
	; rsi=buffer, rdx=size

%ifdef WINDOWS64
	mov			eax, ecx  ; eax=a
	mov			ebx, ecx  ; ebx=b
%endif
%ifdef SYSTEMV64
	mov			eax, edi  ; eax=a
	mov			ebx, edi  ; ebx=b
%endif

	and			eax, 0xffff
	shr			ebx, 0x10

	; number of blocks (size div 512)
	mov			 r8, rdx
	shr			 r8, 9
	jz .loop3

%ifdef WINDOWS64
	; preserve xmm registers xmm6, xmm7, xmm8 and xmm9, xmmA
	sub			rsp, 58h
	movdqa		[rsp+0x00], xmm6
	movdqa		[rsp+0x10], xmm7
	movdqa		[rsp+0x20], xmm8
	movdqa		[rsp+0x30], xmm9
	movdqa		[rsp+0x40], xmmA
%endif

	mov			 r9, r8
	shl			 r9, 9
	sub			 rdx, r9
	push		 rdx

	; load constants
	pxor		xmm0, xmm0
	movdqa		xmm1, [c1]
	movdqa		xmm2, [c2]
	movdqa		xmm3, [c3]

.loop1:
	test		r8, r8
	jz .done1

	mov			r10d, eax  ; prev a
	mov			r11d, ebx  ; prev b
	mov			rdx, 16

	; init the xmm registers for a, b and the accumulator
	pxor		xmm4, xmm4  ; a
	pxor		xmm5, xmm5  ; b
	pxor		xmm6, xmm6  ; accumulator

	; inner loop
.loop2:
	test		rdx, rdx
	jz .done2

	; implemented as:
	; p = a
	; a += [0] + [1] + ... + [n]
	; b += n*p + n*[0] + (n-1)*[1] + ... + (n-n)*[n]

	movdqu		xmm7, [rsi+0x00]
	movdqu		xmm8, [rsi+0x10]

	movdqa		xmm9, xmm7
	movdqa		xmmA, xmm8
	paddd		xmm6, xmm4  ; accumulator + a

	psadbw		xmm9, xmm0  ; _mm_sad_epu8
	psadbw		xmmA, xmm0  ; _mm_sad_epu8

	pmaddubsw	xmm7, xmm2  ; _mm_maddubs_epi16
	pmaddubsw	xmm8, xmm1  ; _mm_maddubs_epi16

	paddd		xmm4, xmm9
	paddd		xmm4, xmmA

	pmaddwd		xmm7, xmm3  ; _mm_madd_epi16
	pmaddwd		xmm8, xmm3  ; _mm_madd_epi16
	paddd		xmm5, xmm7
	paddd		xmm5, xmm8

	add			rsi, 0x20
	dec			rdx
	jmp .loop2

.done2:
	pslld		xmm6, 5
	paddd		xmm5, xmm6

	phaddd		xmm4, xmm5  ; _mm_hadd_epi32
	phaddd		xmm4, xmm0  ; _mm_hadd_epi32
	movdqa		xmm5, xmm4
	psrldq		xmm4, 4

	movd		eax, xmm5
	movd		ebx, xmm4

	add			 eax, r10d
	shl			r10d, 9
	add			 ebx, r11d
	add			 ebx, r10d

	; modulo reduction
	mov			 r10d, eax
	mov			 r11d, ebx
	shr			 r10d, 16
	shr			 r11d, 16
	and			 eax, 0xffff
	and			 ebx, 0xffff

	mov			 ecx, r10d
	shl			 ecx, 4
	sub			 ecx, r10d
	add			 eax, ecx

	mov			 ecx, r11d
	shl			 ecx, 4
	sub			 ecx, r11d
	add			 ebx, ecx

	dec			r8
	jmp .loop1

.done1:
	pop			rdx

%ifdef WINDOWS64
	; restore xmm registers
	movdqa		xmm6, [rsp+0x00]
	movdqa		xmm7, [rsp+0x10]
	movdqa		xmm8, [rsp+0x20]
	movdqa		xmm9, [rsp+0x30]
	movdqa		xmmA, [rsp+0x40]
	add			rsp, 58h
%endif

.loop3:
	cmp			rdx, 16
	jb .loop4

	%assign counter 0
	%rep 4
		movzx		 r8d, byte[rsi+0+counter*4]
		movzx		 r9d, byte[rsi+1+counter*4]
		movzx		r10d, byte[rsi+2+counter*4]
		movzx		r11d, byte[rsi+3+counter*4]
		add			eax, r8d
		add			ebx, eax
		add			eax, r9d
		add			ebx, eax
		add			eax, r10d
		add			ebx, eax
		add			eax, r11d
		add			ebx, eax
		%assign counter counter+1
	%endrep

	sub			rdx, 16
	add			rsi, 16
	jmp .loop3

.loop4:
	test		rdx, rdx
	jz .done

	movzx		r8d, byte[rsi]
	add			eax, r8d
	add			ebx, eax
	dec			rdx
	inc			rsi
	jmp .loop4

.done:
	; modulo reduction
	mov			 r8d, eax
	shr			 r8d, 0x10
	mov			r10d, r8d
	and			 eax, 0xffff
	shl			r10d, 4
	sub			r10d, r8d
	add			 eax, r10d
	cmp			 eax, 65521
	jb	.r2
	sub			 eax, 65521

.r2:
	mov			 r8d, ebx
	shr			 r8d, 0x10
	mov			r10d, r8d
	and			 ebx, 0xffff
	shl			r10d, 4
	sub			r10d, r8d
	add			 ebx, r10d
	cmp			 ebx, 65521
	jb	.r3
	sub			 ebx, 65521

.r3:
	; combine a and b
	shl			ebx, 0x10
	or			eax, ebx

%ifdef WINDOWS64
	pop			rsi
%endif
	pop			rbx
	ret


section .data
align 16

jumptable:
	.update:
		dq initjump

initdone:
	dq 0h
