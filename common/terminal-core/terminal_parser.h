#pragma once
#include <cstddef>
#include <cstdint>

namespace cm {
constexpr unsigned terminal_pixel_width=320,terminal_pixel_height=240,terminal_chrome=16,terminal_font_width=6,terminal_font_height=10;
constexpr unsigned terminal_columns=terminal_pixel_width/terminal_font_width,terminal_rows=(terminal_pixel_height-terminal_chrome)/terminal_font_height,terminal_history_rows=48;
constexpr unsigned terminal_cells=terminal_columns*terminal_rows;
enum CellFlag : uint8_t {bold=1,reverse=2,default_fg=4,default_bg=8,underline=16,conceal=32};
struct TerminalCell {uint8_t glyph=' ',fg=2,bg=0,flags=default_fg|default_bg;};
static_assert(sizeof(TerminalCell)==4,"Bound terminal cell memory");
struct TerminalStorage {
    TerminalCell main[terminal_cells],alternate[terminal_cells];
    TerminalCell history[terminal_columns*terminal_history_rows];
    uint8_t dirty[(terminal_cells+7)/8]{};
};
class TerminalParser {
public:
    using Reply=void(*)(void*,const char*,size_t);
    explicit TerminalParser(TerminalStorage&,Reply=nullptr,void* context=nullptr,unsigned columns=terminal_columns,unsigned rows=terminal_rows);
    bool resize(unsigned columns,unsigned rows,TerminalCell* scratch);
    unsigned columns()const{return columns_;}
    unsigned rows()const{return rows_;}
    void feed(const uint8_t*,size_t);
    void reset();
    void redraw();
    void scroll_view(int rows);
    bool take_dirty(unsigned& column,unsigned& row,TerminalCell&,bool& cursor);
    bool take_dirty_row(unsigned& row,TerminalCell* cells,unsigned capacity,int& cursor_column);
    unsigned clear_generation()const{return clear_generation_;}
    bool synchronized_output()const{return synchronized_output_;}
    TerminalCell cell(unsigned column,unsigned row) const;
    unsigned column()const{return x_;}
    unsigned row()const{return y_;}
    bool alternate_screen()const{return alternate_;}
    bool application_cursor()const{return application_cursor_;}
    bool cursor_visible()const{return cursor_visible_;}
    unsigned history_count()const{return history_count_;}
    unsigned view_offset()const{return view_offset_;}
    unsigned replacements()const{return replacements_;}
private:
    enum class Parse {ground,escape,csi,osc,osc_escape,ignore_string,ignore_escape,charset};
    unsigned columns_=terminal_columns,rows_=terminal_rows,cells_=terminal_cells;
    TerminalStorage& storage_; Reply reply_;void* context_;
    unsigned x_=0,y_=0,top_=0,bottom_=terminal_rows-1,scan_=0;
    unsigned saved_x_=0,saved_y_=0,main_x_=0,main_y_=0;
    unsigned history_head_=0,history_count_=0,view_offset_=0,replacements_=0;
    unsigned clear_generation_=0;
    bool synchronized_output_=false;
    TerminalCell style_{},saved_style_{},main_style_{};
    Parse parse_=Parse::ground;
    int params_[16]{};unsigned parameter_=0;bool private_=false,secondary_=false,bad_csi_=false;
    bool alternate_=false,origin_=false,wrap_=true,pending_wrap_=false,restore_main_style_=false;
    bool cursor_visible_=true,application_cursor_=false,g0_line_=false,g1_line_=false,shifted_=false;
    unsigned charset_slot_=0;
    uint8_t tabs_[(terminal_columns+7)/8]{};
    uint32_t unicode_=0,unicode_min_=0;unsigned unicode_left_=0;
    TerminalCell* screen(){return alternate_?storage_.alternate:storage_.main;}
    const TerminalCell* screen()const{return alternate_?storage_.alternate:storage_.main;}
    void byte(uint8_t);
    void put(uint8_t);
    void codepoint(uint32_t);
    void csi(uint8_t);
    void sgr();
    void linefeed();
    void scroll(unsigned top,unsigned bottom,int rows);
    void erase(unsigned first,unsigned last);
    void mark(unsigned index);
    int parameter(unsigned index,int fallback)const;
    void save();void restore();
    void answer(const char*,size_t);
};
uint16_t terminal_colour(uint8_t index);
}
