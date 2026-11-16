#pragma once

#include <QByteArrayView>
#include <QtGlobal>

namespace fdss::viewmodels {

// Workaround for a Qt 6.12 bug (fix(P12): CI-only dialog failures, docs/BACKLOG.md): the
// incremental garbage collector of the QML engine frees the property storage of objects that are
// still alive, so they print "Cannot find member data" and lose their properties: the IconButtons
// and the FocusFlickable of an AppDialog, the IconButtons of the DateField popup. Which objects
// and when depends on how fast the machine is (CI runners); QV4_GC_TIMELIMIT=1 (very short
// incremental steps) reproduces it every time. A time limit of 0 makes every collection run in one
// go, as before Qt 6.8, and the same run is clean.
// Call before the first QML engine; it wins over the environment, so the app never runs with the
// faulty collector. Remove when qml_smoke under QV4_GC_TIMELIMIT=1 without this call is clean.
inline void useNonIncrementalGarbageCollector() { qputenv("QV4_GC_TIMELIMIT", "0"); }

} // namespace fdss::viewmodels
