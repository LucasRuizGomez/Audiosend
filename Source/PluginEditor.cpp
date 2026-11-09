#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
SongSenderEditor::SongSenderEditor (SongSenderProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setResizable (true, false);
    setSize      (640, 440);
    setResizeLimits (kMinW, kMinH, kMaxW, kMaxH);

    // ── Tab buttons ───────────────────────────────────────────────────────────
    for (auto* btn : { &tabSong, &tabSender, &tabSettings })
    {
        btn->setColour (juce::TextButton::buttonColourId,   juce::Colour(0x00000000));
        btn->setColour (juce::TextButton::buttonOnColourId, juce::Colour(0x00000000));
        btn->setColour (juce::TextButton::textColourOffId,  juce::Colour(0xffa0c8e0));
        btn->setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        btn->setColour (juce::ComboBox::outlineColourId,    juce::Colour(0x00000000));
        addAndMakeVisible (btn);
    }
    tabSong    .onClick = [this]() { switchTab(TAB_SONG);     };
    tabSender  .onClick = [this]() { switchTab(TAB_SENDER);   };
    tabSettings.onClick = [this]() { switchTab(TAB_SETTINGS); };

    // ── Drop zone ─────────────────────────────────────────────────────────────
    dropZone.onFileDrop = [this](const juce::File& f) {
        juce::String folder = folderCombo.getText();
        if (folder.isEmpty()) folder = "Uploads";
        processor.addFile(f, folder);
        statusLabel.setText("Analysing: " + f.getFileName(), juce::dontSendNotification);
    };
    dropZone.onBrowseClick = [this]() { openBrowseDialog(); };
    addAndMakeVisible (dropZone);

    // ── Folder selector ───────────────────────────────────────────────────────
    folderLabel.setText  ("Tag as:", juce::dontSendNotification);
    folderLabel.setFont  (juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    folderLabel.setColour (juce::Label::textColourId, AeroCol::muted());
    addAndMakeVisible (folderLabel);

    folderList = { "Uploads", "Snippets", "Final Versions", "References", "Stems" };
    rebuildFolderCombo();
    folderCombo.setEditableText (true);
    folderCombo.setSelectedId   (1, juce::dontSendNotification);
    folderCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour(0xfff0f8ff));
    folderCombo.setColour (juce::ComboBox::outlineColourId,    juce::Colour(0xff90c8e8));
    folderCombo.setColour (juce::ComboBox::textColourId,       AeroCol::text());
    addAndMakeVisible (folderCombo);

    newFolderBtn.setTooltip ("Create a new folder tag");
    newFolderBtn.setColour  (juce::TextButton::buttonColourId,  juce::Colour(0xffdceeff));
    newFolderBtn.setColour  (juce::TextButton::textColourOffId, AeroCol::blue());
    newFolderBtn.setColour  (juce::ComboBox::outlineColourId,   juce::Colour(0xff90c8e8));
    newFolderBtn.onClick = [this]() { openNewFolderDialog(); };
    addAndMakeVisible (newFolderBtn);

    // ── File list viewport ────────────────────────────────────────────────────
    fileListViewport.setViewedComponent (&fileListContainer, false);
    fileListViewport.setScrollBarsShown (true, false);
    fileListViewport.setScrollBarThickness (4);
    fileListViewport.getVerticalScrollBar()
        .setColour (juce::ScrollBar::thumbColourId, juce::Colour(0x500080c8));
    addAndMakeVisible (fileListViewport);

    // ── Analysis panel ────────────────────────────────────────────────────────
    addAndMakeVisible (analysisPanel);

    // ── Sender: URL label (anchor only, text cleared) ─────────────────────────
    urlLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (urlLabel);

    // ── Sender: Copy URL button ───────────────────────────────────────────────
    copyUrlButton.setColour (juce::TextButton::buttonColourId,  juce::Colour(0xffdceeff));
    copyUrlButton.setColour (juce::TextButton::buttonOnColourId,juce::Colour(0xffbcdaef));
    copyUrlButton.setColour (juce::TextButton::textColourOffId, AeroCol::blue());
    copyUrlButton.setColour (juce::ComboBox::outlineColourId,   juce::Colour(0xff90c8e8));
    copyUrlButton.onClick = [this]() {
        juce::SystemClipboard::copyTextToClipboard (processor.getURL());
        copyUrlButton.setButtonText ("Copied!");
        juce::Timer::callAfterDelay (1500, [this]() { copyUrlButton.setButtonText("Copy URL"); });
    };
    addAndMakeVisible (copyUrlButton);

    // ── Sender: Watch folder controls ────────────────────────────────────────
    watchToggle.setToggleState (processor.isWatchEnabled(), juce::dontSendNotification);
    watchToggle.setColour (juce::ToggleButton::textColourId, AeroCol::text());
    watchToggle.setColour (juce::ToggleButton::tickColourId, AeroCol::blue());
    watchToggle.onStateChange = [this]() {
        processor.setWatchEnabled (watchToggle.getToggleState());
        watchFolderLabel.setText (processor.getWatchFolder().getFileName(),
                                  juce::dontSendNotification);
    };
    addAndMakeVisible (watchToggle);

    watchFolderBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour(0xffdceeff));
    watchFolderBtn.setColour (juce::TextButton::textColourOffId, AeroCol::blue());
    watchFolderBtn.setColour (juce::ComboBox::outlineColourId,   juce::Colour(0xff90c8e8));
    watchFolderBtn.onClick = [this]() {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Select Last Exports folder", processor.getWatchFolder());
        chooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc) {
                auto result = fc.getResult();
                if (result.isDirectory()) {
                    processor.setWatchFolder (result);
                    watchFolderLabel.setText (result.getFileName(), juce::dontSendNotification);
                }
            });
    };
    addAndMakeVisible (watchFolderBtn);

    watchFolderLabel.setFont  (juce::Font("Segoe UI", 10.5f, juce::Font::bold));
    watchFolderLabel.setColour (juce::Label::textColourId, AeroCol::text());
    watchFolderLabel.setText  (processor.getWatchFolder().getFileName(), juce::dontSendNotification);
    addAndMakeVisible (watchFolderLabel);

    addAndMakeVisible (watchInfoBtn);

    // ── Settings panel ────────────────────────────────────────────────────────
    settingsPanel.init (&processor, folderList);
    settingsPanel.applyPortBtn.onClick = [this]() {
        int v = settingsPanel.portEditor.getText().getIntValue();
        if (v > 1024 && v < 65535) processor.serverPort = v;
    };
    // Wire visual toggles so changes take effect immediately in the file list
    settingsPanel.showLufsToggle.onStateChange = [this]() {
        processor.showLufsBadge = settingsPanel.showLufsToggle.getToggleState();
        rebuildFileList();
    };
    settingsPanel.compactRowToggle.onStateChange = [this]() {
        processor.compactRows = settingsPanel.compactRowToggle.getToggleState();
        rebuildFileList();
    };
    settingsPanel.autoAnalyseToggle.onStateChange = [this]() {
        processor.autoAnalyse = settingsPanel.autoAnalyseToggle.getToggleState();
    };
    addAndMakeVisible (settingsPanel);

    // ── Status bar ────────────────────────────────────────────────────────────
    statusLabel.setFont  (juce::Font("Segoe UI", 10.5f, juce::Font::bold));
    statusLabel.setColour (juce::Label::textColourId, AeroCol::blueMid());
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setText  ("Connected", juce::dontSendNotification);
    addAndMakeVisible (statusLabel);

    // ── Processor callback ────────────────────────────────────────────────────
    processor.onFilesChanged = [this]() {
        rebuildFileList();
        rebuildFolderChips();
        statusLabel.setText ("Ready", juce::dontSendNotification);
    };

    switchTab (TAB_SONG);
    updateQR();
    rebuildFileList();
    rebuildFolderChips();
    startTimerHz (1);
}

SongSenderEditor::~SongSenderEditor()
{
    processor.onFilesChanged = nullptr;
    stopTimer();
}

// ─── Tab switching ────────────────────────────────────────────────────────────
void SongSenderEditor::switchTab (Tab t)
{
    activeTab = t;

    // Song tab components
    bool song = (t == TAB_SONG);
    dropZone         .setVisible (song);
    folderLabel      .setVisible (song);
    folderCombo      .setVisible (song);
    newFolderBtn     .setVisible (song);
    fileListViewport .setVisible (song);
    analysisPanel    .setVisible (song);
    for (auto* c : folderChips)
        c->setVisible (song);

    // Sender tab components
    bool sender = (t == TAB_SENDER);
    urlLabel        .setVisible (sender);
    copyUrlButton   .setVisible (sender);
    watchToggle     .setVisible (sender);
    watchFolderBtn  .setVisible (sender);
    watchFolderLabel.setVisible (sender);
    watchInfoBtn    .setVisible (sender);

    // Settings
    settingsPanel.setVisible (t == TAB_SETTINGS);

    repaint();
    resized();
}

// ─── Layout ───────────────────────────────────────────────────────────────────
void SongSenderEditor::resized()
{
    const int m    = 10;
    const int topY = kTabH;

    // Tab buttons
    tabSong    .setBounds (0,       0, 80, kTabH);
    tabSender  .setBounds (80,      0, 80, kTabH);
    tabSettings.setBounds (160,     0, 80, kTabH);

    // Status bar (always visible)
    statusLabel.setBounds (m, H()-kStatusH, W()-m*2, kStatusH);

    // ── SONG TAB ──────────────────────────────────────────────────────────────
    if (activeTab == TAB_SONG)
    {
        const int lw = LW();
        const int rw = W() - lw - m;
        int y = topY + m;

        // Drop zone
        dropZone.setBounds (m, y, lw - m*2, 72);
        y += 78;

        // Folder assign row
        folderLabel  .setBounds (m,        y+2,  44,     16);
        folderCombo  .setBounds (m+46,     y,    lw-m*2-70, 20);
        newFolderBtn .setBounds (lw-m-22,  y,    20,     20);
        y += 26;

        // Filter chips
        {
            int cx = m, chipH = 22;
            for (auto* chip : folderChips)
            {
                juce::Font f ("Segoe UI", 10.0f, juce::Font::bold);
                int cw = (int)f.getStringWidth(chip->label) + 18;
                chip->setBounds (cx, y, cw, chipH);
                cx += cw + 5;
            }
            y += 26;
        }

        // File list fills remaining left-column height
        int listH = H() - y - kStatusH - m;
        fileListViewport.setBounds (m, y, lw - m*2, listH);

        // Analysis panel — right column
        analysisPanel.setBounds (lw + m, topY + m, rw - m, H() - kTabH - kStatusH - m*2);
    }

    // ── SENDER TAB ────────────────────────────────────────────────────────────
    if (activeTab == TAB_SENDER)
    {
        const int cx = W()/2;
        // Vertical budget breakdown (from top of content area):
        //  22 "SCAN TO CONNECT" label
        //  12 gap above QR card
        //  qrMax QR image
        //  12 gap below QR card
        //  18 "Open in Safari" text
        //  10 gap
        //  24 Copy URL button
        //  14 gap
        //  20 watch toggle
        //  26 watch folder row
        // Total fixed overhead = 22+12+12+18+10+24+14+20+26 = 158
        int available = H() - kTabH - kStatusH;
        int qrMax = juce::jmax(100, available - 158);
        qrMax = juce::jmin(qrMax, W() - 60);

        // qrTop: kTabH + 34 gives 22px clear above card for the label
        int qrTop = kTabH + 34;

        urlLabel.setBounds (0, qrTop + qrMax, W(), 0);
        copyUrlButton.setBounds (cx-52, qrTop + qrMax + 42, 104, 24);

        int wY = qrTop + qrMax + 80;
        watchToggle     .setBounds (m*2,         wY,     W()-m*4-22, 20);
        watchInfoBtn    .setBounds (W()-m*2-18,  wY+2,   16,         16);
        watchFolderBtn  .setBounds (m*2,         wY+26,  76,         20);
        watchFolderLabel.setBounds (m*2+82,      wY+28,  W()-m*4-100, 16);
    }

    // ── SETTINGS TAB ─────────────────────────────────────────────────────────
    if (activeTab == TAB_SETTINGS)
    {
        settingsPanel.setBounds (0, kTabH, W(), H()-kTabH-kStatusH);
    }
}

// ─── Paint ────────────────────────────────────────────────────────────────────
void SongSenderEditor::paint (juce::Graphics& g)
{
    // Full background
    g.setGradientFill (juce::ColourGradient(
        juce::Colour(0xfff0f8ff), 0.f, 0.f,
        juce::Colour(0xffe8f4fb), 0.f, (float)H(), false));
    g.fillAll();

    // Aero orb top-right
    g.setGradientFill (juce::ColourGradient(
        juce::Colour(0x40ffffff), (float)(W()-60), 0.f,
        juce::Colour(0x008ed8f5), (float)(W()-60), 120.f, true));
    g.fillEllipse ((float)(W()-120), -30.f, 150.f, 150.f);

    // Top gloss
    g.setGradientFill (juce::ColourGradient(
        juce::Colour(0x30ffffff), 0.f, 0.f,
        juce::Colour(0x00ffffff), 0.f, 20.f, false));
    g.fillRect (0, 0, W(), 20);

    // Status bar background
    g.setColour (juce::Colour(0x180060a0));
    g.fillRect  (0, H()-kStatusH, W(), 1);
    g.setGradientFill (juce::ColourGradient(
        juce::Colour(0xf0e8f4fb), 0.f, (float)(H()-kStatusH),
        juce::Colour(0xf0d8ecf8), 0.f, (float)H(), false));
    g.fillRect (0, H()-kStatusH+1, W(), kStatusH-1);

    paintTabBar (g);

    if (activeTab == TAB_SONG)   paintSongTab   (g);
    if (activeTab == TAB_SENDER) paintSenderTab (g);
}

void SongSenderEditor::paintTabBar (juce::Graphics& g)
{
    // Dark navy Aero tab bar background
    g.setGradientFill (juce::ColourGradient(
        juce::Colour(0xff1e3d5c), 0.f, 0.f,
        juce::Colour(0xff163050), 0.f, (float)kTabH, false));
    g.fillRect (0, 0, W(), kTabH);

    // Top gloss on tab bar
    g.setGradientFill (juce::ColourGradient(
        juce::Colour(0x28ffffff), 0.f, 0.f,
        juce::Colour(0x00ffffff), 0.f, (float)(kTabH/2), false));
    g.fillRect (0, 0, W(), kTabH/2);

    // Bottom edge
    g.setColour (juce::Colour(0xff0a2040));
    g.fillRect  (0, kTabH-1, W(), 1);

    // Active tab indicator — bright blue underline
    int tabIdx = (activeTab == TAB_SONG) ? 0 : (activeTab == TAB_SENDER) ? 1 : 2;
    g.setColour (AeroCol::blueMid());
    g.fillRect  (tabIdx * 80, kTabH-2, 80, 2);

    // Logo: "Audio" bold white + "send" light blue, right-aligned in tab bar
    // Draw logo FIRST so the dot paints on top of nothing
    {
        juce::Font boldF ("Segoe UI", 14.0f, juce::Font::bold);
        juce::Font normF ("Segoe UI", 14.0f, juce::Font::plain);
        const juce::String p1 = "Audio";
        const juce::String p2 = "send";
        int w1 = (int)boldF.getStringWidth(p1);
        int w2 = (int)normF.getStringWidth(p2);
        // Leave 22px gap on the right for the dot
        int lx = W() - w1 - w2 - 28;
        int ly = (kTabH - 16) / 2;

        g.setFont   (boldF);
        g.setColour (juce::Colours::white);
        g.drawText  (p1, lx,    ly, w1+2, 16, juce::Justification::centredLeft);
        g.setFont   (normF);
        g.setColour (AeroCol::blueMid());
        g.drawText  (p2, lx+w1, ly, w2+2, 16, juce::Justification::centredLeft);
    }

    // Animated live dot — rightmost element, never overlaps logo
    {
        float pulse = 0.5f + 0.5f * std::sin(liveDotPhase * 6.2831853f);
        float dotR  = 4.0f + 1.5f * pulse;   // 4.0 to 5.5 px radius
        float alpha = 0.55f + 0.45f * pulse;
        // Anchored to right edge with fixed 10px margin
        float dotCX = (float)W() - 10.f - dotR;
        float dotCY = (float)kTabH * 0.5f;
        float dotX  = dotCX - dotR;
        float dotY  = dotCY - dotR;
        // Glow ring
        g.setColour (juce::Colour(0xff00c896).withAlpha(alpha * 0.28f));
        g.fillEllipse (dotX - 4.f, dotY - 4.f, (dotR+4.f)*2.f, (dotR+4.f)*2.f);
        // Dot body
        juce::ColourGradient dotG (
            juce::Colour(0xff80ffdd), dotCX, dotY,
            juce::Colour(0xff00c896), dotCX, dotY + dotR*2.f, false);
        g.setGradientFill (dotG);
        g.fillEllipse (dotX, dotY, dotR*2.f, dotR*2.f);
    }
}

void SongSenderEditor::paintSongTab (juce::Graphics& g)
{
    const int lw = LW();
    const int m  = 10;

    // Left panel background
    g.setGradientFill (juce::ColourGradient(
        juce::Colour(0xfff0f8ff), 0.f, (float)kTabH,
        juce::Colour(0xffe8f4fb), 0.f, (float)H(), false));
    g.fillRect (0, kTabH, lw, H()-kTabH-kStatusH);

    // Divider between left list and right analysis
    g.setGradientFill (juce::ColourGradient(
        juce::Colour(0x00b0d8f0), 0.f, 0.f,
        juce::Colour(0x50b0d8f0), 0.f, (float)(H()/2), false));
    g.fillRect (lw, kTabH, 1, H()-kTabH-kStatusH);
}

void SongSenderEditor::paintSenderTab (juce::Graphics& g)
{
    const int m = 10;
    const int cx = W()/2;
    int available = H() - kTabH - kStatusH;
    int qrMax = juce::jmax(100, available - 158);
    qrMax = juce::jmin(qrMax, W() - 60);
    int qrLeft = cx - qrMax/2;
    int qrTop  = kTabH + 34;

    // "SCAN TO CONNECT" — drawn 12px above the card top (card top = qrTop-10)
    // So label Y = qrTop - 22, height = 14
    g.setColour (AeroCol::blueMid());
    g.setFont   (juce::Font("Segoe UI", 9.5f, juce::Font::bold));
    g.drawText  ("SCAN TO CONNECT", 0, qrTop - 22, W(), 14, juce::Justification::centred);

    // QR card
    auto cardRect = juce::Rectangle<float>(
        (float)(qrLeft-10), (float)(qrTop-10),
        (float)(qrMax+20),  (float)(qrMax+20));
    g.setGradientFill (juce::ColourGradient(
        juce::Colour(0xfdffffff), cardRect.getX(), cardRect.getY(),
        juce::Colour(0xf5eef8fd), cardRect.getX(), cardRect.getBottom(), false));
    g.fillRoundedRectangle (cardRect, 12.0f);
    g.setColour (juce::Colour(0x380090cc));
    g.drawRoundedRectangle (cardRect.reduced(0.5f), 12.0f, 0.8f);
    g.setColour (juce::Colour(0x80ffffff));
    g.drawLine  (cardRect.getX()+16.f, cardRect.getY()+1.f,
                 cardRect.getRight()-16.f, cardRect.getY()+1.f, 1.0f);

    if (qrImage.isValid())
        g.drawImage (qrImage, qrLeft, qrTop, qrMax, qrMax,
                     0, 0, qrImage.getWidth(), qrImage.getHeight());

    // "Open in Safari" — 12px below card bottom (card bottom = qrTop+qrMax+10, so label at +22)
    g.setColour (AeroCol::text());
    g.setFont   (juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    g.drawText  ("Open in Safari on your iPhone",
                 0, qrTop + qrMax + 22, W(), 16, juce::Justification::centred);

    // Watch section divider + label  (wY matches resized())
    int wY = qrTop + qrMax + 80;
    g.setColour (juce::Colour(0x200060a0));
    g.drawLine  (14.f, (float)(wY-10), (float)(W()-14), (float)(wY-10), 0.5f);
    g.setColour (AeroCol::muted());
    g.setFont   (juce::Font("Segoe UI", 9.0f, juce::Font::bold));
    g.drawText  ("LAST EXPORTS FOLDER", 20, wY-8, W()-40, 11, juce::Justification::left);
}

// ─── Folder chips ─────────────────────────────────────────────────────────────
void SongSenderEditor::rebuildFolderChips()
{
    for (auto* c : folderChips)
        removeChildComponent (c);
    folderChips.clear();

    // "All" chip
    auto* all    = folderChips.add (new FolderChip());
    all->label   = "All";
    all->active  = (activeFilter == "All");
    all->onClick = [this]() {
        activeFilter = "All";
        rebuildFolderChips();
        rebuildFileList();
    };
    addAndMakeVisible (all);

    // One chip per folder that actually has files
    juce::StringArray seen;
    for (auto& rec : processor.getFiles())
    {
        if (seen.contains(rec.folder)) continue;
        seen.add (rec.folder);

        auto* chip    = folderChips.add (new FolderChip());
        chip->label   = rec.folder;
        chip->isAuto  = rec.isAutoWatch;
        chip->active  = (activeFilter == rec.folder);
        juce::String fld = rec.folder;
        chip->onClick = [this, fld]() {
            activeFilter = fld;
            rebuildFolderChips();
            rebuildFileList();
        };
        addAndMakeVisible (chip);
    }

    if (activeTab == TAB_SONG)
        resized();
}

// ─── File list ────────────────────────────────────────────────────────────────
void SongSenderEditor::rebuildFileList()
{
    fileRows.clear();

    const auto& allFiles = processor.getFiles();
    const int m = 4;
    // Compact rows: 32px tall instead of 54, less internal padding
    const int rowH = processor.compactRows ? 34 : kRowH;

    // Collect filtered pointers
    std::vector<const FileRecord*> filtered;
    for (auto& rec : allFiles)
    {
        if (activeFilter == "All" || rec.folder == activeFilter)
            filtered.push_back (&rec);
    }

    int totalH = (int)filtered.size() * (rowH + m) + m;
    fileListContainer.setSize (fileListViewport.getWidth()-4, juce::jmax(80, totalH));

    bool showLufs = processor.showLufsBadge;

    for (size_t i = 0; i < filtered.size(); ++i)
    {
        auto row = std::make_unique<FileRowComponent>();
        row->setRecord (*filtered[i], showLufs);
        row->setBounds (0, m+(int)i*(rowH+m), fileListContainer.getWidth(), rowH);

        const juce::String fid = filtered[i]->id;
        row->onClick = [this, fid]() {
            selectedFileId = fid;
            // Update active state
            for (auto& r : fileRows)
                r->setActive (r->getId() == fid);
            // Show analysis in right panel
            for (auto& rec : processor.getFiles())
            {
                if (rec.id == fid)
                {
                    analysisPanel.setRecord (rec.file.getFileName(), rec.analysis);
                    break;
                }
            }
        };

        fileListContainer.addAndMakeVisible (row.get());
        fileRows.push_back (std::move(row));
    }

    // Restore selection highlight
    for (auto& r : fileRows)
        r->setActive (r->getId() == selectedFileId);
}

// ─── Folder combo ─────────────────────────────────────────────────────────────
void SongSenderEditor::rebuildFolderCombo()
{
    folderCombo.clear (juce::dontSendNotification);
    for (int i = 0; i < folderList.size(); ++i)
        folderCombo.addItem (folderList[i], i+1);
    folderCombo.setSelectedId (1, juce::dontSendNotification);
}

void SongSenderEditor::openNewFolderDialog()
{
    auto* dlg = new juce::AlertWindow("New folder tag",
                                       "Enter a folder name:",
                                       juce::MessageBoxIconType::NoIcon);
    dlg->addTextEditor ("name", "", "Name:");
    dlg->addButton ("Create", 1);
    dlg->addButton ("Cancel", 0);
    dlg->enterModalState (true, juce::ModalCallbackFunction::create(
        [this, dlg](int result) {
            if (result == 1)
            {
                juce::String name = dlg->getTextEditorContents("name").trim();
                if (name.isNotEmpty() && !folderList.contains(name))
                {
                    folderList.add (name);
                    rebuildFolderCombo();
                    folderCombo.setText (name, juce::dontSendNotification);
                }
            }
            delete dlg;
        }), false);
}

// ─── QR ───────────────────────────────────────────────────────────────────────
void SongSenderEditor::updateQR()
{
    int qrMax = juce::jmin (W()-40, H()-kTabH-kStatusH-170);
    qrMax = juce::jmax (100, qrMax);
    int ppm = juce::jmax (3, qrMax / 35);
    qrImage = QRCodeGenerator::generate (processor.getURL(), ppm);
    repaint();
}

// ─── Timer ────────────────────────────────────────────────────────────────────
void SongSenderEditor::timerCallback()
{
    static int lastW = 0;
    if (getWidth() != lastW)
    {
        lastW = getWidth();
        if (activeTab == TAB_SENDER) updateQR();
        resized();
    }

    // Animate live dot (advances ~0.4 per second at 1Hz timer)
    liveDotPhase += 0.4f;
    if (liveDotPhase > 1.0f) liveDotPhase -= 1.0f;
    repaint();   // repaints tab bar which includes the live dot

    static int tick = 0;
    if (++tick % 10 == 0)
    {
        rebuildFileList();
        rebuildFolderChips();
    }
}

// ─── Browse dialog ────────────────────────────────────────────────────────────
void SongSenderEditor::openBrowseDialog()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select audio export",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        "*.wav;*.aiff;*.aif;*.mp3;*.flac");

    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (result.existsAsFile()) {
                juce::String folder = folderCombo.getText();
                if (folder.isEmpty()) folder = "Uploads";
                processor.addFile (result, folder);
                statusLabel.setText ("Analysing: " + result.getFileName(),
                                     juce::dontSendNotification);
            }
        });
}

juce::AudioProcessorEditor* SongSenderProcessor::createEditor()
{
    return new SongSenderEditor(*this);
}
