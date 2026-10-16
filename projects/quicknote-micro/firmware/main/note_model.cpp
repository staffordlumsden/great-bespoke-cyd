#include "note_model.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace quicknote {
namespace {
void message(char* output,size_t capacity,const char* value) {
    if(output&&capacity)std::snprintf(output,capacity,"%s",value);
}
bool path(char* output,size_t capacity,const char* root,const char* suffix) {
    const int written=std::snprintf(output,capacity,"%s/%s",root,suffix);
    return written>0&&static_cast<size_t>(written)<capacity;
}
bool directory(const char* value) {
    if(!mkdir(value,0775)||errno==EEXIST)return true;
    return false;
}
bool durable_close(FILE* file) {
    if(!file)return false;
    bool ok=std::fflush(file)==0;
    if(ok)ok=fsync(fileno(file))==0;
    return std::fclose(file)==0&&ok;
}
}

void Editor::clear(){text_[0]=0;size_=cursor_=0;dirty_=true;}
bool Editor::assign(const char* data,size_t size,bool dirty) {
    if((!data&&size)||size>editor_capacity)return false;
    if(size)std::memcpy(text_,data,size);
    text_[size]=0;size_=cursor_=size;dirty_=dirty;return true;
}
bool Editor::insert(const char* data,size_t count) {
    if(!data||!count||size_+count>editor_capacity)return false;
    std::memmove(text_+cursor_+count,text_+cursor_,size_-cursor_+1);
    std::memcpy(text_+cursor_,data,count);cursor_+=count;size_+=count;dirty_=true;return true;
}
bool Editor::backspace(){if(!cursor_)return false;std::memmove(text_+cursor_-1,text_+cursor_,size_-cursor_+1);--cursor_;--size_;dirty_=true;return true;}
bool Editor::erase_forward(){if(cursor_>=size_)return false;std::memmove(text_+cursor_,text_+cursor_+1,size_-cursor_);--size_;dirty_=true;return true;}
void Editor::left(){if(cursor_)--cursor_;}
void Editor::right(){if(cursor_<size_)++cursor_;}
size_t Editor::line_start(size_t position)const{while(position&&text_[position-1]!='\n')--position;return position;}
size_t Editor::line_end(size_t position)const{while(position<size_&&text_[position]!='\n')++position;return position;}
void Editor::home(){cursor_=line_start(cursor_);}
void Editor::end(){cursor_=line_end(cursor_);}
void Editor::up(){const size_t start=line_start(cursor_);if(!start)return;const size_t column=cursor_-start;const size_t previous_end=start-1;const size_t previous_start=line_start(previous_end);cursor_=std::min(previous_start+column,previous_end);}
void Editor::down(){const size_t end=line_end(cursor_);if(end>=size_)return;const size_t column=cursor_-line_start(cursor_);const size_t next_start=end+1;cursor_=std::min(next_start+column,line_end(next_start));}

Input apply_key(Editor& editor,const cm::KeySequence& key) {
    if(key.local!=cm::LocalKeyAction::none||!key.size)return Input::ignored;
    if(key.size==1) {
        const unsigned char value=key.bytes[0];
        if(value==19)return Input::save;
        if(value==8||value==127)return editor.backspace()?Input::changed:Input::ignored;
        if(value=='\r'||value=='\n')return editor.insert("\n",1)?Input::changed:Input::ignored;
        if(value=='\t')return editor.insert("  ",2)?Input::changed:Input::ignored;
        if(value>=32&&value<=126)return editor.insert(key.bytes,1)?Input::changed:Input::ignored;
        return Input::ignored;
    }
    const char* sequence=key.bytes;
    if(key.size==3&&sequence[0]==27&&(sequence[1]=='['||sequence[1]=='O')) {
        switch(sequence[2]){case 'A':editor.up();return Input::changed;case 'B':editor.down();return Input::changed;
            case 'C':editor.right();return Input::changed;case 'D':editor.left();return Input::changed;
            case 'H':editor.home();return Input::changed;case 'F':editor.end();return Input::changed;default:return Input::ignored;}
    }
    if(key.size==4&&!std::memcmp(sequence,"\033[3~",4))return editor.erase_forward()?Input::changed:Input::ignored;
    return Input::ignored;
}

bool ensure_note_directories(const char* root,char* error,size_t capacity) {
    if(!root||!*root){message(error,capacity,"Invalid notes path");return false;}
    char files[256],quicknote[256],daily[256];
    const char* slash=std::strrchr(root,'/');
    if(!slash||slash==root||static_cast<size_t>(slash-root)>=sizeof(files)){message(error,capacity,"Notes path too long");return false;}
    std::memcpy(files,root,slash-root);files[slash-root]=0;
    if(!path(quicknote,sizeof(quicknote),files,slash+1)||!path(daily,sizeof(daily),root,"Daily")){
        message(error,capacity,"Notes path too long");return false;
    }
    if(!directory(files)||!directory(quicknote)||!directory(daily)){message(error,capacity,"Could not create note folders");return false;}
    return true;
}
bool load_draft(const char* root,Editor& editor,char* error,size_t capacity) {
    char filename[300],previous[300];
    if(!path(filename,sizeof(filename),root,".draft")||!path(previous,sizeof(previous),root,".draft.prev")){message(error,capacity,"Draft path too long");return false;}
    FILE* file=std::fopen(filename,"rb");
    if(!file&&errno==ENOENT)file=std::fopen(previous,"rb");
    if(!file){if(errno==ENOENT)return true;message(error,capacity,"Could not open saved draft");return false;}
    char buffer[editor_capacity+1];const size_t count=std::fread(buffer,1,sizeof(buffer),file);const bool io=std::ferror(file);std::fclose(file);
    if(io||count>editor_capacity){message(error,capacity,"Saved draft is too large or unreadable");return false;}
    if(!editor.assign(buffer,count,count!=0)){message(error,capacity,"Could not restore draft");return false;}return true;
}
bool checkpoint_draft(const char* root,const Editor& editor,char* error,size_t capacity) {
    if(!ensure_note_directories(root,error,capacity))return false;
    char final[300],temporary[300],previous[300];
    if(!path(final,sizeof(final),root,".draft")||!path(temporary,sizeof(temporary),root,".draft.tmp")||!path(previous,sizeof(previous),root,".draft.prev")){message(error,capacity,"Draft path too long");return false;}
    if(!editor.size()){
        unlink(temporary);unlink(previous);
        if(unlink(final)&&errno!=ENOENT){message(error,capacity,"Could not remove empty draft");return false;}
        return true;
    }
    FILE* file=std::fopen(temporary,"wb");
    if(!file){message(error,capacity,"Could not create draft checkpoint");return false;}
    const bool written=std::fwrite(editor.data(),1,editor.size(),file)==editor.size();
    const bool closed=durable_close(file);
    if(!written||!closed){unlink(temporary);message(error,capacity,"Could not finish draft checkpoint");return false;}
    struct stat current{};bool moved=false;
    if(!stat(final,&current)){
        unlink(previous);
        if(std::rename(final,previous)){unlink(temporary);message(error,capacity,"Could not rotate draft checkpoint");return false;}
        moved=true;
    } else if(errno!=ENOENT){unlink(temporary);message(error,capacity,"Could not inspect draft checkpoint");return false;}
    if(std::rename(temporary,final)){
        if(moved)std::rename(previous,final);
        unlink(temporary);message(error,capacity,"Could not publish draft checkpoint");return false;
    }
    unlink(previous);return true;
}
bool discard_draft(const char* root,char* error,size_t capacity) {
    char final[300],temporary[300],previous[300],retired[300];
    if(!path(final,sizeof(final),root,".draft")||!path(temporary,sizeof(temporary),root,".draft.tmp")||
       !path(previous,sizeof(previous),root,".draft.prev")||!path(retired,sizeof(retired),root,".draft.saved")){
        message(error,capacity,"Draft path too long");return false;
    }
    unlink(temporary);unlink(retired);
    if(std::rename(final,retired)&&errno!=ENOENT){message(error,capacity,"Could not retire saved draft");return false;}
    unlink(previous);unlink(retired);return true;
}
bool append_note(const char* root,const char* iso,const char* heading,const char* time,const Editor& editor,char* error,size_t capacity) {
    if(!editor.size()){message(error,capacity,"Write something before saving");return false;}
    if(!ensure_note_directories(root,error,capacity))return false;
    char suffix[64],filename[320];std::snprintf(suffix,sizeof(suffix),"Daily/%s.md",iso);
    if(!path(filename,sizeof(filename),root,suffix)){message(error,capacity,"Daily note path too long");return false;}
    struct stat info{};const bool existing=stat(filename,&info)==0&&info.st_size>0;
    FILE* file=std::fopen(filename,"ab");if(!file){message(error,capacity,"Could not open daily note");return false;}
    bool ok=true;
    if(!existing)ok=std::fprintf(file,"# %s\n\n",heading)>0;
    if(ok)ok=std::fprintf(file,"- %s ",time)>0;
    for(size_t i=0;ok&&i<editor.size();++i) {
        if(editor.data()[i]=='\n')ok=std::fwrite("\n  ",1,3,file)==3;
        else ok=std::fputc(static_cast<unsigned char>(editor.data()[i]),file)!=EOF;
    }
    if(ok)ok=std::fputc('\n',file)!=EOF;
    const bool closed=durable_close(file);
    if(!ok||!closed){message(error,capacity,"Could not finish daily note");return false;}
    return true;
}
size_t load_day_tail(const char* root,const char* iso,char* output,size_t capacity,bool& truncated,char* error,size_t error_capacity) {
    truncated=false;if(!output||!capacity)return 0;output[0]=0;
    char suffix[64],filename[320];std::snprintf(suffix,sizeof(suffix),"Daily/%s.md",iso);
    if(!path(filename,sizeof(filename),root,suffix)){message(error,error_capacity,"Daily note path too long");return 0;}
    FILE* file=std::fopen(filename,"rb");if(!file){if(errno==ENOENT){message(error,error_capacity,"No notes for this date");return 0;}message(error,error_capacity,"Could not read daily note");return 0;}
    if(std::fseek(file,0,SEEK_END)){std::fclose(file);message(error,error_capacity,"Could not seek daily note");return 0;}
    const long total=std::ftell(file);if(total<0){std::fclose(file);message(error,error_capacity,"Could not measure daily note");return 0;}
    long start=std::max<long>(0,total-static_cast<long>(capacity-1));truncated=start>0;
    if(std::fseek(file,start,SEEK_SET)){std::fclose(file);message(error,error_capacity,"Could not seek daily note");return 0;}
    if(truncated){int c=0;while((c=std::fgetc(file))!=EOF&&c!='\n'){} }
    const size_t count=std::fread(output,1,capacity-1,file);const bool io=std::ferror(file);std::fclose(file);output[count]=0;
    if(io){output[0]=0;message(error,error_capacity,"Could not finish reading daily note");return 0;}return count;
}
}
