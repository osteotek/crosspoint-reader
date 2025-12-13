#pragma once
#include <Epub.h>
#include <Epub/Section.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "Screen.h"

class EpubReaderScreen final : public Screen {
  // RAII helper for exception-safe mutex management
  struct MutexGuard {
    SemaphoreHandle_t mutex;
    explicit MutexGuard(SemaphoreHandle_t m) : mutex(m) {
      if (mutex) {
        xSemaphoreTake(mutex, portMAX_DELAY);
      }
    }
    ~MutexGuard() { if (mutex) xSemaphoreGive(mutex); }
    // Delete copy operations to prevent double-release
    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;
  };

  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  std::unique_ptr<Screen> subScreen = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  int pagesUntilFullRefresh = 0;
  bool updateRequired = false;
  const std::function<void()> onGoHome;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void renderScreen();
  void renderContents(std::unique_ptr<Page> p);
  void renderStatusBar() const;

 public:
  explicit EpubReaderScreen(GfxRenderer& renderer, InputManager& inputManager, std::unique_ptr<Epub> epub,
                            const std::function<void()>& onGoHome)
      : Screen(renderer, inputManager), epub(std::move(epub)), onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void handleInput() override;
};
