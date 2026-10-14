#include <cassert>
#include <fstream>
#include <filesystem>
#include <vector>
#include "../firmware/main/app_main.cpp"
namespace {unsigned install_calls=0,helper_calls=0;uint64_t mock_clock=0;struct Reboot {};bool helper_available=false,clock_requested=false;int64_t mock_epoch=1791849600;cyd::clock::Zone mock_zone=cyd::clock::Zone::sydney;bool mock_auto=true;}
int mock_touch_irq_level=1;
namespace {struct Sleeping {};unsigned sleeps=0,resumes=0,unmounts=0;esp_err_t wake_error=ESP_OK,display_error=ESP_OK,unmount_error=ESP_OK;std::vector<int> power_calls;}
esp_err_t esp_sleep_enable_ext0_wakeup(int pin,int level){assert(pin==36&&level==0);power_calls.push_back(1);return wake_error;}
[[noreturn]] void esp_deep_sleep_start(){power_calls.push_back(4);++sleeps;throw Sleeping{};}
const esp_app_desc_t* esp_app_get_description(){static esp_app_desc_t desc{};std::strcpy(desc.version,"0.3.3-sleep");return &desc;}
void esp_restart(){throw Reboot{};}
void vTaskDelay(unsigned n){mock_clock+=n;}
namespace cyd {
BoardStatus hal_init(){return {ESP_OK,ESP_OK,ESP_OK,true};}
esp_err_t hal_render(PixelCallback cb,void* u){for(int y=0;y<240;++y)for(int x=0;x<320;++x)assert(cb(x,y,u)==g_framebuffer[y*320+x]);return ESP_OK;}
esp_err_t hal_sd_unmount(){++unmounts;power_calls.push_back(2);return unmount_error;}
esp_err_t hal_sd_mount(){return ESP_OK;}
esp_err_t hal_display_suspend(){power_calls.push_back(3);return display_error;}
esp_err_t hal_display_resume(){++resumes;return ESP_OK;}
bool hal_touch_poll(int&,int&){return false;}
bool hal_sd_ready(){return true;}
bool hal_sd_usage(uint64_t& total,uint64_t& free){total=32000000000ULL;free=31000000000ULL;return true;}
uint64_t hal_uptime_ms(){return mock_clock;}
unsigned hal_free_heap_kb(){return 100;}
bool installer_init(char (&error)[64]){error[0]=0;return true;}
bool installed_app(InstalledApp&){return false;}
bool install_firmware(const char*,char (&error)[64],InstallProgress,void*){++install_calls;std::strcpy(error,"Injected SD read failure");return false;}
bool boot_installed(char (&error)[64],InstallProgress,void*){std::strcpy(error,"No installed app yet");return false;}
bool clock_helper_available(){return helper_available;}
bool boot_clock_helper(char (&error)[64]){++helper_calls;if(!helper_available){std::strcpy(error,"No clock helper");return false;}return true;}
}
namespace cyd::clock {
void init(){apply_zone(mock_zone);}
Snapshot snapshot(){Snapshot c;c.epoch=mock_epoch;c.zone=mock_zone;c.automatic=mock_auto;apply_zone(mock_zone);c.valid=utc_to_local(c.epoch,c.civil,c.date,c.abbr);return c;}
bool set_local(const Civil& c){return local_to_utc(c,mock_epoch);}
bool set_zone(Zone z){mock_zone=z;apply_zone(z);return true;}
bool set_automatic(bool value){mock_auto=value;return true;}
bool request_sync(){clock_requested=true;return true;}
void cancel_request(){clock_requested=false;}
Result take_return_result(){return Result::none;}
}
void save(const std::filesystem::path& p){std::ofstream f(p,std::ios::binary);f<<"P6\n320 240\n255\n";for(auto c:g_framebuffer){const char rgb[]={static_cast<char>(((c>>11)&31)*255/31),static_cast<char>(((c>>5)&63)*255/63),static_cast<char>((c&31)*255/31)};f.write(rgb,3);}}
int main(int argc,char** argv){
    assert(argc==2);update_button_labels();update_sd_usage();render();save(std::filesystem::path(argv[1])/"console.ppm");
    const auto before=std::vector<uint16_t>(std::begin(g_framebuffer),std::end(g_framebuffer));
    handle_touch(40,90);assert(!dark_mode);render();save(std::filesystem::path(argv[1])/"console-light.ppm");handle_touch(40,90);assert(dark_mode);render();assert(std::equal(before.begin(),before.end(),std::begin(g_framebuffer)));
    installer_ready=true;page=Page::library;library_count=7;has_installed=false;
    for(unsigned i=0;i<7;++i){auto& entry=library[i];entry.valid=true;std::strcpy(entry.name,"TerminalMicro-0.2.3-headroom.bin");std::strcpy(entry.image.project,i?"An app with a very long title":"TerminalMicro");std::strcpy(entry.image.version,"0.2.3-headroom");entry.image.size=1390000;}
    render();save(std::filesystem::path(argv[1])/"library.ppm");
    handle_touch(220,220);assert(library_page==1);handle_touch(220,220);assert(library_page==2);handle_touch(220,220);assert(library_page==2);
    handle_touch(130,220);assert(library_page==1);handle_touch(130,220);assert(library_page==0);
    handle_touch(30,66);assert(page==Page::confirm && chosen==0);render();save(std::filesystem::path(argv[1])/"confirm.ppm");
    handle_touch(10,220);assert(page==Page::library && install_calls==0);
    handle_touch(30,66);handle_touch(170,220);assert(install_calls==1 && page==Page::error);render();
    handle_touch(10,220);assert(page==Page::home);
    handle_touch(260,18);assert(page==Page::clock);render();save(std::filesystem::path(argv[1])/"clock.ppm");
    handle_touch(225,165);assert(helper_calls==0&&!clock_requested); // unavailable sync cannot boot or change Run last
    handle_touch(50,165);assert(page==Page::set_time);render();save(std::filesystem::path(argv[1])/"set-time.ppm");
    editing_time={2026,1,31,12,0,0};handle_touch(140,70);assert(editing_field==cyd::clock::Field::month);handle_touch(200,180);assert(editing_time.month==2&&editing_time.day==28);
    handle_touch(25,225);assert(page==Page::clock);auto previous_time=mock_epoch;
    handle_touch(50,165);editing_time={2026,10,4,2,30,0};handle_touch(220,225);assert(page==Page::set_time&&mock_epoch==previous_time);
    editing_time={2026,10,13,11,23,45};handle_touch(220,225);assert(page==Page::clock&&cyd::clock::snapshot().civil.minute==23);
    handle_touch(225,123);assert(!mock_auto);handle_touch(225,123);assert(mock_auto);
    handle_touch(40,123);assert(mock_zone==cyd::clock::Zone::melbourne);mock_zone=cyd::clock::Zone::sydney;
    helper_available=true;try{handle_touch(225,165);assert(false);}catch(const Reboot&){}assert(helper_calls==1&&clock_requested&&install_calls==1);
    mock_epoch=0;page=Page::home;render();save(std::filesystem::path(argv[1])/"clock-unset.ppm");
    library[0].valid=false;std::strcpy(library[0].error,"Image checksum failed");page=Page::library;handle_touch(30,66);assert(page==Page::error && install_calls==1);
    page=Page::progress;std::strcpy(install_stage,"Verifying flash");install_percent=50;render();save(std::filesystem::path(argv[1])/"progress.ppm");
    // Full rendering of every screen in both themes, including all fallback glyphs.
    for(const auto& face:{cyd_fonts::pixelfont_7x5_thin,cyd_fonts::pixelfont_11x5_thin,cyd_fonts::pixelfont_13x7_semibold})for(unsigned c=1;c<256;++c){char word[]={static_cast<char>(c),0};draw_font_text(word,0,0,face,0xffff);}
    for(auto colour:{light_theme,dark_theme}){theme=colour;for(auto current:{Page::home,Page::library,Page::confirm,Page::error,Page::progress,Page::clock,Page::set_time}){page=current;render();}}
    page=Page::shutdown;render();save(std::filesystem::path(argv[1])/"shutdown.ppm");
    page=Page::home;power_calls.clear();
    try{handle_touch(270,90);assert(false);}catch(const Sleeping&){}
    assert(sleeps==1&&power_calls==std::vector<int>({1,2,3,4}));
    mock_touch_irq_level=0;page=Page::home;power_calls.clear();handle_touch(270,90);assert(page==Page::error&&power_calls.empty()&&sleeps==1);
    mock_touch_irq_level=1;
    for(auto* injection:{&wake_error,&unmount_error,&display_error}){
        *injection=ESP_FAIL;page=Page::home;handle_touch(270,90);assert(page==Page::error&&sleeps==1);*injection=ESP_OK;
    }
    assert(resumes==3);
    std::printf("PASS: themes, picker pagination, selection, cancel without install, failed install returns to error/console, invalid item cannot install, all screen/font rendering under ASan/UBSan\n");
}
