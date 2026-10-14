#include "clock_policy.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace cyd::clock {
namespace {
struct ZoneInfo {const char* name;const char* rule;};
constexpr ZoneInfo zones[]={{"Sydney","AEST-10AEDT,M10.1.0,M4.1.0/3"},
 {"Melbourne","AEST-10AEDT,M10.1.0,M4.1.0/3"},{"Brisbane","AEST-10"},
 {"Adelaide","ACST-9:30ACDT,M10.1.0,M4.1.0/3"},{"Perth","AWST-8"},{"UTC","UTC0"}};
int wrap(int value,int low,int high){return value<low?high:value>high?low:value;}
}
bool valid_zone(Zone z){return unsigned(z)<unsigned(Zone::count);}
const char* zone_name(Zone z){return zones[valid_zone(z)?unsigned(z):0].name;}
const char* zone_rule(Zone z){return zones[valid_zone(z)?unsigned(z):0].rule;}
void apply_zone(Zone z){setenv("TZ",zone_rule(z),1);tzset();}
int month_days(int year,int month){if(month<1||month>12)return 0;constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};return days[month-1]+(month==2&&year%4==0&&(year%100!=0||year%400==0));}
bool valid_civil(const Civil& c){return c.year>=first_year&&c.year<=last_year&&c.month>=1&&c.month<=12&&c.day>=1&&c.day<=month_days(c.year,c.month)&&c.hour>=0&&c.hour<24&&c.minute>=0&&c.minute<60&&c.second>=0&&c.second<60;}
bool utc_to_local(int64_t epoch,Civil& c,char (&date)[32],char (&abbr)[12]){
 time_t t=epoch;tm local{};if(!localtime_r(&t,&local))return false;
 c={local.tm_year+1900,local.tm_mon+1,local.tm_mday,local.tm_hour,local.tm_min,local.tm_sec};if(!valid_civil(c))return false;
 strftime(date,sizeof(date),"%a %d %b %Y",&local);strftime(abbr,sizeof(abbr),"%Z",&local);return true;
}
bool local_to_utc(const Civil& c,int64_t& epoch){
 if(!valid_civil(c))return false;
 tm t{};t.tm_year=c.year-1900;t.tm_mon=c.month-1;t.tm_mday=c.day;t.tm_hour=c.hour;t.tm_min=c.minute;t.tm_sec=c.second;t.tm_isdst=-1;
 time_t value=mktime(&t);if(value<0)return false;
 Civil roundtrip{};char date[32],abbr[12];if(!utc_to_local(value,roundtrip,date,abbr))return false;
 // Reject the skipped spring-forward hour instead of silently changing it.
 if(roundtrip.year!=c.year||roundtrip.month!=c.month||roundtrip.day!=c.day||roundtrip.hour!=c.hour||roundtrip.minute!=c.minute||roundtrip.second!=c.second)return false;
 epoch=value;return true;
}
void adjust(Civil& c,Field field,int direction){direction=direction<0?-1:1;
 switch(field){case Field::year:c.year=wrap(c.year+direction,first_year,last_year);break;case Field::month:c.month=wrap(c.month+direction,1,12);break;case Field::day:c.day=wrap(c.day+direction,1,month_days(c.year,c.month));break;case Field::hour:c.hour=wrap(c.hour+direction,0,23);break;case Field::minute:c.minute=wrap(c.minute+direction,0,59);break;case Field::second:c.second=wrap(c.second+direction,0,59);break;}
 if(c.day>month_days(c.year,c.month))c.day=month_days(c.year,c.month);
}
}
