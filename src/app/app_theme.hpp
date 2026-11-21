#pragma once
class QApplication;

// Changes presentation only. MainWindow applies the same mode to CubeCanvas
// because cube face colors and plastic shading are independent of QPalette.
// scale is a window/content scale in Qt logical pixels; Qt handles monitor DPI.
void applyAppTheme(QApplication& application, bool dark, double scale = 1.0);
