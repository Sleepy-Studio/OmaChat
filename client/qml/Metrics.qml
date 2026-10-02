pragma Singleton
import QtQml

QtObject {
    // A QML function reads scale inside the binding, so every caller tracks
    // live changes. A C++ invokable cannot establish that dependency.
    function px(value: real): real { return value * Theme.scale }
}
