#pragma once
#include "keyboard_mapping.h"
#include <cstddef>

namespace quicknote {
constexpr size_t editor_capacity=8192;

class Editor {
public:
    const char* data()const{return text_;}
    size_t size()const{return size_;}
    size_t cursor()const{return cursor_;}
    bool dirty()const{return dirty_;}
    void mark_saved(){dirty_=false;}
    void clear();
    bool assign(const char* data,size_t size,bool dirty=true);
    bool insert(const char* data,size_t size);
    bool backspace();
    bool erase_forward();
    void left();
    void right();
    void home();
    void end();
    void up();
    void down();
private:
    size_t line_start(size_t position)const;
    size_t line_end(size_t position)const;
    char text_[editor_capacity+1]{};
    size_t size_=0,cursor_=0;
    bool dirty_=false;
};

enum class Input {ignored,changed,save};
Input apply_key(Editor& editor,const cm::KeySequence& key);

bool ensure_note_directories(const char* root,char* error,size_t capacity);
bool load_draft(const char* root,Editor& editor,char* error,size_t capacity);
bool checkpoint_draft(const char* root,const Editor& editor,char* error,size_t capacity);
bool discard_draft(const char* root,char* error,size_t capacity);
bool append_note(const char* root,const char* iso_date,const char* date_heading,
                 const char* time,const Editor& editor,char* error,size_t capacity);
size_t load_day_tail(const char* root,const char* iso_date,char* output,size_t capacity,
                     bool& truncated,char* error,size_t error_capacity);
}
