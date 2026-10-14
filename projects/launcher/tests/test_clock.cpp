#include "clock_policy.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace cyd::clock;
int64_t utc(Civil c){apply_zone(Zone::utc);int64_t value=0;assert(local_to_utc(c,value));return value;}
int main(){
 assert(month_days(2028,2)==29&&month_days(2026,2)==28&&month_days(2100,2)==28);
 assert(!valid_civil({2026,2,29,0,0,0})&&!valid_civil({2026,10,13,24,0,0})&&!valid_civil({2023,12,31,23,59,59}));
 auto epoch=utc({2026,10,13,0,0,0});Civil c{};char date[32],abbr[12];apply_zone(Zone::sydney);
 assert(utc_to_local(epoch,c,date,abbr)&&c.hour==11&&c.day==13&&!std::strcmp(abbr,"AEDT")&&!std::strcmp(date,"Tue 13 Oct 2026"));
 auto winter=utc({2026,7,1,0,0,0});apply_zone(Zone::sydney);assert(utc_to_local(winter,c,date,abbr)&&c.hour==10&&!std::strcmp(abbr,"AEST"));
 auto spring=utc({2026,10,3,15,59,59});apply_zone(Zone::sydney);assert(utc_to_local(spring,c,date,abbr)&&c.hour==1&&c.minute==59);assert(utc_to_local(spring+1,c,date,abbr)&&c.hour==3&&c.minute==0);int64_t invalid=0;assert(!local_to_utc({2026,10,4,2,30,0},invalid));
 auto fall=utc({2026,4,4,15,59,59});apply_zone(Zone::sydney);assert(utc_to_local(fall,c,date,abbr)&&c.hour==2&&c.minute==59&&!std::strcmp(abbr,"AEDT"));assert(utc_to_local(fall+1,c,date,abbr)&&c.hour==2&&c.minute==0&&!std::strcmp(abbr,"AEST"));
 apply_zone(Zone::adelaide);assert(utc_to_local(epoch,c,date,abbr)&&c.hour==10&&c.minute==30);apply_zone(Zone::brisbane);assert(utc_to_local(epoch,c,date,abbr)&&c.hour==10);apply_zone(Zone::perth);assert(utc_to_local(epoch,c,date,abbr)&&c.hour==8);
 c={2028,2,29,23,59,59};adjust(c,Field::year,1);assert(c.year==2029&&c.day==28);adjust(c,Field::hour,1);assert(c.hour==0);adjust(c,Field::minute,1);assert(c.minute==0);adjust(c,Field::second,1);assert(c.second==0);
 c={2026,1,31,12,0,0};adjust(c,Field::month,1);assert(c.month==2&&c.day==28);adjust(c,Field::day,1);assert(c.day==1);assert(!utc_to_local(0,c,date,abbr));
 assert(!valid_zone(static_cast<Zone>(255))&&!std::strcmp(zone_name(static_cast<Zone>(255)),"Sydney"));
 std::puts("PASS clock: Sydney summer/winter, DST gap/fold, Adelaide half-hour, Brisbane/Perth, leap dates, field wrapping/clipping, invalid/unset time");
}
