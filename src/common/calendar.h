#ifndef CLOUDBERRY_CALENDAR_H
#define CLOUDBERRY_CALENDAR_H
#include <stdint.h>
/* Gregorian civil UTC to POSIX seconds; no leap-second fiction or local zone. */
static inline int cb_calendar_epoch(unsigned year,unsigned month,unsigned day,unsigned hour,unsigned minute,unsigned second,int64_t *out){
    static const unsigned char lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(year<1 || year>9999 || month<1 || month>12 || hour>23 || minute>59 || second>59)return 0;
    unsigned leap=!(year%4) && (year%100 || !(year%400));if(!day || day>lengths[month-1]+(month==2?leap:0))return 0;
    int y=(int)year-(month<=2),era=y/400;unsigned yoe=(unsigned)(y-era*400),m=month>2?month-3:month+9;
    int64_t days=(int64_t)era*146097+(yoe*365+yoe/4-yoe/100)+(153*m+2)/5+day-1-719468;
    *out=days*86400+hour*3600+minute*60+second;return 1;
}
#endif
