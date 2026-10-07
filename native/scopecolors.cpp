#include "window.h"
#include "palette.h"
#include "appsettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSettings>
#include <QVBoxLayout>

void ScopeWindow::editColors(int selectedChannel) {
    if (colorDialog_) return;
    const auto original = theme_;
    const auto revision = colorRevision_;
    QDialog dialog(this);
    colorDialog_ = &dialog;
    dialog.setObjectName("scopeColorsDialog"); dialog.setWindowTitle("Scope colors");
    dialog.setMinimumWidth(380);
    const auto palette = playerPalette();
    dialog.setPalette(palette);
    dialog.setStyleSheet(R"(
        QDialog { background: #1b252b; }
        QLineEdit, QComboBox { background: #202e35; border: 1px solid #465b62; border-radius: 4px; padding: 5px; }
        QLineEdit[invalid="true"] { border-color: #ef9b9b; }
        QPushButton:disabled { color: #71848d; }
    )");
    auto layout = new QVBoxLayout(&dialog); layout->setContentsMargins(18, 16, 18, 16); layout->setSpacing(14);
    auto channel = new QComboBox; channel->setObjectName("colorChannel"); channel->setAccessibleName("Channel");
    styleCombo(channel);
    for (int id = 0; id < ScopeTheme::SlotCount; ++id)
        channel->addItem(QString("%1  %2").arg(id + 1, 2, 10, QLatin1Char('0')).arg(state_.info.voices.value(id, "Unused tile")));
    channel->addItem("Full mix");
    if (selectedChannel >= 0 && selectedChannel <= ScopeTheme::FullMix) channel->setCurrentIndex(selectedChannel);
    else if (channelList_->currentItem()) channel->setCurrentIndex(channelList_->currentItem()->data(Qt::UserRole).toInt());
    layout->addWidget(channel);
    auto fields = new QGridLayout; fields->setHorizontalSpacing(10); fields->setVerticalSpacing(10);
    layout->addLayout(fields);
    const std::array<QString, 5> labels = {"Waveform", "Label", "Axis", "Border", "Card background"};
    const std::array<QString, 5> keys = {"waveform", "label", "axis", "border", "background"};
    const std::array<QColor ScopeColors::*, 5> members = {&ScopeColors::waveform, &ScopeColors::label, &ScopeColors::axis, &ScopeColors::border, &ScopeColors::background};
    auto colorAt = [&](int i) -> QColor & { return theme_.colors(channel->currentIndex()).*members[i]; };
    std::array<QLineEdit *, 5> hex{};
    std::array<QPushButton *, 5> swatches{};
    for (int i = 0; i < int(labels.size()); ++i) {
        fields->addWidget(new QLabel(labels[i]), i, 0);
        swatches[i] = new QPushButton; swatches[i]->setObjectName(keys[i] + "Picker"); swatches[i]->setFixedSize(34, 28);
        swatches[i]->setToolTip("Choose " + labels[i].toLower() + " color"); swatches[i]->setAccessibleName(swatches[i]->toolTip());
        fields->addWidget(swatches[i], i, 1);
        hex[i] = new QLineEdit; hex[i]->setObjectName(keys[i] + "Hex"); hex[i]->setAccessibleName(labels[i] + " HEX color");
        hex[i]->setPlaceholderText("#RRGGBB"); hex[i]->setMaxLength(7);
        hex[i]->setValidator(new QRegularExpressionValidator(QRegularExpression("#?[0-9A-Fa-f]{6}"), hex[i]));
        fields->addWidget(hex[i], i, 2);
    }
    auto tools = new QHBoxLayout;
    auto reset = new QPushButton("Reset channel"); reset->setObjectName("resetChannelColors"); tools->addWidget(reset);
    auto all = new QPushButton("Apply to all"); all->setObjectName("applyColorsToAll"); tools->addWidget(all);
    layout->addLayout(tools);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    auto valid = [&] {
        bool complete = true;
        for (auto field : hex) {
            const bool acceptable = field->hasAcceptableInput(); complete &= acceptable;
            field->setProperty("invalid", !acceptable);
            field->style()->unpolish(field); field->style()->polish(field);
        }
        buttons->button(QDialogButtonBox::Save)->setEnabled(complete); all->setEnabled(complete);
    };
    auto load = [&] {
        for (int i = 0; i < int(labels.size()); ++i) {
            const auto color = colorAt(i);
            QSignalBlocker block(hex[i]); hex[i]->setText(color.name().toUpper());
            swatches[i]->setStyleSheet(QString("QPushButton { background: %1; border: 1px solid #81969f; border-radius: 5px; padding: 0; }").arg(color.name()));
        }
        valid();
    };
    for (int i = 0; i < int(labels.size()); ++i) {
        connect(hex[i], &QLineEdit::textChanged, &dialog, [&, i](QString text) {
            valid();
            if (!hex[i]->hasAcceptableInput()) return;
            if (!text.startsWith('#')) text.prepend('#');
            const QColor color(text);
            colorAt(i) = color;
            swatches[i]->setStyleSheet(QString("QPushButton { background: %1; border: 1px solid #81969f; border-radius: 5px; padding: 0; }").arg(color.name()));
            update();
        });
        connect(swatches[i], &QPushButton::clicked, &dialog, [&, i] {
            QColorDialog picker(colorAt(i), &dialog);
            picker.setObjectName("scopeColorPicker"); picker.setWindowTitle(labels[i] + " color");
            picker.setOption(QColorDialog::DontUseNativeDialog); picker.setPalette(palette);
            connect(&dialog, &QDialog::finished, &picker, &QDialog::reject);
            if (picker.exec() == QDialog::Accepted && revision == colorRevision_)
                hex[i]->setText(picker.selectedColor().name().toUpper());
        });
    }
    connect(channel, &QComboBox::currentIndexChanged, &dialog, load);
    connect(reset, &QPushButton::clicked, &dialog, [&] { theme_.colors(channel->currentIndex()) = ScopeColors::defaults(channel->currentIndex()); load(); update(); });
    connect(all, &QPushButton::clicked, &dialog, [&] { const auto selected = theme_.colors(channel->currentIndex()); theme_.tiles.fill(selected); theme_.fullMix = selected; update(); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    load();
    const int result = dialog.exec();
    colorDialog_ = nullptr;
    if (result != QDialog::Accepted && revision == colorRevision_) theme_ = original;
    else if (result == QDialog::Accepted) markThemeEdited();
    update();
}

void ScopeWindow::editWindowBackground() {
    if (colorDialog_) return;
    const auto original = theme_.windowBackground;
    QColorDialog picker(original, this);
    colorDialog_ = &picker;
    picker.setObjectName("windowBackgroundPicker"); picker.setWindowTitle("Window background");
    picker.setOption(QColorDialog::DontUseNativeDialog); picker.setPalette(playerPalette());
    connect(&picker, &QColorDialog::currentColorChanged, &picker, [this](const QColor &color) { theme_.windowBackground = color; update(); });
    if (picker.exec() != QDialog::Accepted) theme_.windowBackground = original;
    else markThemeEdited();
    colorDialog_ = nullptr; update();
}

void ScopeWindow::markThemeEdited() {
    themePath_ = ":custom";
    refreshThemes();
}

void ScopeWindow::rememberTheme(bool reportFailure) {
    // Persist the saved selection, not live previews or unsaved color edits.
    QSettings settings(playerSettingsPath(), playerSettingsFormat());
    settings.setFallbacksEnabled(false);
    if (themePath_.isEmpty()) settings.remove("Scopes/Theme");
    else settings.setValue("Scopes/Theme", QFileInfo(themePath_).fileName());
    settings.sync();
    if (reportFailure && settings.status() != QSettings::NoError)
        QMessageBox::warning(this, "Could not save settings",
            "The theme is applied for this session, but its selection could not be saved to:\n" +
            playerSettingsPath() + "\n\nPlease check that this settings location is writable.");
}

void ScopeWindow::restoreTheme() {
    QSettings settings(playerSettingsPath(), playerSettingsFormat());
    settings.setFallbacksEnabled(false);
    const auto name = settings.value("Scopes/Theme").toString();
    if (name.isEmpty()) return;
    ScopeTheme candidate;
    QString error;
    const auto path = QDir(playerThemesDirectory()).filePath(name);
    // Persist only the basename; resolve against the platform's theme directory.
    if (QFileInfo(name).fileName() == name && !name.contains('\\') &&
        name.endsWith(".ini", Qt::CaseInsensitive) && candidate.load(path, error)) {
        theme_ = candidate; themePath_ = path;
    } else {
        // Missing, unreadable or malformed saved themes quietly fall back to Default.
        rememberTheme(false);
    }
}

void ScopeWindow::refreshThemes() {
    QSignalBlocker block(themes_);
    themes_->clear(); themes_->addItem("Default", QString());
    if (themePath_ == ":custom") themes_->addItem("Custom (unsaved)", themePath_);
    const QDir directory(playerThemesDirectory());
    QStringList files;
    for (const auto &file : directory.entryInfoList({"*.ini"}, QDir::Files, QDir::Name | QDir::IgnoreCase))
        files.append(file.absoluteFilePath());
    files.removeDuplicates(); files.sort(Qt::CaseInsensitive);
    for (const auto &file : files) themes_->addItem(QFileInfo(file).completeBaseName(), file);
    themes_->setCurrentIndex(themes_->findData(themePath_));
    themes_->setToolTip("Select to apply a theme. Files: " + directory.absolutePath());
}

void ScopeWindow::applyTheme() {
    const auto path = themes_->currentData().toString();
    if (path == ":custom") return;
    ScopeTheme candidate;
    QString error;
    if (!path.isEmpty() && !candidate.load(path, error)) {
        QMessageBox::warning(this, "Could not load theme", error);
        refreshThemes(); return;
    }
    ++colorRevision_;
    theme_ = candidate; themePath_ = path; rememberTheme(); refreshThemes(); update();
}

void ScopeWindow::saveTheme() {
    const auto directory = playerThemesDirectory();
    QDialog dialog(this);
    dialog.setObjectName("saveThemeDialog"); dialog.setWindowTitle("Save theme"); dialog.setMinimumWidth(340);
    dialog.setPalette(playerPalette());
    dialog.setStyleSheet(R"(
        QDialog { background: #1b252b; }
        QLineEdit { background: #202e35; color: #dce5e8; border: 1px solid #465b62; border-radius: 4px; padding: 6px; }
        QLabel#themeNameHint { color: #b8cccf; }
        QPushButton:disabled { color: #71848d; }
    )");
    auto layout = new QVBoxLayout(&dialog); layout->setContentsMargins(18, 16, 18, 16); layout->setSpacing(10);
    layout->addWidget(new QLabel("Theme name"));
    auto name = new QLineEdit; name->setObjectName("themeName"); name->setAccessibleName("Theme name"); name->setMaxLength(80);
    name->setText(themePath_.isEmpty() || themePath_ == ":custom" ? "My theme" : QFileInfo(themePath_).completeBaseName());
    layout->addWidget(name);
    auto hint = new QLabel; hint->setObjectName("themeNameHint"); hint->setWordWrap(true); layout->addWidget(hint);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    auto save = buttons->button(QDialogButtonBox::Save); save->setDefault(true);
    QString path;
    auto validate = [&] {
        auto title = name->text().trimmed();
        if (title.endsWith(".ini", Qt::CaseInsensitive)) title.chop(4);
        const QRegularExpression forbidden(R"([<>:"/\\|?*\x00-\x1f])");
        const QRegularExpression reserved(QString::fromUtf8("^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\\.|$)"), QRegularExpression::CaseInsensitiveOption);
        const bool valid = !title.isEmpty() && !title.endsWith('.') && !title.endsWith(' ') &&
            !forbidden.match(title).hasMatch() && !reserved.match(title).hasMatch() && title.compare("Default", Qt::CaseInsensitive) != 0;
        path = valid ? QDir(directory).filePath(title + ".ini") : QString();
        const bool replacing = valid && QFileInfo::exists(path);
        save->setEnabled(valid); save->setText(replacing ? "Replace" : "Save");
        hint->setText(!valid ? "Please choose a different theme name." : replacing ? "A theme with this name already exists." : QString());
    };
    connect(name, &QLineEdit::textChanged, &dialog, validate);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        validate(); if (!save->isEnabled()) return;
        QString error;
        if (!QDir().mkpath(directory)) error = "Could not create the themes folder.";
        else if (theme_.save(path, error)) {
            themePath_ = QFileInfo(path).absoluteFilePath(); rememberTheme(); refreshThemes(); dialog.accept(); return;
        }
        hint->setText("Could not save theme: " + error);
    });
    validate(); name->selectAll(); name->setFocus(); dialog.exec();
}
