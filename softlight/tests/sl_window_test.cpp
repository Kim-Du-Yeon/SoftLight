
#include <iostream>
#include <memory>

#include "lightsky/utils/Pointer.h"

#include "softlight/SL_KeySym.hpp"
#include "softlight/SL_RenderWindow.hpp"
#include "softlight/SL_WindowEvent.hpp"



/*-----------------------------------------------------------------------------
 *
-----------------------------------------------------------------------------*/
int main()
{
  std::cout << "Event Alignment: " << alignof(SL_WindowEvent) << std::endl;
  ls::utils::Pointer<SL_RenderWindow> pWindow{SL_RenderWindow::create()};

  int shouldQuit = pWindow->init();

  if (shouldQuit)
  {
    return shouldQuit;
  }

  if (!pWindow->run())
  {
    std::cerr << "Unable to run the test window!" << std::endl;
    pWindow->destroy();
    return -1;
  }

  pWindow->set_keys_repeat(false);

  while (!shouldQuit)
  {
    pWindow->update();
    SL_WindowEvent evt;

    if (pWindow->has_event())
    {
      pWindow->pop_event(&evt);

      if (evt.type == SL_WinEventType::WIN_EVENT_KEY_DOWN)
      {
        std::cout << "Pressed key " << evt.keyboard.key << ' ' << evt.keyboard.keysym  << std::endl;
      }
      else if (evt.type == SL_WinEventType::WIN_EVENT_KEY_UP)
      {
        const SL_KeySymbol keySym = evt.keyboard.keysym;
        switch (keySym)
        {
          case SL_KeySymbol::KEY_SYM_ESCAPE:
            std::cout << "Escape button pressed. Now exiting." << std::endl;
            shouldQuit = 1;
            break;

          case SL_KeySymbol::KEY_SYM_SPACE:
            if (pWindow->state() == WindowStateInfo::WINDOW_RUNNING)
            {
              std::cout << "Space button pressed. Pausing." << std::endl;
              pWindow->set_keys_repeat(true);
              pWindow->pause();
            }
            else
            {
              std::cout << "Space button pressed. Resuming." << std::endl;
              pWindow->set_keys_repeat(false);
              pWindow->run();
            }
            break;

          case SL_KeySymbol::KEY_SYM_LEFT:
            std::cout << "Decreasing window size" << std::endl;
            if (!pWindow->set_size(640, 480))
            {
              std::cerr << "Failed to resize window" << std::endl;
            }
            break;

          case SL_KeySymbol::KEY_SYM_RIGHT:
            std::cout << "Increasing window size" << std::endl;
            if (!pWindow->set_size(800, 600))
            {
              std::cerr << "Failed to resize window" << std::endl;
            }
            break;

          case SL_KeySymbol::KEY_SYM_NUMPAD_0:
          case SL_KeySymbol::KEY_SYM_NUMPAD_1:
          case SL_KeySymbol::KEY_SYM_NUMPAD_2:
          case SL_KeySymbol::KEY_SYM_NUMPAD_3:
          case SL_KeySymbol::KEY_SYM_NUMPAD_4:
          case SL_KeySymbol::KEY_SYM_NUMPAD_5:
          case SL_KeySymbol::KEY_SYM_NUMPAD_6:
          case SL_KeySymbol::KEY_SYM_NUMPAD_7:
          case SL_KeySymbol::KEY_SYM_NUMPAD_9:
            std::cout << "Pressed numpad digit." << std::endl;
            break;

          case SL_KeySymbol::KEY_SYM_UNKNOWN:
            std::cout << "Invalid key released" << std::endl;
            break;

          default:
            std::cout << "Released key " << keySym << ": " << key_to_string(keySym) << std::endl;
            break;
        }
      }
      else if (evt.type == SL_WinEventType::WIN_EVENT_MOUSE_WHEEL_MOVED)
      {
        std::cout << "Mouse wheel moved: " << evt.wheel.x << ' ' << evt.wheel.y << ' ' << evt.wheel.direction << std::endl;
      }
      else if (evt.type == SL_WinEventType::WIN_EVENT_MOUSE_ENTER || evt.type == SL_WinEventType::WIN_EVENT_MOUSE_LEAVE)
      {
          std::cout << "Mouse Enter/Leave: " << evt.mousePos.x << 'x' << evt.mousePos.y << std::endl;
      }
      else if (evt.type == SL_WinEventType::WIN_EVENT_MOUSE_MOVED)
      {
          std::cout << "Mouse moved: " << evt.mousePos.x << 'x' << evt.mousePos.y << std::endl;
      }
      else if (evt.type == SL_WinEventType::WIN_EVENT_MOVED)
      {
        std::cout << "Window moved: "
                  << evt.window.x << 'x' << evt.window.y << " - "
                  << pWindow->x_position() << 'x' << pWindow->y_position()
                  << std::endl;
      }
      else if (evt.type == SL_WinEventType::WIN_EVENT_RESIZED)
      {
        std::cout << "Window resized: "
                  << evt.window.width << 'x' << evt.window.height << " - "
                  << pWindow->width() << 'x' << pWindow->height()
                  << std::endl;
      }
      else if (evt.type == SL_WinEventType::WIN_EVENT_CLOSING)
      {
        std::cout << "Window close event caught. Exiting." << std::endl;
        shouldQuit = true;
      }
    }

    // All events handled. Now check on the state of the window.
    if (pWindow->state() == WindowStateInfo::WINDOW_CLOSING)
    {
      std::cout << "Window close state encountered. Exiting." << std::endl;
      shouldQuit = true;
    }
  }

  return pWindow->destroy();
}
