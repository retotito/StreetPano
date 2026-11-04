#pragma once

#include <utility>

namespace streetpano::ui {

// Creates a Qt object owned by its parent, which deletes it automatically.
// This is the only place in the project allowed to use plain new.
template <typename T, typename Parent, typename... Args>
[[nodiscard]] T* makeChild(Parent* parent, Args&&... args) {
    return new T(std::forward<Args>(args)..., parent);
}

}  // namespace streetpano::ui
