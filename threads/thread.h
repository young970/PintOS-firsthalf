#ifndef THREAD_H
#define THREAD_H

#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"


void thread_sleep (int64_t ticks); // 실행 중인 스레드를 sleep으로 만듬
void thread_awake(int64_t ticks); // sleep queue에서 깨워야할 스레드를 깨움
void update_next_tick_to_awake(int64_t ticks); // 최소 틱을 가진 스레드 저장
int64_t get_next_tick_to_awake(void); // thread.c의 next_tick_to_awake 반환


// struct thread
// {
//     /* 깨어나야 할 tick을 저장할 변수 추가 */
// };


#endif

