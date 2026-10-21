#ifndef CLOUDBERRY_THREAD_ACCOUNT_H
#define CLOUDBERRY_THREAD_ACCOUNT_H
#include <stdint.h>
struct thread_account {uint64_t total,start;unsigned active,invalid;};
static inline void thread_account_begin(struct thread_account *a,uint64_t now,int valid){a->start=now;a->active=valid;if(!valid)a->invalid=1;}
static inline uint64_t thread_account_value(const struct thread_account *a,uint64_t now,int valid){if(!a->active || !valid || now<a->start)return a->total;uint64_t delta=now-a->start;return delta>UINT64_MAX-a->total?UINT64_MAX:a->total+delta;}
static inline void thread_account_end(struct thread_account *a,uint64_t now,int valid){if(a->active && (!valid || now<a->start))a->invalid=1;a->total=thread_account_value(a,now,valid);a->active=0;}
#endif
