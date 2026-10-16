#include "note_model.h"
#include "quicknote_ui.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {
cm::KeySequence key(const char* bytes,size_t size){cm::KeySequence result{};std::memcpy(result.bytes,bytes,size);result.size=size;return result;}
void save_ppm(const std::filesystem::path& path,quicknote::ui::Scene& scene){std::ofstream out(path,std::ios::binary);out<<"P6\n320 240\n255\n";for(int y=0;y<240;++y)for(int x=0;x<320;++x){const auto p=quicknote::ui::pixel(x,y,&scene);const unsigned char rgb[]={static_cast<unsigned char>(((p>>11)&31)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};out.write(reinterpret_cast<const char*>(rgb),3);}}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
}
int main(int argc,char** argv){assert(argc==2);std::filesystem::create_directories(argv[1]);
 const std::string public_sample="abc ABC 123 !@# $%& ()[]{}<>";quicknote::Editor editor;for(char c:public_sample){auto input=key(&c,1);assert(quicknote::apply_key(editor,input)==quicknote::Input::changed);}assert(editor.size()==public_sample.size());
 assert(quicknote::apply_key(editor,key("\r",1))==quicknote::Input::changed);for(char c:std::string("second line"))quicknote::apply_key(editor,key(&c,1));
 quicknote::apply_key(editor,key("\033[D",3));quicknote::apply_key(editor,key("\177",1));assert(std::strstr(editor.data(),"second lie"));
 assert(quicknote::apply_key(editor,key("\023",1))==quicknote::Input::save);
 char temporary[]="/tmp/quicknote-test-XXXXXX";char* base=mkdtemp(temporary);assert(base);std::filesystem::path root=std::filesystem::path(base)/"files/QuickNote";char error[96]{};
 assert(quicknote::ensure_note_directories(root.c_str(),error,sizeof(error)));assert(quicknote::checkpoint_draft(root.c_str(),editor,error,sizeof(error)));
 quicknote::Editor replacement;assert(replacement.assign("replacement checkpoint",22));assert(quicknote::checkpoint_draft(root.c_str(),replacement,error,sizeof(error)));
 quicknote::Editor replacement_restored;assert(quicknote::load_draft(root.c_str(),replacement_restored,error,sizeof(error)));assert(replacement_restored.size()==replacement.size());
 assert(quicknote::checkpoint_draft(root.c_str(),editor,error,sizeof(error)));
 quicknote::Editor restored;assert(quicknote::load_draft(root.c_str(),restored,error,sizeof(error)));assert(restored.size()==editor.size());assert(!std::memcmp(restored.data(),editor.data(),editor.size()));
 std::filesystem::rename(root/".draft",root/".draft.prev");quicknote::Editor recovered_previous;assert(quicknote::load_draft(root.c_str(),recovered_previous,error,sizeof(error)));assert(recovered_previous.size()==editor.size());assert(quicknote::checkpoint_draft(root.c_str(),recovered_previous,error,sizeof(error)));assert(std::filesystem::exists(root/".draft"));assert(!std::filesystem::exists(root/".draft.prev"));
 assert(quicknote::append_note(root.c_str(),"2026-10-13","Tue 13 Oct 2026","19:42",editor,error,sizeof(error)));
 const auto daily=root/"Daily/2026-10-13.md";auto markdown=read(daily);assert(markdown.rfind("# Tue 13 Oct 2026\n\n",0)==0);assert(markdown.find("- 19:42 abc ABC 123 !@# $%& ()[]{}<>\n  second lie\n")!=std::string::npos);
 quicknote::Editor another;assert(another.assign("third",5));assert(quicknote::append_note(root.c_str(),"2026-10-13","Tue 13 Oct 2026","20:01",another,error,sizeof(error)));markdown=read(daily);assert(markdown.find("# Tue",1)==std::string::npos);assert(markdown.find("- 20:01 third\n")!=std::string::npos);
 char tail[64];bool truncated=false;const auto count=quicknote::load_day_tail(root.c_str(),"2026-10-13",tail,sizeof(tail),truncated,error,sizeof(error));assert(count&&truncated&&tail[count]==0);
 assert(quicknote::discard_draft(root.c_str(),error,sizeof(error)));quicknote::Editor empty;assert(quicknote::load_draft(root.c_str(),empty,error,sizeof(error))&&!empty.size());

 const std::string sample_note="Saturday notes\nWeather app is ready.\nRemember the small ideas.";
 quicknote::ui::Scene scene;quicknote::ui::Model model{};model.clock_valid=true;model.dirty=true;model.save_enabled=true;model.bluetooth_connected=true;model.time="19:42";model.date="Tue 13 Oct 2026";model.bluetooth="BT OK";model.status="Unsaved draft - Ctrl+S saves";model.content=sample_note.data();model.content_size=sample_note.size();model.cursor=sample_note.size();
 for(bool light:{false,true})for(int font=0;font<3;++font){model.light=light;model.font_index=font;quicknote::ui::build(scene,model);assert(scene.count<=56);for(unsigned i=0;i<scene.count;++i){const auto& e=scene.elements[i];assert(e.x>=0&&e.y>=0&&e.x+e.w<=320&&e.y+e.h<=240);}for(int y=0;y<240;++y)for(int x=0;x<320;++x)quicknote::ui::pixel(x,y,&scene);if(font==1)save_ppm(std::filesystem::path(argv[1])/(light?"editor-light.ppm":"editor-dark.ppm"),scene);}
 model.history=true;model.content=markdown.c_str();model.content_size=markdown.size();model.status="Viewing Tuesday";model.day_offset=-1;quicknote::ui::build(scene,model);save_ppm(std::filesystem::path(argv[1])/"history.ppm",scene);
 model.clock_valid=false;model.history=false;model.save_enabled=false;model.bluetooth_connected=false;model.time="--:--";model.date="Clock not set";model.bluetooth="BT WAKE";model.status="Set clock in Console before saving";quicknote::ui::build(scene,model);save_ppm(std::filesystem::path(argv[1])/"clock-required.ppm",scene);
 std::string many_lines;for(int i=0;i<600;++i)many_lines+="line\n";model.clock_valid=true;model.content=many_lines.c_str();model.content_size=many_lines.size();model.cursor=many_lines.size();model.font_index=1;quicknote::ui::build(scene,model);assert(scene.count<=56);
 std::filesystem::remove_all(base);std::cout<<"PASS: bounded editor, cursor/edit controls, exact draft recovery, durable Markdown append, bounded day tail, all PixelFont sizes/themes/layout raster\n";
}
