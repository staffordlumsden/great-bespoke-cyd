#include "file_policy.h"
#include "device_ui.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iostream>
void save(const std::filesystem::path& path,const fsapp::ui::Scene& scene){std::ofstream out(path,std::ios::binary);out<<"P6\n320 240\n255\n";for(int y=0;y<240;++y)for(int x=0;x<320;++x){auto p=fsapp::ui::pixel(x,y,const_cast<fsapp::ui::Scene*>(&scene));unsigned char bytes[]={static_cast<unsigned char>(((p>>11)&31)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};out.write(reinterpret_cast<char*>(bytes),3);}}
int main(int argc,char** argv){assert(argc==2);char decoded[256];
 for(const char* path:{"/files","/files/test.bin","/firmware/App-0.1.bin","/files/A folder/test.json"})assert(fsapp::shared_path(path));
 for(const char* path:{"/terminal-micro/config.json","/file-server/net-a.json","/files/../contextual-micro/key","/files/.hidden","/files//test","/firmware2/test","/files/a\\b","/files/C:secret"})assert(!fsapp::shared_path(path));
 assert(fsapp::decode_path("%2Ffiles%2Fhello%20world.bin",decoded,sizeof(decoded)));assert(!std::strcmp(decoded,"/files/hello world.bin"));
 for(const char* encoded:{"/files/a%00b","/files/%C0%AE%C0%AE/x","/files/%ED%A0%80","/files/%FF","/files/%Q0","/files/%0"})assert(!fsapp::decode_path(encoded,decoded,sizeof(decoded)));
 assert(fsapp::decode_path("/files/caf%C3%A9.txt",decoded,sizeof(decoded)));assert(fsapp::shared_path(decoded));
 uint32_t start=0,length=0;assert(fsapp::byte_range("",100,start,length)&&start==0&&length==100);
 assert(fsapp::byte_range("bytes=10-19",100,start,length)&&start==10&&length==10);assert(fsapp::byte_range("bytes=-10",100,start,length)&&start==90&&length==10);assert(fsapp::byte_range("bytes=90-",100,start,length)&&start==90&&length==10);assert(fsapp::byte_range("bytes=0-999",100,start,length)&&length==100);
 for(const char* range:{"bytes=100-","bytes=10-1","bytes=-0","bytes=1-2,4-5","bytes=+1-2","bytes=","bytes=999999999999999999999999-"})assert(!fsapp::byte_range(range,100,start,length));
 fsapp::ui::Scene scene;fsapp::ui::Model m;m.version="FS 0.1.2-clock-clearcode";m.connected=true;m.ip="192.0.2.42";m.code="23456792";m.busy=true;m.phase="Uploading to SD";m.name="Public-transfer-test.bin";m.done=4*1048576;m.total=8*1048576;m.elapsed_ms=8000;m.card_free=29ull*1073741824;
 for(bool light:{false,true}){m.light=light;fsapp::ui::build(scene,m);assert(scene.count<=32);for(unsigned i=0;i<scene.count;++i){auto e=scene.elements[i];assert(e.x>=0&&e.y>=0&&e.x+e.w<=320&&e.y+e.h<=240);}save(std::filesystem::path(argv[1])/(light?"device-light.ppm":"device-dark.ppm"),scene);}
 m.networks=true;m.setup=true;m.setup_ssid="CYD-Setup-ABC123";fsapp::ui::build(scene,m);save(std::filesystem::path(argv[1])/"device-networks.ppm",scene);
 m.networks=false;m.setup=false;m.clock_sync=true;m.phase="Synchronizing clock";m.message="Connecting to a time server";
 for(bool light:{false,true}){m.light=light;fsapp::ui::build(scene,m);assert(scene.elements[0].kind==fsapp::ui::Kind::clock);save(std::filesystem::path(argv[1])/(light?"clock-sync-light.ppm":"clock-sync-dark.ppm"),scene);}
 m.clock_sync=false;
 m.name="x";m.done=m.total;fsapp::ui::build(scene,m);for(int y=0;y<240;++y)for(int x=0;x<320;++x)fsapp::ui::pixel(x,y,&scene);
 std::cout<<"PASS: protected paths, canonical UTF-8, decoded NUL/traversal rejection, byte ranges, complete device themes/layout/raster under ASan/UBSan\n";
}
