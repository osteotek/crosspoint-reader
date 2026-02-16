#include "ButtonNavigator.h"

const MappedInputManager* ButtonNavigator::mappedInput = nullptr;

// Combine single-step and continuous navigation for "next" direction.
void ButtonNavigator::onNext(const Callback& callback) {
  onNextPress(callback);
  onNextContinuous(callback);
}

// Combine single-step and continuous navigation for "previous" direction.
void ButtonNavigator::onPrevious(const Callback& callback) {
  onPreviousPress(callback);
  onPreviousContinuous(callback);
}

// Apply the same handler for initial press and repeated hold behavior.
void ButtonNavigator::onPressAndContinuous(const Buttons& buttons, const Callback& callback) {
  onPress(buttons, callback);
  onContinuous(buttons, callback);
}

// Fire once when a "next" button is pressed.
void ButtonNavigator::onNextPress(const Callback& callback) { onPress(getNextButtons(), callback); }

// Fire once when a "previous" button is pressed.
void ButtonNavigator::onPreviousPress(const Callback& callback) { onPress(getPreviousButtons(), callback); }

// Fire once when a "next" button is released (unless continuous handled it).
void ButtonNavigator::onNextRelease(const Callback& callback) { onRelease(getNextButtons(), callback); }

// Fire once when a "previous" button is released (unless continuous handled it).
void ButtonNavigator::onPreviousRelease(const Callback& callback) { onRelease(getPreviousButtons(), callback); }

// Repeat "next" navigation while a button is held.
void ButtonNavigator::onNextContinuous(const Callback& callback) { onContinuous(getNextButtons(), callback); }

// Repeat "previous" navigation while a button is held.
void ButtonNavigator::onPreviousContinuous(const Callback& callback) { onContinuous(getPreviousButtons(), callback); }

// Invoke when any of the specified buttons is newly pressed.
void ButtonNavigator::onPress(const Buttons& buttons, const Callback& callback) {
  const bool wasPressed = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasPressed(button);
  });

  if (wasPressed) {
    callback();
  }
}

// Invoke on button release, suppressing the release if continuous already ran.
void ButtonNavigator::onRelease(const Buttons& buttons, const Callback& callback) {
  const bool wasReleased = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasReleased(button);
  });

  if (wasReleased) {
    // Only fire on release if we didn't already handle a continuous repeat for this hold.
    if (lastContinuousNavTime == 0) {
      callback();
    }

    lastContinuousNavTime = 0;
  }
}

// Invoke repeatedly for held buttons based on the configured timing.
void ButtonNavigator::onContinuous(const Buttons& buttons, const Callback& callback) {
  const bool isPressed = std::any_of(buttons.begin(), buttons.end(), [this](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->isPressed(button) && shouldNavigateContinuously();
  });

  if (isPressed) {
    // Throttle continuous navigation to the configured repeat interval.
    callback();
    lastContinuousNavTime = millis();
  }
}

// Decide whether the hold duration and repeat interval allow another step.
bool ButtonNavigator::shouldNavigateContinuously() const {
  if (!mappedInput) return false;

  // Start repeating after a hold delay, then repeat at a fixed interval.
  const bool buttonHeldLongEnough = mappedInput->getHeldTime() > continuousStartMs;
  const bool navigationIntervalElapsed = (millis() - lastContinuousNavTime) > continuousIntervalMs;

  return buttonHeldLongEnough && navigationIntervalElapsed;
}

// Wrap to the next index in a list.
int ButtonNavigator::nextIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the next index with wrap-around
  return (currentIndex + 1) % totalItems;
}

// Wrap to the previous index in a list.
int ButtonNavigator::previousIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the previous index with wrap-around
  return (currentIndex + totalItems - 1) % totalItems;
}

// Jump forward by one page of items, wrapping to start when needed.
int ButtonNavigator::nextPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    return nextIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex < lastPageIndex) {
    return (currentPageIndex + 1) * itemsPerPage;
  }

  return 0;
}

// Jump backward by one page of items, wrapping to the final page when needed.
int ButtonNavigator::previousPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    return previousIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex > 0) {
    return (currentPageIndex - 1) * itemsPerPage;
  }

  return lastPageIndex * itemsPerPage;
}
