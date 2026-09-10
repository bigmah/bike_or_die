/* setjmp for the freestanding GBA build; only the type is needed by the
 * PumpkinOS headers, the functions are provided by gba/src/setjmp.s. */
#ifndef BOD_SETJMP_H
#define BOD_SETJMP_H
struct __jmp_buf_tag { unsigned long regs[16]; };
typedef struct __jmp_buf_tag jmp_buf[1];
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#endif
