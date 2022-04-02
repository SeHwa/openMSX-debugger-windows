#include "SpriteViewer.h"
#include "ui_SpriteViewer.h"
#include "VDPDataStore.h"
#include "VramSpriteView.h"
#include "PaletteDialog.h"
#include "Convert.h"

// static to feed to PaletteDialog and be used when VDP colors aren't selected
uint8_t SpriteViewer::defaultPalette[32] = {
//    RB  G
    0x00, 0,
    0x00, 0,
    0x11, 6,
    0x33, 7,
    0x17, 1,
    0x27, 3,
    0x51, 1,
    0x27, 6,
    0x71, 1,
    0x73, 3,
    0x61, 6,
    0x64, 6,
    0x11, 4,
    0x65, 2,
    0x55, 5,
    0x77, 7,
};

SpriteViewer::SpriteViewer(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::SpriteViewer)
{
    ui->setupUi(this);
    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    ui->plainTextEdit->setFont(fixedFont);

    // Now hook up some signals and slots.
    // This way we have created the VDPDataStore::instance before our imagewidget.
    // This allows the VDPDatastore to start asking for data as quickly as possible.
    connect(ui->refreshButton, SIGNAL(clicked(bool)),
            &VDPDataStore::instance(), SLOT(refresh()));
    connect(&VDPDataStore::instance(), SIGNAL(dataRefreshed()),
            this, SLOT(VDPDataStoreDataRefreshed()));

    imageWidget = new VramSpriteView();
    //QSizePolicy sizePolicy1(QSizePolicy::Fixed, QSizePolicy::Fixed);
    //sizePolicy1.setHorizontalStretch(0);
    //sizePolicy1.setVerticalStretch(0);
    //sizePolicy1.setHeightForWidth(imageWidget->sizePolicy().hasHeightForWidth());
    //imageWidget->setSizePolicy(sizePolicy1);
    imageWidget->setMinimumSize(QSize(256, 212));
    connect(&VDPDataStore::instance(), SIGNAL(dataRefreshed()),
            imageWidget, SLOT(refresh()));
    ui->spritePatternGenerator_widget->parentWidget()->layout()->replaceWidget(
        ui->spritePatternGenerator_widget, imageWidget);

    imageWidget->setVramSource(VDPDataStore::instance().getVramPointer());

    imageWidgetSingle = new VramSpriteView(nullptr, VramSpriteView::PatternMode, true);
    QSizePolicy sizePolicy3(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sizePolicy3.setHorizontalStretch(0);
    sizePolicy3.setVerticalStretch(0);
    sizePolicy3.setHeightForWidth(imageWidgetSingle->sizePolicy().hasHeightForWidth());
    imageWidgetSingle->setSizePolicy(sizePolicy3);
    imageWidgetSingle->setMinimumSize(QSize(64, 64));
    connect(&VDPDataStore::instance(), SIGNAL(dataRefreshed()),
            imageWidgetSingle, SLOT(refresh()));
    ui->single_spritePatternGenerator_widget->parentWidget()->layout()->replaceWidget(
        ui->single_spritePatternGenerator_widget, imageWidgetSingle);

    imageWidgetSingle->setVramSource(VDPDataStore::instance().getVramPointer());


    imageWidgetSpat = new VramSpriteView(nullptr, VramSpriteView::SpriteAttributeMode);
    imageWidgetSpat->setMinimumSize(QSize(256, 212));
    connect(&VDPDataStore::instance(), SIGNAL(dataRefreshed()),
            imageWidgetSpat, SLOT(refresh()));
    ui->spriteAttributeTable_widget->parentWidget()->layout()->replaceWidget(
        ui->spriteAttributeTable_widget, imageWidgetSpat);

    imageWidgetSpat->setVramSource(VDPDataStore::instance().getVramPointer());


    imageWidgetColor = new VramSpriteView(nullptr, VramSpriteView::ColorMode);
    imageWidgetColor->setMinimumSize(QSize(256, 212));
    connect(&VDPDataStore::instance(), SIGNAL(dataRefreshed()),
            imageWidgetColor, SLOT(refresh()));
    ui->spriteColorTable_widget->parentWidget()->layout()->replaceWidget(
        ui->spriteColorTable_widget, imageWidgetColor);

    imageWidgetColor->setVramSource(VDPDataStore::instance().getVramPointer());


    setPaletteSource(VDPDataStore::instance().getPalettePointer(), true);

    setCorrectVDPData();
    setCorrectEnabled(ui->useVDPRegisters->isChecked());

    connect(imageWidget, SIGNAL(imageClicked(int, int, int, QString)),
            this, SLOT(pgtwidget_mouseClickedEvent(int, int, int, QString)));
    connect(imageWidget, SIGNAL(imagePosition(int, int, int)),
            this, SLOT(pgtwidget_mouseMoveEvent(int, int, int)));


    connect(imageWidgetSpat, SIGNAL(imageClicked(int, int, int, QString)),
            this, SLOT(spatwidget_mouseClickedEvent(int, int, int, QString)));
    connect(imageWidgetSpat, SIGNAL(imagePosition(int, int, int)),
            this, SLOT(spatwidget_mouseMoveEvent(int, int, int)));


    // Since imageWidgetColor and imageWidgetSpat are the same structure we reuse
    // spatwidget_mouseClickedEvent
    connect(imageWidgetColor, SIGNAL(imageClicked(int, int, int, QString)),
            this, SLOT(spatwidget_mouseClickedEvent(int, int, int, QString)));


    // Have spat and color the same spriteselection box synced
    connect(imageWidgetSpat, SIGNAL( spriteboxClicked(int)),
            imageWidgetColor, SLOT(setSpriteboxClicked(int)));

    connect(imageWidgetColor, SIGNAL( spriteboxClicked(int)),
            imageWidgetSpat, SLOT(setSpriteboxClicked(int)));

    //clear pattern selection if spat or color selected
    connect(imageWidgetSpat, SIGNAL( spriteboxClicked(int)),
            imageWidget, SLOT(setCharacterClicked()));

    connect(imageWidgetColor, SIGNAL( spriteboxClicked(int)),
            imageWidget, SLOT(setCharacterClicked()));

    //clear spat and color selection if imagewidget clicked
    connect(imageWidget, SIGNAL( characterClicked(int)),
            imageWidgetSpat, SLOT(setSpriteboxClicked()));

    connect(imageWidget, SIGNAL( characterClicked(int)),
            imageWidgetColor, SLOT(setSpriteboxClicked()));


    connect(ui->cb_displaygrid, SIGNAL(stateChanged(int)),
            this, SLOT(setDrawGrid(int)));

    // And now go fetch the initial data
    VDPDataStore::instance().refresh();
}

SpriteViewer::~SpriteViewer()
{
    delete ui;
}
void SpriteViewer::setPaletteSource(const uint8_t* palSource, bool useVDP)
{
    imageWidget->setPaletteSource(palSource, useVDP);
    imageWidgetSingle->setPaletteSource(palSource, useVDP);
    imageWidgetSpat->setPaletteSource(palSource, useVDP);
    imageWidgetColor->setPaletteSource(palSource, useVDP);
}

void SpriteViewer::refresh()
{
    // All of the code is in the VDPDataStore;
    VDPDataStore::instance().refresh();
}

void SpriteViewer::VDPDataStoreDataRefreshed()
{
    if (ui->useVDPRegisters->isChecked()) {
        decodeVDPregs();
        setCorrectVDPData();
    }
}

void SpriteViewer::pgtwidget_mouseMoveEvent(int /*x*/, int /*y*/, int character)
{
    ui->label_spgt_pat->setText(QString::number(character));
    ui->label_spg_adr->setText(hexValue(pgtAddr + character * 8, 4));
    ui->label_spg_inspat->setText("maybe...");

    imageWidgetSingle->setCharToDisplay(character);
}

void SpriteViewer::pgtwidget_mouseClickedEvent(int x, int y, int character, QString text)
{
    pgtwidget_mouseMoveEvent(x, y, character);

    ui->plainTextEdit->setPlainText(QString("info for pattern %1 (%2)\n%3")
                                    .arg(character)
                                    .arg(hexValue(character, 2))
                                    .arg(text));
}

void SpriteViewer::spatwidget_mouseMoveEvent(int /*x*/, int /*y*/, int character)
{
    ui->label_spat_sprite->setText(QString::number(character));
    ui->label_spat_adr->setText(hexValue(spAtAddr + 4 * character, 4));
    const uint8_t* vramBase = VDPDataStore::instance().getVramPointer();
    if (vramBase != nullptr && character < 32) {
        auto addr = spAtAddr + 4 * character;
        ui->label_spat_posx->setText(QString::number(vramBase[addr + 0]));
        ui->label_spat_posy->setText(QString::number(vramBase[addr + 1]));
        ui->label_spat_pattern->setText(QString::number(vramBase[addr + 2]));
        ui->label_spat_color->setText(QString::number(vramBase[addr + 3]));
    } else {
        ui->label_spat_posx->setText("");
        ui->label_spat_posy->setText("");
        ui->label_spat_pattern->setText("");
        ui->label_spat_color->setText("");
    }
}

void SpriteViewer::spatwidget_mouseClickedEvent(int x, int y, int character, QString text)
{
    spatwidget_mouseMoveEvent(x, y, character);
    ui->plainTextEdit->setPlainText(QString("info for sprite %1 (%2)\n%3")
                                    .arg(character)
                                    .arg(hexValue(character, 2))
                                    .arg(text));
}

void SpriteViewer::setDrawGrid(int state)
{
    imageWidget->setDrawgrid(state == Qt::Checked);
    imageWidgetSingle->setDrawgrid(state == Qt::Checked);
    imageWidgetSpat->setDrawgrid(state == Qt::Checked);
    imageWidgetColor->setDrawgrid(state == Qt::Checked);
}

void SpriteViewer::decodeVDPregs()
{
    const uint8_t* regs = VDPDataStore::instance().getRegsPointer();
    // first determine sprite mode from screenmode
    int v = ((regs[0] & 0x0E) << 1) | ((regs[1] & 0x18) >> 3);
    uint8_t patAdrMask = 255;
    switch (v) {
    case 2: // Text1
    case 10: // Text2
        spriteMode = 0; // no sprites here!
        size16x16 = false;
        magnified = false;
        spritesEnabled = false;
        break;
    case 0: // Graphic1
    case 1: // MultiColor
    case 4: // Graphic2
        spriteMode = 1;
        spritesEnabled = (regs[8] & 2) == 0;
        size16x16 = regs[1] & 2;
        magnified = regs[1] & 1;
        break;
    default:
        spriteMode = 2;
        patAdrMask = 0xfc;
        spritesEnabled = (regs[8] & 2) == 0;
        size16x16 = regs[1] & 2;
        magnified = regs[1] & 1;
    }

    // ui-> ->setEnabled(spritemode != 0);
    spAtAddr = (regs[11] << 15) + ((regs[5] & patAdrMask) << 7);
    pgtAddr = regs[6] << 11;
    spColAddr = spAtAddr & 0xFFFFFDFF; // only used in spritemode2
}

void SpriteViewer::on_le_patterntable_textChanged(const QString& arg1)
{
    if (int i = stringToValue(arg1); i != -1) {
        spAtAddr = i;
        imageWidget->setPatternTableAddress(i);
        imageWidgetSingle->setPatternTableAddress(i);
        imageWidgetSpat->setPatternTableAddress(i);
        imageWidgetColor->setPatternTableAddress(i);
        auto font = ui->le_patterntable->font();
        font.setItalic(false);
        ui->le_patterntable->setFont(font);
    } else {
        auto font = ui->le_patterntable->font();
        font.setItalic(true);
        ui->le_patterntable->setFont(font);
    }
}

void SpriteViewer::on_le_attributentable_textChanged(const QString& arg1)
{
    if (int i = stringToValue(arg1); i != -1) {
        imageWidget->setAttributeTableAddress(i);
        imageWidgetSingle->setAttributeTableAddress(i);
        imageWidgetSpat->setAttributeTableAddress(i);
        imageWidgetColor->setAttributeTableAddress(i);
        auto font = ui->le_attributentable->font();
        font.setItalic(false);
        ui->le_attributentable->setFont(font);
    } else {
        auto font = ui->le_attributentable->font();
        font.setItalic(true);
        ui->le_attributentable->setFont(font);
    }
}

void SpriteViewer::on_useVDPRegisters_toggled(bool checked)
{
    if (checked) {
        decodeVDPregs();
        setCorrectVDPData();
    }
    setCorrectEnabled(checked);
}

void SpriteViewer::setCorrectEnabled(bool checked)
{
    ui->cb_spritemode->setEnabled(!checked);
    ui->cb_size->setEnabled(!checked && spriteMode != 0);
    ui->cb_mag->setEnabled(!checked && spriteMode != 0);

    ui->le_patterntable->setEnabled(!checked);
    ui->le_attributentable->setEnabled(!checked);
    ui->le_colortable->setEnabled(!checked && spriteMode == 2);

    ui->label_size->setEnabled(spriteMode != 0);
    ui->label_mag->setEnabled(spriteMode != 0);
    ui->label_spritesenabled->setEnabled(spritesEnabled);

    ui->gb_colpat->setVisible(spriteMode == 2 || ui->cb_alwaysShowColorTable->isChecked());
}

void SpriteViewer::setCorrectVDPData()
{
    imageWidget->setSize16x16(size16x16);
    imageWidget->setSpritemode(spriteMode);

    imageWidgetSingle->setSize16x16(size16x16);
    imageWidgetSingle->setSpritemode(spriteMode);

    imageWidgetSpat->setSize16x16(size16x16);
    imageWidgetSpat->setSpritemode(spriteMode);

    imageWidgetColor->setSize16x16(size16x16);
    imageWidgetColor->setSpritemode(spriteMode);

    ui->cb_spritemode->setCurrentIndex(spriteMode);
    ui->cb_size->setCurrentIndex(size16x16?1:0);
    ui->cb_mag->setCurrentIndex(magnified?1:0);

    // We write to the lineedits and the textchanged slots will write the values to the imagewidgets...
    ui->le_attributentable->setText(hexValue(spAtAddr, 4));
    ui->le_patterntable->setText(hexValue(pgtAddr, 4));
    ui->le_colortable->setText(hexValue(spColAddr, 4));

    ui->label_spgt_size->setText(size16x16 ? "(16x16)" : "(8x8)");
}

void SpriteViewer::on_cb_size_currentIndexChanged(int index)
{
    size16x16 = index != 0;
    ui->label_spgt_size->setText(size16x16 ? "(16x16)" : "(8x8)");
    imageWidget->setSize16x16(size16x16);
    imageWidgetSingle->setSize16x16(size16x16);
    imageWidgetSpat->setSize16x16(size16x16);
    imageWidgetColor->setSize16x16(size16x16);
}

void SpriteViewer::on_cb_spritemode_currentIndexChanged(int index)
{
    ui->gb_colpat->setVisible(index == 2 || ui->cb_alwaysShowColorTable->isChecked());
    spriteMode = index;
    imageWidget->setSpritemode(index);
    imageWidgetSingle->setSpritemode(index);
    imageWidgetSpat->setSpritemode(index);
    imageWidgetColor->setSpritemode(index);
}

void SpriteViewer::on_le_colortable_textChanged(const QString& arg1)
{
    if (int i = stringToValue(arg1); i != -1) {
        spColAddr = i;
        imageWidget->setColorTableAddress(i);
        imageWidgetSingle->setColorTableAddress(i);
        imageWidgetSpat->setColorTableAddress(i);
        imageWidgetColor->setColorTableAddress(i);
        auto font = ui->le_patterntable->font();
        font.setItalic(false);
        ui->le_patterntable->setFont(font);
    } else {
        auto font = ui->le_patterntable->font();
        font.setItalic(true);
        ui->le_patterntable->setFont(font);
    }
}

void SpriteViewer::on_sp_zoom_valueChanged(int arg1)
{
    int zoom = arg1 * 2;
    imageWidget->setZoom(zoom);
    imageWidgetSpat->setZoom(zoom);
    imageWidgetColor->setZoom(zoom);
}

void SpriteViewer::on_cb_ecinfluence_toggled(bool checked)
{
    imageWidget->setECinfluence(checked);
    imageWidgetSpat->setECinfluence(checked);
    imageWidgetColor->setECinfluence(checked);
}

void SpriteViewer::on_cb_mag_currentIndexChanged(int index)
{
    imageWidget->setUseMagnification(index == 1);
    imageWidgetSpat->setUseMagnification(index == 1);
    imageWidgetColor->setUseMagnification(index == 1);
}

void SpriteViewer::on_cb_alwaysShowColorTable_toggled(bool /*checked*/)
{
    ui->gb_colpat->setVisible(spriteMode == 2 || ui->cb_alwaysShowColorTable->isChecked());
}

void SpriteViewer::on_useVDPPalette_stateChanged(int state)
{
    const uint8_t* palette = VDPDataStore::instance().getPalettePointer();
    if (state == Qt::Checked) {
        setPaletteSource(palette, true);
    } else {
        if (palette != nullptr) memcpy(defaultPalette, palette, 32);
        setPaletteSource(defaultPalette, false);
    }
    imageWidget->refresh();
    imageWidgetSingle->refresh();
    imageWidgetSpat->refresh();
    imageWidgetColor->refresh();
    ui->editPaletteButton->setEnabled(state != Qt::Checked);
}

void SpriteViewer::on_editPaletteButton_clicked(bool /*checked*/)
{
    PaletteDialog* p = new PaletteDialog();
    p->setPalette(defaultPalette);
    p->setAutoSync(true);
    connect(p, SIGNAL(paletteSynced()), imageWidget, SLOT(refresh()));
    connect(p, SIGNAL(paletteSynced()), imageWidgetSingle, SLOT(refresh()));
    connect(p, SIGNAL(paletteSynced()), imageWidgetSpat, SLOT(refresh()));
    connect(p, SIGNAL(paletteSynced()), imageWidgetColor, SLOT(refresh()));
    p->show();
}
