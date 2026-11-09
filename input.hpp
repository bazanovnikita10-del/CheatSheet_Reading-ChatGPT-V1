#pragma once

#include "Arduino.h"

enum class ButtonState : int {
  None,
  Down,
  Up,
  Hold
};

struct Input {
  ButtonState Left;
  ButtonState Right;
  ButtonState Center;

  Input(ButtonState left, ButtonState right, ButtonState center)
    : Left(left), Right(right), Center(center) {}
};

extern Input GetInput(void);
extern Input GetFastInput(void);