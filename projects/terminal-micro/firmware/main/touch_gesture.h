#pragma once
#include <cstdint>
#include <cstdlib>
namespace terminal {
struct Gesture {bool released=false;int x=0,y=0,dy=0;bool dragged=false;};
class TouchGesture {
    bool active_=false;int first_x_=0,first_y_=0,x_=0,y_=0;unsigned samples_=0;uint64_t last_=0;
public:
    bool active()const{return active_;}
    Gesture sample(bool down,int x,int y,uint64_t now){
        if(down){
            if(!active_){active_=true;first_x_=x_=x;first_y_=y_=y;samples_=1;}
            else {x_=(3*x_+x)/4;y_=(3*y_+y)/4;if(samples_<16)++samples_;}
            last_=now;return {};
        }
        if(!active_||now-last_<45)return {};
        active_=false;if(samples_<2)return {};
        const int dy=y_-first_y_;
        return {true,first_x_,first_y_,dy,std::abs(dy)>14||std::abs(x_-first_x_)>14};
    }
};
}
