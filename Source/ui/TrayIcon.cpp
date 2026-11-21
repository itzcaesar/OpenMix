#include "ui/TrayIcon.h"
#include "ui/Theme.h"

TrayIcon::TrayIcon()
{
    const auto icon = makeIcon();
    setIconImage (icon, icon);
    setIconTooltip ("OpenMix");
}

// Placeholder mark: the product initials on a strip of tape (the UI's identity motif).
// Replace with a designed icon when one exists.
juce::Image TrayIcon::makeIcon()
{
    juce::Image img (juce::Image::ARGB, 32, 32, true);
    juce::Graphics g (img);
    const auto& t = Theme::darkPalette();
    g.setColour (t.tape);
    g.fillRoundedRectangle (1.0f, 6.0f, 30.0f, 20.0f, 3.0f);
    g.setColour (t.tapeText);
    g.setFont (Theme::ui (15.0f, true));
    g.drawText ("OM", 1, 6, 30, 20, juce::Justification::centred);
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
