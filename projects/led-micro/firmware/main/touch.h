#pragma once
#include "led_model.h"
namespace ledapp {
// One action per complete press/release; entering the app with a held touch is
// inert.
class Touch {
    bool armed = false, pressed = false, cancelled = false;
    unsigned releases = 0;
    Action start = Action::none;

  public:
    Action sample(bool down, Action current) {
        if (!armed) {
            releases = down ? 0 : releases + 1;
            if (releases >= 3) {
                armed = true;
                releases = 0;
            }
            return Action::none;
        }
        if (down) {
            releases = 0;
            if (!pressed) {
                pressed = true;
                cancelled = false;
                start = current;
            } else if (current != start)
                cancelled = true;
        } else if (pressed && ++releases >= 3) {
            pressed = false;
            releases = 0;
            return cancelled ? Action::none : start;
        }
        return Action::none;
    }
};
} // namespace ledapp
