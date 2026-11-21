#include "ui/TrayIcon.h"
#include "ui/Theme.h"

TrayIcon::TrayIcon()
{
    const auto icon = makeIcon();
    setIconImage (icon, icon);
    setIconTooltip ("OpenMix");
}

// The app icon (resources/make_icon.py), drawn in code: a faceplate with three faders, the
// middle cap lit amber. Keep the geometry in step with the script (1024 units -> 32 px).
juce::Image TrayIcon::makeIcon()
{
    constexpr int size = 32;
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);
    const auto& t = Theme::darkPalette();
    const float k = size / 1024.0f;
    auto rect = [k] (float x0, float y0, float x1, float y1) { return juce::Rectangle<float> (x0 * k, y0 * k, (x1 - x0) * k, (y1 - y0) * k); };

    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (rect (56, 56, 968, 968), 196 * k);
    g.setColour (t.panel);
    g.fillRoundedRectangle (rect (76, 76, 948, 948), 178 * k);

    const struct { float cx, cy; juce::Colour cap; } faders[] {
        { 300, 612, t.tape }, { 512, 392, t.accent }, { 724, 520, t.tape }
    };
    for (const auto& f : faders)
    {
        g.setColour (t.meterWell);
        g.fillRoundedRectangle (rect (f.cx - 30, 216, f.cx + 30, 808), 30 * k);
        g.setColour (f.cap);
        g.fillRoundedRectangle (rect (f.cx - 104, f.cy - 54, f.cx + 104, f.cy + 54), 22 * k);
        g.setColour (t.onAccent);
        g.fillRect (rect (f.cx - 104, f.cy - 7, f.cx + 104, f.cy + 7));
    }
    return img;
}

void TrayIcon::mouseDown (const juce::MouseEvent&)
{
    juce::Process::makeForegroundProcess();

    juce::PopupMenu menu;
    menu.addItem ("Show OpenMix", [this] { if (onShow) onShow(); });
    menu.addSeparator();
    menu.addItem ("Quit OpenMix", [this] { if (onQuit) onQuit(); });
    menu.showMenuAsync (juce::PopupMenu::Options());
}

void TrayIcon::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onShow) onShow();
}

void TrayIcon::tellStillRunning()
{
    if (told)
        return;

    told = true;
    showInfoBubble ("OpenMix is still running",
                    "Your audio keeps playing. Double-click the tray icon to open the mixer, "
                    "or right-click it and choose Quit.");
}
