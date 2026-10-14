#pragma once
#include <cstdint>

namespace cyd::clock {
enum class Zone : uint8_t {sydney,melbourne,brisbane,adelaide,perth,utc,count};
enum class Field : unsigned {year,month,day,hour,minute,second};
struct Civil {int year=2026,month=1,day=1,hour=12,minute=0,second=0;};
constexpr int first_year=2024,last_year=2099;
const char* zone_name(Zone zone);
const char* zone_rule(Zone zone);
bool valid_zone(Zone zone);
void apply_zone(Zone zone);
int month_days(int year,int month);
bool valid_civil(const Civil& civil);
bool utc_to_local(int64_t epoch,Civil& civil,char (&date)[32],char (&abbr)[12]);
bool local_to_utc(const Civil& civil,int64_t& epoch);
void adjust(Civil& civil,Field field,int direction);
}
